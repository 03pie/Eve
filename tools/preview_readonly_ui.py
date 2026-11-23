"""One registered D10 configuration UI sequence; the owner alone manages ACL/processes.

Reads may be retried. Every mutation has a fixed CreateNew intent and is never replayed.
This adapter neither inherits the four-run Driver nor manufactures its registration.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import stat
import subprocess
import sys
import time

# The owner launches isolated Python (-I); import only the explicitly staged sibling.
sys.path.insert(0, str(Path(__file__).resolve().parent))
import preview_ui as ui

Unknown = ui.Unknown
ACTION = Path(__file__).with_name('preview_ui_action.ps1')
PHASES = ('owner-ready', 'ui-applied', 'owner-protected', 'ui-save-failed',
          'owner-restored', 'ui-saved', 'ui-complete')
FAILURE_MESSAGES = ('configuration write lock is unavailable',
                    'cannot create configuration temporary file',
                    'cannot write complete configuration',
                    'configuration temporary file commit failed')


def require(condition, message):
    if not condition:
        raise Unknown(message)


def sha(path):
    result = hashlib.sha256()
    with Path(path).open('rb') as stream:
        while block := stream.read(1024 * 1024):
            result.update(block)
    return result.hexdigest()


def hex64(value):
    return isinstance(value, str) and re.fullmatch('[0-9a-f]{64}', value) is not None


def safe_path(value):
    require(isinstance(value, (str, Path)), 'A literal absolute path is required')
    path = Path(value)
    require(path.is_absolute() and '..' not in path.parts, 'Relative/noncanonical evidence/tool path refused')
    for part in (path, *path.parents):
        if part.exists() or part.is_symlink():
            info = part.lstat()
            require(not stat.S_ISLNK(info.st_mode) and not getattr(info, 'st_file_attributes', 0)
                    & stat.FILE_ATTRIBUTE_REPARSE_POINT, 'Reparse path refused: ' + str(part))
    resolved = path.resolve()
    require(ui.norm(path) == ui.norm(resolved), 'Noncanonical path refused')
    return resolved


def same_path(left, right):
    return ui.norm(safe_path(left)) == ui.norm(safe_path(right))


def inside(path, root):
    return os.path.commonpath([ui.norm(safe_path(path)), ui.norm(safe_path(root))]) == ui.norm(root)


def positive(value):
    return type(value) is int and value > 0


def configuration_value(value):
    require(isinstance(value, dict) and set(value) == {'formatVersion', 'plugins'}
            and type(value['formatVersion']) is int and value['formatVersion'] == 1
            and isinstance(value['plugins'], list) and len(value['plugins']) == 1, 'Unexpected D10 configuration')
    plugin = value['plugins'][0]
    require(isinstance(plugin, dict) and set(plugin) == {'id', 'plugin', 'config'}
            and plugin['id'] == 'notes' and plugin['plugin'] == 'demo.notes'
            and isinstance(plugin['config'], dict) and set(plugin['config']) == {'title'}
            and isinstance(plugin['config']['title'], str) and 0 < len(plugin['config']['title']) <= 256,
            'Unexpected D10 notes value')


def validate_registration(registration, path, expected_sha):
    required = {'schema', 'nonce', 'scopeRoot', 'evidenceRoot', 'packageRoot', 'configurationFile',
                'manifestSha256', 'qaSid', 'sessionId', 'target', 'wrapper', 'baselineValue', 'wantedValue',
                'baselineSha256', 'aclRegistrationPath', 'aclRegistrationSha256', 'aclProducerPath',
                'aclProducerSha256', 'tools'}
    require(isinstance(registration, dict) and required <= set(registration)
            and registration['schema'] == 'cordis.preview.readonly-save.registration/1', 'Wrong D10 registration schema')
    raw = Path(path).read_bytes()
    require(hex64(expected_sha) and hashlib.sha256(raw).hexdigest() == expected_sha
            and json.loads(raw.decode('utf-8-sig')) == registration, 'External registration SHA differs')
    require(isinstance(registration['nonce'], str)
            and re.fullmatch('[A-Za-z0-9_-]{16,128}', registration['nonce']) is not None, 'Invalid campaign nonce')
    scope = safe_path(registration['scopeRoot'])
    require(same_path(registration['evidenceRoot'], scope / 'evidence')
            and same_path(path, scope / 'evidence/registration.json')
            and same_path(registration['packageRoot'], scope / 'permissions/acl/application')
            and same_path(registration['configurationFile'], scope / 'permissions/acl/data/session.yaml')
            and same_path(registration['aclRegistrationPath'], scope / 'permissions/acl-registration.json'),
            'D10 private layout differs')
    require(positive(registration['sessionId']) and isinstance(registration['qaSid'], str)
            and re.fullmatch(r'S-1-(?:\d+-)+\d+', registration['qaSid']) is not None, 'Invalid QA identity')
    fields = {'pid', 'creationFileTime', 'path', 'sessionId', 'ownerSid', 'parentPid'}
    for key in ('target', 'wrapper'):
        descriptor = registration[key]
        require(isinstance(descriptor, dict) and fields <= set(descriptor)
                and positive(descriptor['pid']) and positive(descriptor['parentPid'])
                and isinstance(descriptor['creationFileTime'], str)
                and re.fullmatch('[1-9][0-9]{0,19}', descriptor['creationFileTime']) is not None
                and type(descriptor['sessionId']) is int and descriptor['sessionId'] == registration['sessionId']
                and descriptor['ownerSid'] == registration['qaSid'], 'Invalid registered ' + key + ' identity')
        safe_path(descriptor['path'])
    require(registration['target']['parentPid'] == registration['wrapper']['pid']
            and registration['target']['pid'] != registration['wrapper']['pid']
            and same_path(registration['target']['path'], scope / 'permissions/acl/application/cordis_config_workbench.exe'),
            'Target is not this owner configuration child')
    for name in ('manifestSha256', 'baselineSha256', 'aclRegistrationSha256', 'aclProducerSha256'):
        require(hex64(registration[name]), 'Invalid registered digest: ' + name)
    configuration_value(registration['baselineValue']); configuration_value(registration['wantedValue'])
    require(registration['baselineValue'] != registration['wantedValue'], 'C0 and wanted target must differ')
    require(sha(scope / 'permissions/acl/application/build-manifest.json') == registration['manifestSha256'],
            'Registered package manifest differs')
    require(sha(safe_path(registration['aclRegistrationPath'])) == registration['aclRegistrationSha256']
            and sha(safe_path(registration['aclProducerPath'])) == registration['aclProducerSha256'],
            'Original ACL registration/producer differs')
    require(not inside(registration['aclProducerPath'], scope / 'permissions/acl/data'), 'ACL producer is in protected data')
    acl = ui.read_json(registration['aclRegistrationPath'])
    require(acl.get('schema') == 'cordis.preview.acl-registration/1'
            and same_path(acl.get('scopeParent'), scope / 'permissions')
            and same_path(acl.get('createdBy', {}).get('path'), registration['aclProducerPath'])
            and acl.get('createdBy', {}).get('sha256') == registration['aclProducerSha256'], 'Original ACL producer provenance differs')
    manifest = ui.read_json(scope / 'permissions/acl/application/build-manifest.json')
    helper = [row for row in manifest.get('files', []) if row.get('path') == 'verification/preview-acl.ps1']
    require(len(helper) == 1 and helper[0].get('sha256') == registration['aclProducerSha256'],
            'ACL producer differs from the registered package source')


def verify_tools(registration):
    tools = registration['tools']
    require(isinstance(tools, list) and tools, 'Explicit tool list is required')
    root = Path(__file__).resolve().parents[1]
    seen = set()
    for row in tools:
        require(isinstance(row, dict) and {'path', 'bytes', 'sha256'} <= set(row)
                and type(row['bytes']) is int and row['bytes'] >= 0 and hex64(row['sha256']), 'Invalid tool record')
        path = safe_path(row['path']); key = ui.norm(path)
        require(inside(path, root) and key not in seen and path.is_file(), 'Tool is duplicate/outside tool root')
        seen.add(key)
        require(path.stat().st_size == row['bytes'] and sha(path) == row['sha256'], 'Tool bytes differ: ' + str(path))
    mandatory = (Path(__file__), Path(ui.__file__), ACTION, Path(sys.executable),
                 root / 'delivery/windows/verification/preview-ui.psm1',
                 root / 'delivery/windows/verification/preview-windows.psm1')
    require(all(ui.norm(safe_path(path)) in seen for path in mandatory), 'Required UI/runtime tool is not listed')


def validate_identity_reply(raw, registration, caller_pid):
    require(raw.get('identityVerified') is True and isinstance(raw.get('actualIdentity'), dict),
            'Action helper did not verify actual identities')
    for name in ('target', 'wrapper'):
        actual = raw['actualIdentity'].get(name, {})
        expected = registration[name]
        require(all(actual.get(key) == expected[key] for key in ('pid', 'creationFileTime', 'sessionId', 'ownerSid', 'parentPid'))
                and same_path(actual.get('path'), expected['path']), 'Actual ' + name + ' was replaced/reused')
    actor = raw.get('actor', {})
    require(positive(actor.get('pid')) and actor.get('parentPid') == caller_pid
            and actor.get('ownerSid') == registration['qaSid'] and actor.get('sessionId') == registration['sessionId']
            and actor.get('administrator') is False and actor.get('integrityRids') == [8192]
            and actor.get('interactive') is True and actor.get('is64Bit') is True,
            'Actual action actor is not the ordinary interactive QA child')
    caller = actor.get('caller', {})
    require(caller.get('pid') == caller_pid and caller.get('parentPid') == registration['wrapper']['pid']
            and caller.get('ownerSid') == registration['qaSid'] and caller.get('sessionId') == registration['sessionId']
            and same_path(caller.get('path'), sys.executable), 'Actual Python caller identity differs')


def acl_record(record, action, registration):
    require(isinstance(record, dict) and type(record.get('exitCode')) is int, 'Missing original ACL operation')
    evidence = safe_path(registration['evidenceRoot'])
    for kind in ('command', 'stdout', 'stderr', 'report'):
        path, expected = record.get(kind + 'Path'), record.get(kind + 'Sha256')
        require(hex64(expected) and (inside(path, evidence) or
                (kind == 'report' and inside(path, Path(registration['scopeRoot']) / 'permissions')))
                and sha(safe_path(path)) == expected, 'Original ACL ' + kind + ' evidence differs')
    command = ui.read_json(record['commandPath'])
    arguments = command.get('command', [])
    require(command.get('started') is True and positive(command.get('pid'))
            and '-Action' in arguments and arguments[arguments.index('-Action') + 1] == action
            and '-File' in arguments and same_path(arguments[arguments.index('-File') + 1], registration['aclProducerPath'])
            and '-Registration' in arguments
            and same_path(arguments[arguments.index('-Registration') + 1], registration['aclRegistrationPath'])
            and '-Target' in arguments and arguments[arguments.index('-Target') + 1] == 'data',
            'Original ACL command is not this producer/data action')
    require(record.get('processTerminated') is False and record.get('timedOut') is False
            and len(record.get('logs', [])) == 2
            and all(row.get('eof') is True and row.get('truncated') is False and row.get('seen') == row.get('saved')
                    for row in record['logs']), 'ACL child capture is incomplete or terminated')
    report = ui.read_json(record['reportPath'])
    require(record['logs'][0]['saved'] == Path(record['stdoutPath']).stat().st_size
            and record['logs'][1]['saved'] == Path(record['stderrPath']).stat().st_size
            and ui.read_json(record['stdoutPath']) == report, 'Original ACL stdout/report or capture length differs')
    require(report.get('schema') == 'cordis.preview.acl-observation/1' and report.get('action') == action
            and same_path(report.get('target'), Path(registration['configurationFile']).parent)
            and report.get('tokenSid') == registration['qaSid']
            and report.get('registrationSha256') == registration['aclRegistrationSha256']
            and record.get('report') == report, 'Original ACL report binding differs')
    return report


def protection_proof(facts, registration):
    require(facts.get('effectiveProtectionVerified') is True
            and facts.get('baselineSha256') == registration['baselineSha256'] and hex64(facts.get('stateSha256')),
            'Owner protection proof is incomplete')
    reports = []
    for key, action in (('protect', 'Protect'), ('probe', 'Probe')):
        record = facts.get(key, {}); report = acl_record(record, action, registration)
        probe = report.get('probe', {})
        require(record['exitCode'] == 0 and report.get('passed') is True and probe.get('passed') is True
                and probe.get('createNewDenied') is True and probe.get('writeDataError') == 5
                and probe.get('deleteAccessError') == 5 and probe.get('deleteProbeUnlinksFile') is False
                and same_path(probe.get('existingFile'), registration['configurationFile']), 'Effective denial5 is not proven')
        reports.append(report)
    state_path = safe_path(reports[0].get('statePath'))
    require(inside(state_path, Path(registration['scopeRoot']) / 'permissions')
            and sha(state_path) == facts['stateSha256'], 'Original data ACL state differs')
    state = ui.read_json(state_path)
    require(state.get('schema') == 'cordis.preview.acl-state/1'
            and state.get('registrationSha256') == registration['aclRegistrationSha256']
            and state.get('tokenSid') == registration['qaSid']
            and same_path(state.get('target'), Path(registration['configurationFile']).parent)
            and isinstance(state.get('objects'), list) and len(state['objects']) > 0, 'ACL state has wrong ownership')
    paths = [row.get('relativePath') for row in state['objects']]
    require(all(isinstance(path, str) and not Path(path).is_absolute() and '..' not in Path(path).parts for path in paths)
            and len(set(paths)) == len(paths), 'ACL state object set is ambiguous')
    return state_path, state


def restoration_proof(facts, protected, registration, state_path, state):
    require(facts.get('restorationVerified') is True
            and facts.get('baselineSha256') == registration['baselineSha256']
            and facts.get('stateSha256') == protected['stateSha256'] == sha(state_path), 'Owner restore proof is incomplete')
    original = facts.get('originalRestore', {}); report = acl_record(original, 'Restore', registration)
    require(original['exitCode'] in (0, 1) and report.get('passed') is (original['exitCode'] == 0)
            and same_path(report.get('statePath'), state_path), 'Original Restore outcome was not retained')
    comparison = facts.get('semanticComparison', {})
    objects = comparison.get('objects', [])
    expected = {row['relativePath']: row for row in state['objects']}
    require(comparison.get('passed') is True and comparison.get('identityVerified') is True
            and comparison.get('rule') == 'OwnerGroupOrderedDaclBinaryAllFlagsExceptDaclAutoInherited/1'
            and comparison.get('allowedControlFlagsDifference') == 0x0400
            and comparison.get('writeDataError') == 0 and comparison.get('deleteAccessError') == 0
            and comparison.get('filesWrittenOrDeleted') is False and comparison.get('mismatches') == []
            and comparison.get('stateSha256') == protected['stateSha256']
            and comparison.get('objectCount') == len(expected) == len(objects)
            and len({row.get('relativePath') for row in objects}) == len(expected)
            and all(row.get('relativePath') in expected and row.get('identityVerified') is True
                    and row.get('expectedSddl') == expected[row['relativePath']]['sddl']
                    and row.get('comparison', {}).get('passed') is True for row in objects),
            'Independent identity/ordered-descriptor/access restoration proof is incomplete')


def configuration_entries(snapshot):
    page = ui.control(snapshot, 'CordisConfigurationPage')
    tables = [item for item in snapshot['elements'] if item['type'] == 'ControlType.Table'
              and item['class'] == 'ElaTableView' and not item['offscreen'] and ui.in_scope(snapshot, item, page)]
    require(len(tables) == 1, 'Expected one actual configuration entries table')
    table = tables[0]
    require(not table['id'] or ui.id_matches(table['id'], 'CordisConfigEntries'), 'Unexpected entries table ID')
    require({'GridPatternIdentifiers.Pattern', 'TablePatternIdentifiers.Pattern'} <= set(table['patterns']),
            'Actual table does not expose grid/table patterns')
    children = [item for item in snapshot['elements'] if item.get('parentIndex') == table['index']]
    bounds = table.get('rect')
    require(bounds and all(type(bounds.get(key)) in (int, float) and math.isfinite(bounds[key])
                          for key in ('x', 'y', 'width', 'height'))
            and bounds['width'] > 0 and bounds['height'] > 0, 'Table bounds are unavailable')

    def cells(kind):
        values = [item for item in children if item['type'] == kind]
        require(len(values) == 4, 'Configuration table is not exactly four columns/one row')
        for item in values:
            rect = item.get('rect')
            require(not item['offscreen'] and not item.get('nameTruncated') and not item.get('textTruncated')
                    and rect and all(type(rect.get(key)) in (int, float) and math.isfinite(rect[key])
                                     for key in ('x', 'y', 'width', 'height'))
                    and rect['width'] > 0 and rect['height'] > 0, 'Incomplete table cell')
            require(rect['x'] >= bounds['x'] - 1 and rect['y'] >= bounds['y'] - 1
                    and rect['x'] + rect['width'] <= bounds['x'] + bounds['width'] + 1
                    and rect['y'] + rect['height'] <= bounds['y'] + bounds['height'] + 1, 'Table cell is outside its actual table')
        values.sort(key=lambda item: item['rect']['x'])
        require(len({item['index'] for item in values}) == 4
                and len({(item['rect']['y'], item['rect']['height']) for item in values}) == 1
                and all(left['rect']['x'] + left['rect']['width'] <= right['rect']['x'] + 1
                        for left, right in zip(values, values[1:])), 'Table column geometry is ambiguous')
        return [item['name'] for item in values]

    require(cells('ControlType.Header') == ['条目', '期望', '运行', '实例'], 'Configuration headers differ')
    values = cells('ControlType.DataItem')
    require(values[:3] == ['notes', '启用', '运行中'] and re.fullmatch('[1-9][0-9]*', values[3]) is not None,
            'Configuration entry is not the live wanted notes instance')
    return values


def target_facts(snapshot, wanted, editor_text, revision=None, entries=None):
    actual_revision = ui.config_applied(snapshot)
    require(actual_revision > 0 and (revision is None or actual_revision == revision), 'Accepted revision changed')
    editor = ui.config_control(snapshot, 'CordisConfigEditor', editor_text)
    require(json.loads(editor['value']) == wanted, 'Accepted editor target differs')
    actual_entries = configuration_entries(snapshot)
    require(entries is None or actual_entries == entries, 'Actual configuration entry/instance changed')
    require(ui.control(snapshot, 'CordisConfigSave')['enabled'], 'Explicit Save is unavailable')
    return {'revision': actual_revision, 'targetValue': wanted, 'entries': actual_entries}


def failed_save_facts(snapshot, wanted, editor_text, revision, entries):
    facts = target_facts(snapshot, wanted, editor_text, revision, entries)
    require(ui.text(snapshot, 'CordisConfigSaveStatus') == '磁盘：保存失败', 'Save is not an actual disk failure')
    details = ui.text(snapshot, 'CordisConfigDetails')
    require(any(message in details for message in FAILURE_MESSAGES), 'Actual file transaction failure message is missing')
    return {**facts, 'saveError': details, 'dirtyBooleanObserved': False}


class ReadOnlySaveUi:
    def __init__(self, registration_path, expected_sha):
        self.path = safe_path(registration_path); self.expected_sha = expected_sha
        self.registration = ui.read_json(self.path)
        validate_registration(self.registration, self.path, expected_sha)
        self.control = safe_path(Path(self.registration['evidenceRoot']) / 'control')
        self.output = safe_path(Path(self.registration['evidenceRoot']) / 'ui')
        self.output.mkdir(exist_ok=True)
        ui.write_new(self.output / 'attempt.json', {'nonce': self.registration['nonce'], 'registrationSha256': expected_sha,
                     'pid': os.getpid(), 'utc': time.time(), 'replayAllowed': False})
        self.intents = safe_path(self.output / 'intents'); self.intents.mkdir(exist_ok=False)
        self.sequence = 0; self.seen = {}; self.required_phase = 1
        self.deadline = time.monotonic() + 600
        self.input = None; self.last_snapshot_path = None

    def record(self, label, facts):
        self.sequence += 1
        path = self.output / f'{self.sequence:04d}-{label}.json'
        ui.write_new(path, {'pid': self.registration['target']['pid'], 'utc': time.time(), **facts})
        return path

    def validate(self):
        validate_registration(ui.read_json(self.path), self.path, self.expected_sha)
        for path in (self.control, self.output, self.intents):
            safe_path(path)
        require(os.getppid() == self.registration['wrapper']['pid'], 'Adapter parent is not the registered live owner')
        require(not (self.control / 'owner-aborted.json').exists(), 'Owner aborted; no further input is authorized')
        verify_tools(self.registration)
        require(time.monotonic() < self.deadline, 'D10 UI sequence time budget exceeded')

    def chain(self, maximum):
        self.validate()
        allowed = {f'{index:02d}-{phase}.json' for index, phase in enumerate(PHASES, 1)} | {'owner-aborted.json'}
        require(all(path.is_file() and path.name in allowed for path in self.control.iterdir()), 'Unknown control phase entry')
        previous = self.expected_sha; result = []
        for index, phase in enumerate(PHASES, 1):
            path = self.control / f'{index:02d}-{phase}.json'
            if not path.exists():
                require(not any((self.control / f'{later:02d}-{PHASES[later-1]}.json').exists()
                                for later in range(index + 1, 8)), 'Out-of-order phase file')
                break
            require(index <= maximum, 'Unexpected later phase; input cannot be replayed')
            safe_path(path); raw = path.read_bytes(); digest = hashlib.sha256(raw).hexdigest()
            value = json.loads(raw.decode('utf-8-sig'))
            require(value.get('schema') == 'cordis.preview.readonly-save.phase/1'
                    and value.get('nonce') == self.registration['nonce']
                    and value.get('registrationSha256') == self.expected_sha and value.get('phase') == phase
                    and value.get('previousSha256') == previous and value.get('pid') == self.registration['target']['pid']
                    and value.get('creationFileTime') == self.registration['target']['creationFileTime']
                    and isinstance(value.get('utc'), str) and value['utc'] and isinstance(value.get('facts'), dict),
                    'Phase identity/predecessor differs')
            require(path not in self.seen or self.seen[path] == digest, 'Immutable phase was replaced')
            self.seen[path] = digest; previous = digest; result.append(value)
        return result

    def context(self):
        values = self.chain(self.required_phase)
        require(len(values) == self.required_phase, 'Required owner/predecessor phase is missing')
        return values

    def reserve(self, label):
        self.context()
        require(isinstance(label, str) and re.fullmatch('[a-z][a-z0-9-]{0,63}', label) is not None, 'Invalid fixed intent name')
        ui.write_new(self.intents / (label + '.json'), {'nonce': self.registration['nonce'],
                     'registrationSha256': self.expected_sha, 'phase': PHASES[self.required_phase],
                     'mutation': label, 'pid': self.registration['target']['pid'], 'utc': time.time()})

    def action(self, action, *, label=None, suffix='', name=None, value=None, selector=None):
        self.context()
        if action != 'Read':
            require(action in ('Expand', 'SetValue', 'Invoke', 'Close') and label, 'Explicit mutation intent is required')
            self.reserve(label)
            require(self.input is not None, 'Native foreground guard is unavailable')
            self.record('foreground-guard', self.input.wait_guard())
        self.sequence += 1
        request = self.output / f'{self.sequence:04d}-{action}-request.json'
        output = request.with_name(request.stem.replace('-request', '-raw') + '.json')
        selected = {'id': suffix, 'name': name, 'scope': None, 'type': None,
                    'emptyId': False, 'matchValue': False, 'expectedValue': None}
        if selector:
            selected.update(selector)
        ui.write_new(request, {'pid': self.registration['target']['pid'], 'entry': 'configuration', 'action': action,
                     'selector': selected, 'value': value, 'callerPid': os.getpid(),
                     'expectedIdentity': {key: self.registration[key] for key in ('target', 'wrapper')}})
        powershell = Path(os.environ.get('SystemRoot', r'C:\Windows')) / 'System32/WindowsPowerShell/v1.0/powershell.exe'
        command = [str(powershell), '-NoProfile', '-NonInteractive', '-STA', '-ExecutionPolicy', 'Bypass',
                   '-File', str(ACTION), '-Request', str(request), '-Output', str(output)]
        with request.with_suffix('.stdout').open('xb') as stdout, request.with_suffix('.stderr').open('xb') as stderr:
            result = subprocess.run(command, stdout=stdout, stderr=stderr,
                                    timeout=min(12, max(1, self.deadline-time.monotonic())),
                                    creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        raw = ui.read_json(output)
        require(result.returncode == 0 and raw.get('ok') is True and raw.get('pid') == self.registration['target']['pid'],
                'Guarded UI action is unknown; do not replay: ' + str(output))
        validate_identity_reply(raw, self.registration, os.getpid())
        if action == 'Read':
            snapshot = raw.get('snapshot', {})
            require(snapshot.get('pid') == self.registration['target']['pid'] and not snapshot.get('errors')
                    and not snapshot.get('truncated') and snapshot.get('readOnly') is True, 'Incomplete live UI observation')
            self.last_snapshot_path = output
            return snapshot

    def snapshot(self):
        last = None
        for attempt in range(3):
            try:
                return self.action('Read')
            except (Unknown, subprocess.TimeoutExpired) as error:
                last = str(error); self.record('read-unknown', {'attempt': attempt + 1, 'error': last})
                time.sleep(.1)
        raise Unknown('Read-only observations remain unknown: ' + str(last))

    def wait(self, predicate, description):
        until = min(self.deadline, time.monotonic() + 30)
        last = None
        while time.monotonic() < until:
            try:
                snapshot = self.snapshot(); result = predicate(snapshot)
                if result:
                    return snapshot
            except Unknown as error:
                last = str(error)
            time.sleep(.15)
        raise Unknown(description + ': ' + str(last))

    def wait_phase(self, number):
        until = min(self.deadline, time.monotonic() + 150)
        while time.monotonic() < until:
            try:
                phases = self.chain(number)
            except (OSError, json.JSONDecodeError) as error:
                self.record('phase-read-unknown', {'phase': number, 'error': str(error)})
                time.sleep(.1)
                continue
            if len(phases) == number:
                self.required_phase = number
                return phases[-1]['facts']
            time.sleep(.1)
        raise Unknown('Owner phase timed out; no input replay')

    def publish(self, number, facts, snapshot=True):
        self.context()
        if snapshot:
            require(self.last_snapshot_path is not None, 'No guarded snapshot for phase receipt')
            facts = {**facts, 'snapshotPath': str(self.last_snapshot_path), 'snapshotSha256': sha(self.last_snapshot_path)}
        path = self.control / f'{number:02d}-{PHASES[number-1]}.json'
        require(number == self.required_phase + 1, 'UI cannot skip phase predecessor')
        ui.write_new(path, {'schema': 'cordis.preview.readonly-save.phase/1', 'nonce': self.registration['nonce'],
                     'registrationSha256': self.expected_sha, 'phase': PHASES[number-1],
                     'previousSha256': sha(self.control / f'{number-1:02d}-{PHASES[number-2]}.json'),
                     'utc': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
                     'pid': self.registration['target']['pid'], 'creationFileTime': self.registration['target']['creationFileTime'],
                     'facts': facts})
        self.required_phase = number

    def unchanged_c0(self):
        file = safe_path(self.registration['configurationFile'])
        require(sha(file) == self.registration['baselineSha256']
                and json.loads(file.read_text(encoding='utf-8-sig')) == self.registration['baselineValue'], 'Original C0 changed')

    def invoke(self, suffix, label):
        self.action('Invoke', suffix=suffix, label=label)

    def run(self):
        ready = self.wait_phase(1)
        token = ready.get('token', {})
        require(token.get('qaSid') == self.registration['qaSid'] and token.get('sessionId') == self.registration['sessionId']
                and token.get('administrator') is False and token.get('integrityRid') == 8192
                and ready.get('baselineSha256') == self.registration['baselineSha256'], 'Owner ready identity/C0 proof differs')
        self.unchanged_c0()
        snapshot = self.snapshot()
        self.input = ui.NativeInput(self.registration['target']['pid'], self.record, self.snapshot)
        self.reserve('activate-initial'); self.input.activate(snapshot)
        self.action('Expand', name='文件(F)', label='open-file-menu')
        self.invoke('CordisConfigurationFile', 'open-configuration')
        snapshot = self.wait(lambda s: ui.config_control(s, 'CordisConfigEditor')['enabled'], 'Configuration page missing')
        path_control = ui.config_control(snapshot, 'CordisConfigFilePath', self.registration['configurationFile'])
        require(not path_control.get('nameTruncated') and same_path(path_control['name'], self.registration['configurationFile']),
                'Actual UI configuration file differs')
        self.unchanged_c0()
        initial_text = ui.config_control(snapshot, 'CordisConfigEditor')['value']
        require(initial_text == Path(self.registration['configurationFile']).read_text(encoding='utf-8-sig'), 'Initial editor is not C0 bytes')
        self.editor_text = json.dumps(self.registration['wantedValue'], ensure_ascii=False, indent=2)
        editor = ui.config_control(snapshot, 'CordisConfigEditor', initial_text)
        selector = {'id': editor['id'], 'scope': 'CordisConfigurationPage', 'type': 'ControlType.Edit',
                    'emptyId': not editor['id'], 'matchValue': True, 'expectedValue': initial_text}
        self.action('SetValue', value=self.editor_text, selector=selector, label='edit-wanted')
        self.wait(lambda s: ui.config_control(s, 'CordisConfigEditor', self.editor_text), 'Wanted edit was not observed')
        self.invoke('CordisConfigPreview', 'preview-wanted')
        self.wait(lambda s: ui.control(s, 'CordisConfigApply')['enabled'], 'Preview did not enable Apply')
        self.invoke('CordisConfigApply', 'apply-wanted')
        def applied(s):
            facts = target_facts(s, self.registration['wantedValue'], self.editor_text)
            require(ui.text(s, 'CordisConfigSaveStatus') == '磁盘：未保存', 'Accepted target is not unsaved')
            return facts
        snapshot = self.wait(applied, 'Wanted target did not apply'); self.unchanged_c0()
        accepted = applied(snapshot); self.publish(2, accepted)
        protected = self.wait_phase(3)
        state_path, state = protection_proof(protected, self.registration)
        self.unchanged_c0()
        before_save = self.snapshot()
        target_facts(before_save, self.registration['wantedValue'], self.editor_text, accepted['revision'], accepted['entries'])
        require(ui.text(before_save, 'CordisConfigSaveStatus') == '磁盘：未保存', 'Pre-failure Save state changed')
        self.invoke('CordisConfigSave', 'save-denied-once')
        predicate = lambda s: failed_save_facts(s, self.registration['wantedValue'], self.editor_text,
                                               accepted['revision'], accepted['entries'])
        snapshot = self.wait(predicate, 'Save did not establish the expected file failure'); self.unchanged_c0()
        self.publish(4, predicate(snapshot))
        restored = self.wait_phase(5)
        restoration_proof(restored, protected, self.registration, state_path, state)
        self.unchanged_c0()
        predicate(self.snapshot())
        self.invoke('CordisConfigSave', 'save-restored-once')
        def saved(s):
            facts = target_facts(s, self.registration['wantedValue'], self.editor_text, accepted['revision'], accepted['entries'])
            require(ui.config_saved(s, accepted['revision']), 'Current accepted revision is not saved')
            return facts
        snapshot = self.wait(saved, 'Same revision did not save after restoration')
        require(json.loads(Path(self.registration['configurationFile']).read_text(encoding='utf-8-sig')) == self.registration['wantedValue'],
                'Actual saved file does not contain the accepted target')
        self.publish(6, {**saved(snapshot), 'savedFileSha256': sha(self.registration['configurationFile'])})
        self.action('Close', suffix='CordisContributionDialog', name='配置管理', label='close-configuration')
        self.action('Close', suffix='ElaWindow', label='close-main-window')
        self.publish(7, {'normalCloseRequested': True, 'targetKilled': False, 'exitEvidenceOwner': 'record-readonly-save.ps1'}, snapshot=False)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--registration', type=Path, required=True)
    parser.add_argument('--expected-registration-sha256', required=True)
    args = parser.parse_args(argv)
    driver = None
    try:
        require(os.name == 'nt', 'D10 UI execution requires Windows')
        driver = ReadOnlySaveUi(args.registration, args.expected_registration_sha256)
        driver.run()
        ui.write_new(driver.output / 'summary.json', {'schema': 'cordis.preview.readonly-save.ui/1',
                     'outcome': 'UICompletedNormalCloseRequested', 'targetKilled': False, 'productExitClaimed': False})
        return 0
    except Exception as error:
        if driver:
            ui.write_new(driver.output / 'summary.json', {'schema': 'cordis.preview.readonly-save.ui/1',
                         'outcome': 'Unknown', 'error': str(error), 'targetKilled': False, 'inputReplayAllowed': False})
        print('D10 readonly UI unknown: ' + str(error))
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
