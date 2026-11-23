"""Pure temporary-file/mocked tests: no OS UI, product, ACL or process execution."""
import copy
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
import preview_readonly_ui as adapter


def write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False), encoding='utf-8')
    return adapter.sha(path)


def node(index, parent, kind, name='', identifier='', rect=None, **extra):
    return {'index': index, 'parentIndex': parent, 'id': identifier, 'type': kind, 'name': name,
            'value': None, 'class': '', 'rect': rect, 'offscreen': False, 'enabled': True,
            'patterns': [], **extra}


def snapshot(status='磁盘：保存失败', revision=7, instance='12', title='D10-R1'):
    value = adapter.ui.config_value(title)
    editor = json.dumps(value, ensure_ascii=False, indent=2)
    result = {'errors': [], 'truncated': False, 'elements': [
        node(0, None, 'ControlType.Group', identifier='CordisConfigurationPage'),
        node(1, 0, 'ControlType.Text', f'运行：已应用 · R{revision}', 'CordisConfigSummary'),
        node(2, 0, 'ControlType.Text', '编辑：已应用', 'CordisConfigEditStatus'),
        node(3, 0, 'ControlType.Edit', identifier='CordisConfigEditor', value=editor),
        node(4, 0, 'ControlType.Text', status, 'CordisConfigSaveStatus'),
        node(5, 0, 'ControlType.Edit', identifier='CordisConfigDetails', value='configuration write lock is unavailable'),
        node(6, 0, 'ControlType.Button', '保存', 'CordisConfigSave'),
        node(7, 0, 'ControlType.Table', rect={'x': 10, 'y': 10, 'width': 400, 'height': 120},
             **{'class': 'ElaTableView', 'patterns': ['GridPatternIdentifiers.Pattern', 'TablePatternIdentifiers.Pattern']}),
    ]}
    for column, (header, cell) in enumerate(zip(['条目', '期望', '运行', '实例'], ['notes', '启用', '运行中', instance])):
        result['elements'].append(node(8 + column, 7, 'ControlType.Header', header,
                                      rect={'x': 10 + column * 100, 'y': 10, 'width': 100, 'height': 30}))
        result['elements'].append(node(12 + column, 7, 'ControlType.DataItem', cell,
                                      rect={'x': 10 + column * 100, 'y': 40, 'width': 100, 'height': 30}))
    return result, value, editor


class RegistrationFixture:
    def __init__(self, root):
        root = root.resolve()
        self.scope = root / 'scope'; self.evidence = self.scope / 'evidence'
        self.package = self.scope / 'permissions/acl/application'
        self.config = self.scope / 'permissions/acl/data/session.yaml'
        self.producer = root / 'original/verification/preview-acl.ps1'
        self.producer.parent.mkdir(parents=True); self.producer.write_bytes(b'frozen producer')
        producer_sha = adapter.sha(self.producer)
        manifest_sha = write(self.package / 'build-manifest.json', {'files': [
            {'path': 'verification/preview-acl.ps1', 'sha256': producer_sha}]})
        self.acl_path = self.scope / 'permissions/acl-registration.json'
        acl_sha = write(self.acl_path, {'schema': 'cordis.preview.acl-registration/1',
            'scopeParent': str(self.scope / 'permissions'), 'createdBy': {'path': str(self.producer), 'sha256': producer_sha}})
        self.baseline = adapter.ui.config_value('D10-C0'); baseline_sha = write(self.config, self.baseline)
        descriptor = {'pid': 42, 'creationFileTime': '123456789', 'path': str(self.package / 'cordis_config_workbench.exe'),
                      'sessionId': 2, 'ownerSid': 'S-1-5-21-1001', 'parentPid': 41}
        self.registration = {'schema': 'cordis.preview.readonly-save.registration/1', 'nonce': 'a' * 32,
            'scopeRoot': str(self.scope), 'evidenceRoot': str(self.evidence), 'packageRoot': str(self.package),
            'configurationFile': str(self.config), 'manifestSha256': manifest_sha, 'qaSid': descriptor['ownerSid'], 'sessionId': 2,
            'target': descriptor, 'wrapper': {**descriptor, 'pid': 41, 'parentPid': 40, 'path': str(root / 'powershell.exe')},
            'baselineValue': self.baseline, 'wantedValue': adapter.ui.config_value('D10-R1'), 'baselineSha256': baseline_sha,
            'aclRegistrationPath': str(self.acl_path), 'aclRegistrationSha256': acl_sha,
            'aclProducerPath': str(self.producer), 'aclProducerSha256': producer_sha, 'tools': []}
        self.path = self.evidence / 'registration.json'
        self.digest = write(self.path, self.registration)

    def validate(self, registration=None):
        value = registration or self.registration
        digest = write(self.path, value)
        adapter.validate_registration(value, self.path, digest)

    def acl_record(self, action, probe=None, exit_code=0):
        base = self.evidence / action
        report = {'schema': 'cordis.preview.acl-observation/1', 'action': action, 'target': str(self.config.parent),
                  'tokenSid': self.registration['qaSid'], 'registrationSha256': self.registration['aclRegistrationSha256'],
                  'passed': exit_code == 0, 'statePath': str(self.state_path)}
        if probe is not None:
            report['probe'] = probe
        command_path = base.with_suffix('.command.json')
        command_sha = write(command_path, {'started': True, 'pid': 100, 'command': ['powershell.exe', '-File', str(self.producer),
            '-Action', action, '-Registration', str(self.acl_path), '-Target', 'data']})
        report_path = self.scope / 'permissions' / (action + '-report.json')
        report_sha = write(report_path, report)
        stdout_path, stderr_path = base.with_suffix('.stdout'), base.with_suffix('.stderr')
        stdout_path.write_bytes(json.dumps(report).encode()); stderr_path.write_bytes(b'')
        return {'exitCode': exit_code, 'commandPath': str(command_path), 'commandSha256': command_sha,
                'stdoutPath': str(stdout_path), 'stdoutSha256': adapter.sha(stdout_path),
                'stderrPath': str(stderr_path), 'stderrSha256': adapter.sha(stderr_path),
                'reportPath': str(report_path), 'reportSha256': report_sha, 'report': report,
                'processTerminated': False, 'timedOut': False,
                'logs': [{'eof': True, 'truncated': False, 'seen': path.stat().st_size, 'saved': path.stat().st_size}
                         for path in (stdout_path, stderr_path)]}

    def proofs(self, write_error=5, restore_exit=1):
        self.state_path = self.scope / 'permissions/data-state.json'
        state = {'schema': 'cordis.preview.acl-state/1', 'target': str(self.config.parent),
                 'registrationSha256': self.registration['aclRegistrationSha256'], 'tokenSid': self.registration['qaSid'],
                 'objects': [{'relativePath': '.', 'sddl': 'O:SYG:SYD:P'}]}
        state_sha = write(self.state_path, state)
        probe = {'passed': True, 'createNewDenied': True, 'writeDataError': write_error, 'deleteAccessError': 5,
                 'deleteProbeUnlinksFile': False, 'existingFile': str(self.config)}
        protected = {'effectiveProtectionVerified': True, 'baselineSha256': self.registration['baselineSha256'],
                     'stateSha256': state_sha, 'protect': self.acl_record('Protect', probe), 'probe': self.acl_record('Probe', probe)}
        restored = {'restorationVerified': True, 'baselineSha256': self.registration['baselineSha256'], 'stateSha256': state_sha,
                    'originalRestore': self.acl_record('Restore', exit_code=restore_exit),
                    'semanticComparison': {'passed': True, 'identityVerified': True,
                        'rule': 'OwnerGroupOrderedDaclBinaryAllFlagsExceptDaclAutoInherited/1', 'allowedControlFlagsDifference': 0x400,
                        'writeDataError': 0, 'deleteAccessError': 0, 'filesWrittenOrDeleted': False, 'mismatches': [],
                        'stateSha256': state_sha, 'objectCount': 1,
                        'objects': [{'relativePath': '.', 'identityVerified': True, 'expectedSddl': 'O:SYG:SYD:P', 'comparison': {'passed': True}}]}}
        return protected, restored, state


class ReadOnlyUiValues(unittest.TestCase):
    def test_actual_anonymous_table_reads_four_cell_names(self):
        value, _, _ = snapshot()
        self.assertEqual(adapter.configuration_entries(value), ['notes', '启用', '运行中', '12'])

    def test_incomplete_ambiguous_or_foreign_table_is_unknown(self):
        for change in ('duplicate-table', 'missing-cell', 'wrong-header', 'outside-parent', 'truncated', 'offscreen', 'outside-bounds'):
            value, _, _ = snapshot()
            if change == 'duplicate-table': value['elements'].append({**value['elements'][7], 'index': 99})
            elif change == 'missing-cell': value['elements'].pop()
            elif change == 'wrong-header': value['elements'][8]['name'] = 'fake'
            elif change == 'outside-parent': value['elements'][-1]['parentIndex'] = 0
            elif change == 'truncated': value['elements'][-1]['nameTruncated'] = True
            elif change == 'offscreen': value['elements'][-1]['offscreen'] = True
            else: value['elements'][-1]['rect']['x'] = 900
            with self.subTest(change=change), self.assertRaises(adapter.Unknown): adapter.configuration_entries(value)

    def test_failure_requires_exact_disk_failure_and_actual_message(self):
        value, wanted, editor = snapshot()
        self.assertEqual(adapter.failed_save_facts(value, wanted, editor, 7, ['notes', '启用', '运行中', '12'])['revision'], 7)
        for state in ('磁盘：保存冲突', '磁盘：保存中', '磁盘：未保存', '磁盘：已保存 R7'):
            value['elements'][4]['name'] = state
            with self.subTest(state=state), self.assertRaises(adapter.Unknown):
                adapter.failed_save_facts(value, wanted, editor, 7, ['notes', '启用', '运行中', '12'])
        value['elements'][4]['name'] = '磁盘：保存失败'; value['elements'][5]['value'] = '保存已排队。'
        with self.assertRaises(adapter.Unknown): adapter.failed_save_facts(value, wanted, editor, 7, ['notes', '启用', '运行中', '12'])

    def test_failure_cannot_hide_changed_revision_editor_or_instance(self):
        for change in ('revision', 'editor', 'instance', 'applied'):
            value, wanted, editor = snapshot()
            if change == 'revision': value['elements'][1]['name'] = '运行：已应用 · R8'
            elif change == 'editor': value['elements'][3]['value'] = json.dumps(adapter.ui.config_value('other'))
            elif change == 'instance': value['elements'][-1]['name'] = '13'
            else: value['elements'][2]['name'] = '编辑：尚未应用'
            with self.subTest(change=change), self.assertRaises(adapter.Unknown):
                adapter.failed_save_facts(value, wanted, editor, 7, ['notes', '启用', '运行中', '12'])

    def test_saved_status_must_name_same_positive_revision(self):
        value, wanted, editor = snapshot(status='磁盘：已保存 R6')
        adapter.target_facts(value, wanted, editor, 7, ['notes', '启用', '运行中', '12'])
        self.assertFalse(adapter.ui.config_saved(value, 7))
        value['elements'][1]['name'] = '运行：已应用 · R0'
        with self.assertRaises(adapter.Unknown): adapter.target_facts(value, wanted, editor)

    def test_registration_rejects_legacy_wrong_pid_path_and_value(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = RegistrationFixture(Path(directory)); fixture.validate()
            for name in ('schema', 'pid', 'parent', 'session', 'file', 'same-value', 'producer'):
                value = copy.deepcopy(fixture.registration)
                if name == 'schema': value['schema'] = 'cordis.preview.desktop.registration/1'
                elif name == 'pid': value['target']['pid'] = True
                elif name == 'parent': value['target']['parentPid'] = 99
                elif name == 'session': value['target']['sessionId'] = 3
                elif name == 'file': value['configurationFile'] = str(Path(directory) / 'other.yaml')
                elif name == 'same-value': value['wantedValue'] = value['baselineValue']
                else: value['aclProducerSha256'] = '0' * 64
                with self.subTest(name=name), self.assertRaises(adapter.Unknown): fixture.validate(value)

    def test_registration_external_hash_is_not_self_redefined(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = RegistrationFixture(Path(directory)); fixture.path.write_bytes(fixture.path.read_bytes() + b' ')
            with self.assertRaises(adapter.Unknown): adapter.validate_registration(fixture.registration, fixture.path, fixture.digest)

    def test_protection_needs_original_denial5_not_owner_boolean(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = RegistrationFixture(Path(directory)); good, _, _ = fixture.proofs()
            adapter.protection_proof(good, fixture.registration)
            bad, _, _ = fixture.proofs(write_error=0)
            with self.assertRaises(adapter.Unknown): adapter.protection_proof(bad, fixture.registration)

    def test_restore_preserves_original_false_and_requires_identity_access_and_narrow_comparison(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = RegistrationFixture(Path(directory)); protected, restored, state = fixture.proofs()
            adapter.restoration_proof(restored, protected, fixture.registration, fixture.state_path, state)
            self.assertEqual(restored['originalRestore']['exitCode'], 1)
            for field, replacement in [('identityVerified', False), ('writeDataError', 5), ('allowedControlFlagsDifference', 0x401),
                                       ('mismatches', ['bad']), ('filesWrittenOrDeleted', True), ('objectCount', 2)]:
                changed = copy.deepcopy(restored); changed['semanticComparison'][field] = replacement
                with self.subTest(field=field), self.assertRaises(adapter.Unknown):
                    adapter.restoration_proof(changed, protected, fixture.registration, fixture.state_path, state)

    def test_original_acl_record_hash_is_not_replaceable_by_parsed_report(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = RegistrationFixture(Path(directory)); protected, _, _ = fixture.proofs()
            Path(protected['probe']['reportPath']).write_bytes(b'{}')
            with self.assertRaises(adapter.Unknown): adapter.protection_proof(protected, fixture.registration)

    def test_same_pid_creation_reuse_and_wrong_actor_caller_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            registration = RegistrationFixture(Path(directory)).registration
            raw = {'identityVerified': True, 'actualIdentity': {key: copy.deepcopy(registration[key]) for key in ('target', 'wrapper')},
                   'actor': {'pid': 90, 'parentPid': 89, 'ownerSid': registration['qaSid'], 'sessionId': 2,
                             'administrator': False, 'integrityRids': [8192], 'interactive': True, 'is64Bit': True,
                             'caller': {'pid': 89, 'parentPid': 41, 'ownerSid': registration['qaSid'], 'sessionId': 2, 'path': sys.executable}}}
            adapter.validate_identity_reply(raw, registration, 89)
            for change in ('legacy-helper', 'reused-pid', 'administrator', 'caller-parent', 'caller-path'):
                bad = copy.deepcopy(raw)
                if change == 'legacy-helper': del bad['identityVerified']
                elif change == 'reused-pid': bad['actualIdentity']['target']['creationFileTime'] = '987654321'
                elif change == 'administrator': bad['actor']['administrator'] = True
                elif change == 'caller-parent': bad['actor']['caller']['parentPid'] = 40
                else: bad['actor']['caller']['path'] = str(Path(directory) / 'other-python.exe')
                with self.subTest(change=change), self.assertRaises(adapter.Unknown): adapter.validate_identity_reply(bad, registration, 89)

    def test_reads_retry_without_replaying_mutations(self):
        driver = adapter.ReadOnlySaveUi.__new__(adapter.ReadOnlySaveUi)
        driver.action = mock.Mock(side_effect=[adapter.Unknown('provider replaced tree'), {'snapshot': 'read'}])
        driver.record = mock.Mock()
        with mock.patch.object(adapter.time, 'sleep'): self.assertEqual(driver.snapshot(), {'snapshot': 'read'})
        self.assertEqual(driver.action.call_args_list, [mock.call('Read'), mock.call('Read')])
        driver.action = mock.Mock(side_effect=adapter.Unknown('unavailable'))
        with mock.patch.object(adapter.time, 'sleep'), self.assertRaises(adapter.Unknown): driver.snapshot()
        self.assertEqual(driver.action.call_args_list, [mock.call('Read')] * 3)

    def test_unknown_mutation_consumes_fixed_intent_and_cannot_be_replayed(self):
        with tempfile.TemporaryDirectory() as directory:
            driver = adapter.ReadOnlySaveUi.__new__(adapter.ReadOnlySaveUi)
            driver.output = Path(directory); driver.intents = Path(directory) / 'intents'; driver.intents.mkdir()
            driver.registration = {'nonce': 'a' * 32, 'target': {'pid': 42}, 'wrapper': {'pid': 41}}
            driver.expected_sha = 'b' * 64; driver.required_phase = 3; driver.sequence = 0
            driver.deadline = time.monotonic() + 30; driver.context = mock.Mock(); driver.input = mock.Mock()
            driver.input.wait_guard.return_value = {'foregroundPid': 42, 'focusPid': 42}
            with mock.patch.object(adapter.subprocess, 'run', side_effect=subprocess.TimeoutExpired('owned-action', 12)) as action:
                with self.assertRaises(subprocess.TimeoutExpired): driver.invoke('CordisConfigSave', 'save-denied-once')
                with self.assertRaises(FileExistsError): driver.invoke('CordisConfigSave', 'save-denied-once')
                self.assertEqual(action.call_count, 1)
                self.assertEqual(action.call_args.kwargs['creationflags'], getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            driver.input.wait_guard.assert_called_once()
            driver.input.activate.assert_not_called()
            request = next(Path(directory).glob('*Invoke-request.json'))
            raw = json.loads(request.read_text())
            self.assertEqual(raw['expectedIdentity'], {'target': {'pid': 42}, 'wrapper': {'pid': 41}})
            self.assertEqual(raw['callerPid'], adapter.os.getpid())

    def test_transient_focus_guard_retries_reads_without_activation_or_input(self):
        native = adapter.ui.NativeInput.__new__(adapter.ui.NativeInput)
        native.guard = mock.Mock(side_effect=[adapter.Unknown('focus temporarily zero'), {'foregroundPid': 42, 'focusPid': 42}])
        native.activate = mock.Mock(); native.keys = mock.Mock(); native.click = mock.Mock()
        with mock.patch.object(adapter.ui.time, 'sleep'):
            self.assertEqual(native.wait_guard(), {'foregroundPid': 42, 'focusPid': 42})
        self.assertEqual(native.guard.call_count, 2)
        native.activate.assert_not_called(); native.keys.assert_not_called(); native.click.assert_not_called()

    def test_second_adapter_attempt_and_owner_abort_never_authorize_input(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = RegistrationFixture(Path(directory))
            driver = adapter.ReadOnlySaveUi(fixture.path, fixture.digest)
            with self.assertRaises(FileExistsError): adapter.ReadOnlySaveUi(fixture.path, fixture.digest)
            control = fixture.evidence / 'control'; control.mkdir()
            write(control / 'owner-aborted.json', {'reason': 'stop'})
            with mock.patch.object(adapter.os, 'getppid', return_value=41), self.assertRaises(adapter.Unknown): driver.validate()

    def test_phase_chain_rejects_nonce_pid_creation_predecessor_and_future_phase(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = RegistrationFixture(Path(directory)); driver = adapter.ReadOnlySaveUi(fixture.path, fixture.digest)
            driver.control.mkdir(); driver.validate = mock.Mock()
            first = {'schema': 'cordis.preview.readonly-save.phase/1', 'nonce': fixture.registration['nonce'],
                     'registrationSha256': fixture.digest, 'phase': 'owner-ready', 'previousSha256': fixture.digest,
                     'utc': '2026-11-21T12:00:00Z', 'pid': 42, 'creationFileTime': '123456789', 'facts': {}}
            path = driver.control / '01-owner-ready.json'; write(path, first)
            self.assertEqual(len(driver.chain(1)), 1)
            for field, replacement in [('nonce', 'b' * 32), ('pid', 43), ('creationFileTime', '987654321'),
                                       ('previousSha256', '0' * 64), ('schema', 'cordis.preview.desktop.phase/1')]:
                changed = {**first, field: replacement}; write(path, changed)
                with self.subTest(field=field), self.assertRaises(adapter.Unknown): driver.chain(1)
            write(path, first)
            write(driver.control / '03-owner-protected.json', {**first, 'phase': 'owner-protected'})
            with self.assertRaises(adapter.Unknown): driver.chain(3)

    def test_phase_file_cannot_be_replaced_even_with_same_identity_fields(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = RegistrationFixture(Path(directory)); driver = adapter.ReadOnlySaveUi(fixture.path, fixture.digest)
            driver.control.mkdir(); driver.validate = mock.Mock()
            value = {'schema': 'cordis.preview.readonly-save.phase/1', 'nonce': fixture.registration['nonce'],
                     'registrationSha256': fixture.digest, 'phase': 'owner-ready', 'previousSha256': fixture.digest,
                     'utc': '2026-11-21T12:00:00Z', 'pid': 42, 'creationFileTime': '123456789', 'facts': {}}
            path = driver.control / '01-owner-ready.json'; write(path, value); driver.chain(1)
            value['facts'] = {'substituted': True}; write(path, value)
            with self.assertRaises(adapter.Unknown): driver.chain(1)

    def test_unknown_control_file_is_not_a_new_authorized_stage(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = RegistrationFixture(Path(directory)); driver = adapter.ReadOnlySaveUi(fixture.path, fixture.digest)
            driver.control.mkdir(); driver.validate = mock.Mock()
            write(driver.control / '08-retry-save.json', {})
            with self.assertRaises(adapter.Unknown): driver.chain(1)

    def test_tools_require_exact_runtime_closure_and_current_bytes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            paths = [root / name for name in ('tools/preview_readonly_ui.py', 'tools/preview_ui.py', 'tools/preview_ui_action.ps1',
                     'python/python.exe', 'delivery/windows/verification/preview-ui.psm1',
                     'delivery/windows/verification/preview-windows.psm1')]
            for path in paths:
                path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(b'anchored tool')
            registration = {'tools': [{'path': str(path), 'bytes': path.stat().st_size, 'sha256': adapter.sha(path)} for path in paths]}
            with mock.patch.object(adapter, '__file__', str(paths[0])), mock.patch.object(adapter.ui, '__file__', str(paths[1])), \
                    mock.patch.object(adapter, 'ACTION', paths[2]), mock.patch.object(adapter.sys, 'executable', str(paths[3])):
                adapter.verify_tools(registration)
                absent = {'tools': registration['tools'][:-1]}
                with self.assertRaises(adapter.Unknown): adapter.verify_tools(absent)
                paths[1].write_bytes(b'changed tool')
                with self.assertRaises(adapter.Unknown): adapter.verify_tools(registration)

    def test_first_save_unknown_stops_sequence_without_reapply_rebase_retry_or_close(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = RegistrationFixture(Path(directory))
            driver = adapter.ReadOnlySaveUi.__new__(adapter.ReadOnlySaveUi)
            driver.registration = fixture.registration
            initial, _, _ = snapshot(title='D10-C0')
            initial['elements'][3]['value'] = fixture.config.read_text(encoding='utf-8')
            initial['elements'].append(node(100, 0, 'ControlType.Text', str(fixture.config), 'CordisConfigFilePath'))
            accepted, _, _ = snapshot(status='磁盘：未保存')
            ready = {'token': {'qaSid': fixture.registration['qaSid'], 'sessionId': 2,
                              'administrator': False, 'integrityRid': 8192}, 'baselineSha256': fixture.registration['baselineSha256']}
            driver.wait_phase = mock.Mock(side_effect=[ready, {}])
            driver.snapshot = mock.Mock(side_effect=[initial, accepted])
            driver.record = mock.Mock(); driver.reserve = mock.Mock(); driver.action = mock.Mock()
            driver.wait = mock.Mock(side_effect=[initial, accepted, accepted, accepted])
            driver.publish = mock.Mock(); driver.unchanged_c0 = mock.Mock()
            calls = []
            def invoke(suffix, label):
                calls.append(suffix)
                if suffix == 'CordisConfigSave': raise adapter.Unknown('unknown mutation outcome')
            driver.invoke = invoke
            with mock.patch.object(adapter.ui, 'NativeInput'), mock.patch.object(adapter, 'protection_proof', return_value=(Path(directory), {})), \
                    self.assertRaises(adapter.Unknown): driver.run()
            self.assertEqual(calls, ['CordisConfigurationFile', 'CordisConfigPreview', 'CordisConfigApply', 'CordisConfigSave'])
            self.assertFalse(any(call.args[0] == 'Close' for call in driver.action.call_args_list))
            self.assertEqual(driver.publish.call_count, 1)


if __name__ == '__main__':
    unittest.main()
