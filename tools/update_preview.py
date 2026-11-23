"""Finite offline app/data deployment for ADR-0059; never qualify or repair a plugin.

Only closed candidate copies are transformed. Real Session/Store validation is an
externally reviewed input, never a result manufactured by this tool.
"""
import argparse
import base64
import copy
import ctypes
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
import threading
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
MAX_JSON = 8 * 1024 * 1024
MAX_FILES, MAX_BYTES = 10000, 1024 * 1024 * 1024
ADOPTION = 'qualification/native-business-p6-3-20261123.json'
ALIASES = ('counter', 'reporter', 'notebook')
CONFIG = 'configuration/cordis.yaml'
BUSINESS = 'business/session.yaml'
VERIFIER_SHA = '321a0aa891390d5c57a88aa1d01cadf121c511a1970b9c6d8ee776ce526d9177'
WINDOWS_SHA = '37c5d3d27c8a6473bdfaa59f635e5427b050b80e633f6978a259b7dc61d48cd2'


class Refused(RuntimeError):
    """The exact prerequisite is unproved; retain original/partial files."""


def need(condition, message):
    if not condition:
        raise Refused(message)


def digest(path):
    result = hashlib.sha256()
    with Path(path).open('rb') as stream:
        while block := stream.read(1024 * 1024):
            result.update(block)
    return result.hexdigest()


def hex64(value):
    return isinstance(value, str) and re.fullmatch('[0-9a-f]{64}', value) is not None


def relative(value):
    need(isinstance(value, str) and value and '\\' not in value and ':' not in value, 'Unsafe relative path')
    path = PurePosixPath(value)
    need(not path.is_absolute() and str(path) == value, 'Noncanonical relative path')
    for part in path.parts:
        need(part not in ('.', '..') and part[-1] not in ' .'
             and not any(ord(char) < 32 or ord(char) == 127 or char in '<>"|?*' for char in part)
             and not re.fullmatch(r'(?i)(CON|PRN|AUX|NUL|COM[1-9\u00b9\u00b2\u00b3]|LPT[1-9\u00b9\u00b2\u00b3])(?:\..*)?', part), 'Unsafe path component')
    return value


def safe(path):
    path = Path(path)
    need(path.is_absolute() and '..' not in path.parts and not str(path).startswith(('\\\\', '\\?\\')), 'Local absolute path required')
    for item in (path, *path.parents):
        if item.exists() or item.is_symlink():
            info = item.lstat()
            need(not item.is_symlink() and not getattr(info, 'st_file_attributes', 0) & 0x400, 'Reparse path refused')
            if item.is_file():
                need(info.st_nlink == 1, 'Hard-linked file refused')
    return path.resolve()


def child(root, name):
    root = safe(root); target = safe(root / relative(name))
    need(target != root and root in target.parents, 'Path escaped owned root')
    return target


def disjoint(left, right):
    left, right = safe(left), safe(right)
    need(left != right and left not in right.parents and right not in left.parents, 'Input/output roots overlap')


def _pairs(pairs):
    result = {}
    for key, value in pairs:
        need(key not in result, 'Duplicate JSON key')
        result[key] = value
    return result


def parse(raw):
    need(len(raw) <= MAX_JSON, 'JSON budget exceeded')
    value = json.loads(raw.decode('utf-8-sig'), object_pairs_hook=_pairs,
                       parse_constant=lambda _: (_ for _ in ()).throw(Refused('Nonfinite JSON number')))
    def depth(item, level=0):
        need(level <= 32, 'JSON depth exceeded')
        for nested in item.values() if isinstance(item, dict) else item if isinstance(item, list) else ():
            depth(nested, level + 1)
    depth(value)
    return value


def read(path, expected=None):
    path = safe(path); raw = path.read_bytes()
    if expected is not None:
        need(hex64(expected) and hashlib.sha256(raw).hexdigest() == expected, 'External JSON anchor mismatch')
    return parse(raw)


def encoded(value):
    return (json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(',', ':'), allow_nan=False) + '\n').encode('utf-8')


def value_sha(value):
    return hashlib.sha256(encoded(value)).hexdigest()


def write_new(path, value):
    path = safe(path); path.parent.mkdir(parents=True, exist_ok=True); safe(path.parent)
    with path.open('xb') as stream:
        stream.write(encoded(value)); stream.flush(); os.fsync(stream.fileno())


def snapshot(root):
    root = safe(root); need(root.is_dir(), 'Data/package root is missing')
    files, directories, seen, total = {}, [], set(), 0
    pending = [root]
    while pending:
        directory = pending.pop()
        for path in sorted(directory.iterdir()):
            safe(path); name = relative(path.relative_to(root).as_posix())
            need(name.casefold() not in seen, 'Case-colliding tree entry'); seen.add(name.casefold())
            need(len(seen) <= MAX_FILES, 'Tree entry budget exceeded')
            if path.is_dir():
                directories.append(name); pending.append(path)
            else:
                need(path.is_file(), 'Non-regular tree input')
                before = path.stat(); total += before.st_size
                need(len(files) < MAX_FILES and total <= MAX_BYTES, 'Tree budget exceeded')
                checksum = digest(path); after = path.stat()
                need((before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns)
                     == (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns), 'Input changed while hashed')
                files[name] = {'bytes': after.st_size, 'sha256': checksum}
    return {'files': files, 'directories': sorted(directories)}


def copy_tree(source, destination, expected):
    source, destination = safe(source), safe(destination)
    disjoint(source, destination)
    need(snapshot(source) == expected and not destination.exists(), 'Copy precondition differs')
    destination.mkdir(parents=True, exist_ok=False)
    for name in expected['directories']:
        child(destination, name).mkdir(parents=True, exist_ok=False)
    for name, record in expected['files'].items():
        origin, target = child(source, name), child(destination, name)
        target.parent.mkdir(parents=True, exist_ok=True)
        with origin.open('rb') as reader, target.open('xb') as writer:
            while block := reader.read(1024 * 1024):
                writer.write(block)
            writer.flush(); os.fsync(writer.fileno())
        need(digest(target) == record['sha256'], 'Copied bytes differ')
    need(snapshot(source) == snapshot(destination) == expected, 'Copy source/set changed')


class Lease:
    """One OS-owned exclusive lease: v1 serializes managed launches and updates."""
    def __init__(self, path):
        self.path = safe(path); self.handle = None

    def __enter__(self):
        if os.name == 'nt':
            self.kernel = ctypes.WinDLL('kernel32', use_last_error=True)
            self.kernel.CreateFileW.argtypes = [ctypes.c_wchar_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p,
                                                ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p]
            self.kernel.CreateFileW.restype = ctypes.c_void_p
            self.kernel.CloseHandle.argtypes = [ctypes.c_void_p]
            self.handle = self.kernel.CreateFileW(str(self.path), 0xC0000000, 0, None, 3, 0x80, None)
            need(self.handle not in (None, ctypes.c_void_p(-1).value), 'Controller lease is held/unavailable')
        else:
            import fcntl
            self.handle = self.path.open('r+b')
            try:
                fcntl.flock(self.handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except OSError:
                self.handle.close(); self.handle = None; raise Refused('Controller lease is held')
        return self

    def __exit__(self, *_):
        if self.handle is not None:
            if os.name == 'nt': self.kernel.CloseHandle(self.handle)
            else: self.handle.close()
            self.handle = None


def powershell():
    return str(Path(os.environ.get('SystemRoot', r'C:\Windows')) / 'System32/WindowsPowerShell/v1.0/powershell.exe')


def stock_verify(package, expected, output):
    verifier = ROOT / 'delivery/windows/verification/verify-package.ps1'
    need(digest(safe(verifier)) == VERIFIER_SHA, 'Trusted stock verifier differs')
    output = safe(output); output.mkdir(parents=True, exist_ok=False)
    command = [powershell(), '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', str(verifier),
               '-PackageRoot', str(package), '-ExpectedManifestSha256', expected, '-ReportPath', str(output / 'report.json')]
    result = subprocess.run(command, capture_output=True, timeout=60, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    for name in ('stdout', 'stderr'):
        with (output / (name + '.raw')).open('xb') as stream: stream.write(getattr(result, name))
    write_new(output / 'command.json', {'command': command, 'exitCode': result.returncode})
    need(result.returncode == 0 and read(output / 'report.json').get('verified') is True, 'Stock package verification failed')


def quiet(root):
    need(os.name == 'nt', 'Real deployment/launch requires Windows')
    module = ROOT / 'delivery/windows/verification/preview-windows.psm1'
    need(digest(safe(module)) == WINDOWS_SHA, 'Trusted Windows observer differs')
    payload = base64.b64encode(json.dumps({'root': str(root), 'module': str(module)}).encode()).decode()
    script = "$ErrorActionPreference='Stop';[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false);" + \
        "$v=ConvertFrom-Json ([Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('" + payload + "')));" + \
        "Import-Module $v.module -Force;$t=[CordisPreview.Windows]::Token();" + \
        "$p=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent());" + \
        "if($t.elevated -or $t.integrityRid -ne 8192 -or $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator) -or -not[Environment]::UserInteractive){throw 'Ordinary interactive Medium token required.'};" + \
        "$rows=@(Get-CimInstance Win32_Process -Filter \"Name='cordis_config_workbench.exe' OR Name='cordis_platform_workbench_demo.exe'\"|ForEach-Object {$_.ProcessId});" + \
        "ConvertTo-Json -Compress -InputObject @{live=$rows;integrityRid=$t.integrityRid}"
    command = [powershell(), '-NoProfile', '-NonInteractive', '-EncodedCommand', base64.b64encode(script.encode('utf-16le')).decode()]
    result = subprocess.run(command, capture_output=True, timeout=30, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    need(result.returncode == 0, 'Cannot establish ordinary token/closed consumers')
    # An external executable can use relative data paths from a managed cwd.
    # CIM does not prove cwd, so every live Cordis host conservatively blocks v1.
    need(parse(result.stdout).get('live') == [], 'A Cordis host still runs; relative/unknown consumers are not proved closed')


def describe(pid):
    need(type(pid) is int and pid > 0, 'Positive actual process ID required')
    script = "$ErrorActionPreference='Stop';[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false);" + \
        "$p=[Diagnostics.Process]::GetProcessById(" + str(pid) + ");try{[void]$p.Handle;" + \
        "$n=Get-CimInstance Win32_Process -Filter 'ProcessId=" + str(pid) + "';" + \
        "$o=Invoke-CimMethod -InputObject $n -MethodName GetOwnerSid;if($o.ReturnValue -ne 0 -or -not$n.ExecutablePath){throw 'Identity unavailable'};" + \
        "@{pid=$p.Id;creationFileTime=$p.StartTime.ToUniversalTime().ToFileTimeUtc().ToString([Globalization.CultureInfo]::InvariantCulture);path=$n.ExecutablePath;sessionId=$p.SessionId;ownerSid=$o.Sid;parentPid=[int]$n.ParentProcessId}|ConvertTo-Json -Compress}finally{$p.Dispose()}"
    result = subprocess.run([powershell(), '-NoProfile', '-NonInteractive', '-EncodedCommand', base64.b64encode(script.encode('utf-16le')).decode()],
                            capture_output=True, timeout=20, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    need(result.returncode == 0, 'Cannot observe actual held process identity')
    return parse(result.stdout)


def package(path, expected):
    path = safe(path); manifest = read(path / 'build-manifest.json', expected); inventory = snapshot(path)
    need(manifest.get('schema') == 'cordis.preview.package/1' and manifest.get('scope') == 'internal-technical-preview'
         and manifest.get('platform') == 'windows-x64', 'Unsupported preview package')
    profile = manifest['profile']
    need(profile['id'] == 'ela-msvc-release' and profile['buildType'] == 'Release' and profile['runtime'] == 'MD'
         and profile['pointerBits'] == 64 and profile['asan'] is False, 'Only the reviewed Release profile is deployable')
    files = {}
    for row in manifest['files']:
        name = relative(row['path']); need(name.casefold() not in {key.casefold() for key in files}, 'Duplicate package path')
        need(name.casefold() not in ('build-manifest.json', 'sha256sums.txt'), 'Reserved package metadata path')
        need(type(row['bytes']) is int and row['bytes'] >= 0 and hex64(row['sha256']), 'Invalid payload digest/length')
        files[name] = {'bytes': row['bytes'], 'sha256': row['sha256']}
    need(set(inventory['files']) == set(files) | {'build-manifest.json', 'SHA256SUMS.txt'}
         and all(inventory['files'][name] == row for name, row in files.items()), 'Package inventory/bytes differ')
    need({'cordis_config_workbench.exe', 'cordis_platform_workbench_demo.exe'} <= set(files), 'Missing fixed entrypoint')
    sums = {}
    for line in (path / 'SHA256SUMS.txt').read_text('utf-8-sig').splitlines():
        match = re.fullmatch(r'([0-9a-f]{64})  (.+)', line)
        need(match and match[2] not in sums, 'Invalid SHA list'); sums[match[2]] = match[1]
    need(set(sums) == set(inventory['files']) - {'SHA256SUMS.txt'}
         and all(inventory['files'][name]['sha256'] == checksum for name, checksum in sums.items()), 'SHA list differs')
    return {'path': path, 'sha256': expected, 'manifest': manifest, 'inventory': inventory}


def artifact(pack, declaration_path):
    declaration_path = relative(declaration_path); declaration = read(child(pack['path'], declaration_path))
    library = relative(declaration['library']); need('/' not in library, 'Artifact must declare one adjacent library')
    source = child(pack['path'], str(PurePosixPath(declaration_path).parent / library))
    need(source.is_file() and digest(source) == declaration['sha256'], 'Module library differs')
    return declaration, source


def migration(old, new, old_data, mapping_path, mapping_sha, review_path, review_sha):
    mapping = read(mapping_path, mapping_sha); review = read(review_path, review_sha)
    need(mapping.get('schema') == 'cordis.preview.migration-map/1' and review.get('schema') == 'cordis.preview.migration-review/1'
         and review.get('passed') is True and review.get('mappingSha256') == mapping_sha, 'Explicit independent map review required')
    for field, expected in (('oldManifestSha256', old['sha256']), ('newManifestSha256', new['sha256']),
                            ('newQualificationSha256', new['manifest']['qualification']['sourceSha256']), ('profileId', 'ela-msvc-release')):
        need(mapping.get(field) == review.get(field) == expected, 'Map/qualification/profile binding differs')
    need(review.get('adoptionPath') == ADOPTION and new['manifest']['qualification']['nativeApprovalCount'] == 4
         and new['manifest']['qualification']['qtPolicy'] == 'resident', 'No candidate/union approval adoption')
    native_rows = []
    for alias in ('counter', 'reporter'):
        for variant in ('a', 'b'):
            declaration, _ = artifact(new, f'native-upgrade/{alias}-{variant}/module.json')
            need(declaration['loader'] == 'c-abi' and declaration['unloadPolicy'] == 'explicit', 'Native policy differs')
            native_rows.append({'moduleId': declaration['moduleId'], 'sha256': declaration['sha256']})
    need(sorted(review.get('nativeApprovals', []), key=lambda row: (row['moduleId'], row['sha256']))
         == sorted(native_rows, key=lambda row: (row['moduleId'], row['sha256'])), 'Exact four Release approval rows required')
    document = read(child(old_data, BUSINESS))
    need(type(document.get('formatVersion')) is int and document['formatVersion'] == 1
         and set(document.get('modules', {})) == set(ALIASES), 'Unsupported persisted JSON format/modules')
    transformed = copy.deepcopy(document); entries = mapping.get('entries', [])
    need(len(entries) == 3 and {row.get('alias') for row in entries} == set(ALIASES), 'Three exact module maps required')
    installed = []
    for row in entries:
        alias = row['alias']; current = row['old']; target = row['new']
        locator = relative(document['modules'][alias]['manifest']); old_declaration = read(child(old_data, 'business/store/' + locator))
        declaration, library = artifact(new, target['packageManifest'])
        for value, desc in ((current, old_declaration), (target, declaration)):
            need(value['moduleId'] == desc['moduleId'] == 'org.cordis.example.' + alias
                 and value['moduleVersion'] == desc['moduleVersion'] and value['librarySha256'] == desc['sha256']
                 and value['loader'] == desc['loader'] and value['unloadPolicy'] == desc['unloadPolicy']
                 and value['abi'] == desc['abi'] and desc['plugins'] == ['example.' + alias], 'Mapped module identity/ABI differs')
        need(current['manifestSha256'] == digest(child(old_data, 'business/store/' + locator))
             and current['locator'] == locator == current['librarySha256'] + '/module.json'
             and digest(child(old_data, 'business/store/' + str(PurePosixPath(locator).parent / old_declaration['library']))) == current['librarySha256']
             and target['manifestSha256'] == digest(child(new['path'], target['packageManifest']))
             and current['abi'] == target['abi'], 'Mapped old/new bytes differ')
        need(any(item['sha256'] == current['manifestSha256'] and item['path'].endswith('/module.json') for item in old['manifest']['files'])
             and any(item['sha256'] == current['librarySha256'] and item['path'].endswith('.dll') for item in old['manifest']['files']),
             'Old selected artifact is not bound to the old qualified package')
        old_schema = old['manifest']['versions']['moduleSchemas']['example.' + alias]
        new_schema = new['manifest']['versions']['moduleSchemas']['example.' + alias]
        need(row['oldStateSchema'] == old_schema == new_schema == row['newStateSchema']
             and old['manifest']['versions']['configurationFormat'] == new['manifest']['versions']['configurationFormat'] == 1
             and old['manifest']['profile']['sdkVersion'] == new['manifest']['profile']['sdkVersion'], 'A schema/SDK transform is not implemented')
        need((alias == 'notebook' and declaration['loader'] == 'qt-ui' and declaration['unloadPolicy'] == 'resident')
             or (alias != 'notebook' and {'moduleId': declaration['moduleId'], 'sha256': declaration['sha256']} in native_rows), 'Mapped admission differs')
        transformed['modules'][alias]['manifest'] = declaration['sha256'] + '/module.json'
        installed.append({'alias': alias, 'manifest': target['packageManifest'], 'library': library,
                          'sha256': declaration['sha256'], 'libraryName': library.name})
    return transformed, installed


def _checkpoint(name):
    """Owned file-stage boundary for tests/externally reviewed interruption capture.

    A capture harness may stop only this updater here after all products exited;
    no CLI fault flag, runtime injection or qualification/approval bypass exists.
    """


class Updater:
    def __init__(self, root):
        self.root = safe(root)

    def path(self, name):
        return child(self.root, name)

    def check_selection(self, value):
        need(value.get('schema') == 'cordis.preview.selection/1' and hex64(value['manifestSha256'])
             and all(isinstance(value.get(key), str) and re.fullmatch('[0-9a-f]{32}', value[key]) for key in ('selectionId', 'transactionId'))
             and (value.get('previousSha256') is None or hex64(value['previousSha256'])), 'Invalid active selection')
        self.path(value['release']); self.path(value['data'])
        need(re.fullmatch('releases/[0-9a-f]{32}', value['release']) and re.fullmatch('data/[0-9a-f]{32}', value['data']),
             'Selection is outside managed slots')
        return value

    def selector(self):
        return self.check_selection(read(self.path('active.json')))

    def transaction(self, identifier):
        need(isinstance(identifier, str) and re.fullmatch('[0-9a-f]{32}', identifier), 'Invalid transaction ID')
        tx = self.path('transactions/' + identifier)
        plan = read(tx / '00-plan.json'); prepared = read(tx / '10-prepared.json')
        self.check_selection(plan['oldSelection']); self.check_selection(prepared['selection'])
        need(plan['transactionId'] == identifier == prepared['selection']['transactionId']
             and hex64(plan['oldSelectionSha256']) and value_sha(plan['oldSelection']) == plan['oldSelectionSha256']
             and prepared['planSha256'] == digest(tx / '00-plan.json')
             and prepared['selectionSha256'] == value_sha(prepared['selection'])
             and prepared['selection']['previousSha256'] == plan['oldSelectionSha256'], 'Transaction predecessor/hash chain differs')
        return tx, plan, prepared

    def selection_mapping(self, selection):
        """Initial/rolled-back tuples have no candidate mapping attached."""
        tx = self.path('transactions/' + selection['transactionId'])
        if (tx / '10-prepared.json').exists():
            _, plan, prepared = self.transaction(selection['transactionId'])
            if prepared['selection'] == selection:
                return plan['mappingSha256']
        return None

    def lease(self):
        return Lease(self.path('controller.lease'))

    def quiet(self):
        quiet(self.root)

    def stage_release(self, source, expected, transaction):
        pack = package(source, expected)
        target = self.path('releases/' + transaction)
        copy_tree(pack['path'], target, pack['inventory'])
        stock_verify(target, expected, self.path('transactions/' + transaction + '/package-verification'))
        need(package(target, expected)['inventory'] == pack['inventory'], 'Staged package differs')
        return target

    def replace_selector(self, value, expected):
        self.check_selection(value)
        need(value['previousSha256'] == expected, 'Selection predecessor differs')
        current = self.path('active.json')
        need((digest(current) if current.exists() else None) == expected, 'Active predecessor CAS conflict')
        temp = self.path('selector-' + uuid.uuid4().hex + '.tmp')
        write_new(temp, value)
        need((digest(current) if current.exists() else None) == expected, 'Active predecessor changed before replace')
        _checkpoint('before-selector-replace')
        os.replace(temp, current)
        _checkpoint('after-selector-replace')
        need(read(current) == value, 'Actual selector readback differs')

    def initialize(self, old_package, manifest_sha, business_source, data_index_path, data_index_sha, configuration_source, configuration_sha):
        need(not self.root.exists(), 'Initialization requires a new controller root')
        for source in (old_package, business_source, data_index_path, configuration_source):
            disjoint(self.root, source)
        business_source = safe(business_source); data_index = read(data_index_path, data_index_sha)
        source_inventory = snapshot(business_source)
        indexed = {relative(row['path']): {'bytes': row['bytes'], 'sha256': row['sha256']} for row in data_index['files']}
        need(len(indexed) == len(data_index['files']) and source_inventory['files'] == indexed, 'Accepted source index differs')
        need(digest(safe(configuration_source)) == configuration_sha, 'Explicit old configuration differs')
        read(configuration_source)
        self.root.mkdir(parents=True, exist_ok=False)
        with self.path('controller.lease').open('xb') as stream: stream.write(b'ADR0059\n')
        with self.lease():
            self.quiet(); transaction = uuid.uuid4().hex
            release = self.stage_release(old_package, manifest_sha, transaction)
            data = self.path('data/' + transaction)
            copy_tree(business_source, data / 'business', source_inventory)
            config = data / CONFIG; config.parent.mkdir(parents=True)
            with config.open('xb') as stream: stream.write(Path(configuration_source).read_bytes())
            need(digest(config) == configuration_sha, 'Configuration changed during copy')
            selection = {'schema': 'cordis.preview.selection/1', 'selectionId': uuid.uuid4().hex, 'transactionId': transaction,
                         'release': release.relative_to(self.root).as_posix(), 'manifestSha256': manifest_sha,
                         'data': data.relative_to(self.root).as_posix(), 'previousSha256': None}
            write_new(self.path('transactions/' + transaction + '/initial.json'), {'selection': selection, 'data': snapshot(data),
                      'sourceBusinessIndexSha256': data_index_sha, 'sourceConfigurationSha256': configuration_sha})
            self.replace_selector(selection, None)
            return selection

    def validate_runtime(self, selection, receipt_path, receipt_sha, transaction, mapping_sha, expected_business, expected_config, modules):
        self.check_selection(selection)
        proof = read(receipt_path, receipt_sha)
        need(proof.get('schema') == 'cordis.preview.update-validation/1' and proof.get('externallyReviewed') is True
             and proof.get('transactionId') == transaction and proof.get('tupleSha256') == value_sha(selection)
             and proof.get('mappingSha256') == mapping_sha and proof.get('runtimeSchemaValidated') is True, 'Real externally reviewed host validation required')
        launches = proof.get('launches', [])
        need(len(launches) == 2 and {row['entry'] for row in launches} == {'configuration', 'business'}, 'Both original hosts must validate')
        need(len({row['launchId'] for row in launches}) == 2, 'Distinct held host launches required')
        data = self.path(selection['data'])
        need(read(data / BUSINESS) == expected_business and read(data / CONFIG) == expected_config, 'Validated semantics differ')
        for row in launches:
            need(isinstance(row.get('launchId'), str) and re.fullmatch('[0-9a-f]{32}', row['launchId']), 'Invalid launch ID')
            launch = read(self.path('launches/' + relative(row['launchId']) + '/exit.json'), row['launchReceiptSha256'])
            file = data / (BUSINESS if row['entry'] == 'business' else CONFIG)
            need(launch.get('schema') == 'cordis.preview.update-launch/1' and launch['launchId'] == row['launchId']
                 and launch['entry'] == row['entry'] and launch['tupleSha256'] == value_sha(selection)
                 and type(launch['exitCode']) is int and launch['exitCode'] == 0 and launch['normalExitObserved'] is True and launch['error'] is None
                 and launch['forcedCloseOrKillPerformed'] is False and row['pid'] == launch['pid']
                 and type(launch['pid']) is int and launch['pid'] > 0 and launch.get('processHandleObserved') is True
                 and isinstance(launch['creationFileTime'], str) and launch['creationFileTime'].isdigit() and int(launch['creationFileTime']) > 0
                 and row['creationFileTime'] == launch['creationFileTime'] and type(row.get('acceptedRevision')) is int
                 and row['acceptedRevision'] > 0 and row.get('explicitSaveObserved') is True
                 and row['saveOutcome'] in (('Written', 'Unchanged') if row['entry'] == 'business' else ('Saved',))
                 and row['savedFileSha256'] == digest(file)
                 and launch['toolSha256'] == digest(Path(__file__))
                 and launch['dataAfter']['files'][BUSINESS if row['entry'] == 'business' else CONFIG]['sha256'] == row['savedFileSha256'],
                 'Host save/actual normal exit receipt differs')
            if row['entry'] == 'business':
                observed = {item['alias']: item for item in row.get('modules', [])}
                need(len(row.get('modules', [])) == len(observed) == 3 and set(observed) == set(ALIASES), 'Actual loaded module evidence missing')
                for module in modules:
                    item = observed[module['alias']]
                    need(item['sha256'] == module['sha256'] and os.path.normcase(item['path']) == os.path.normcase(str(data / 'business/store' / module['sha256'] / module['libraryName'])), 'Actual new module path/hash differs')
        return snapshot(data)

    def prepare(self, new_package, new_manifest_sha, mapping_path, mapping_sha, review_path, review_sha, expected_active_sha, old_validation, old_validation_sha):
        with self.lease():
            self.quiet(); old_selection = self.selector()
            need(digest(self.path('active.json')) == expected_active_sha, 'Old active CAS differs')
            old = package(self.path(old_selection['release']), old_selection['manifestSha256'])
            new = package(new_package, new_manifest_sha)
            old_version = re.fullmatch(r'0\.1\.5-preview\.(\d+)', old['manifest']['deliveryVersion'])
            new_version = re.fullmatch(r'0\.1\.5-preview\.(\d+)', new['manifest']['deliveryVersion'])
            need(old_version and new_version and int(new_version[1]) >= 3 and int(new_version[1]) > int(old_version[1])
                 and new_manifest_sha != old['sha256'], 'A real newer preview package is required')
            old_data = self.path(old_selection['data']); old_snapshot = snapshot(old_data)
            need(not any(name.endswith('.cordis.lock') for name in old_snapshot['files']), 'Unexplained configuration lock retained')
            old_modules = []
            old_business = read(old_data / BUSINESS)
            for alias in ALIASES:
                desc = read(child(old_data, 'business/store/' + old_business['modules'][alias]['manifest']))
                old_modules.append({'alias': alias, 'sha256': desc['sha256'], 'libraryName': desc['library']})
            self.validate_runtime(old_selection, old_validation, old_validation_sha, old_selection['transactionId'], self.selection_mapping(old_selection),
                                  old_business, read(old_data / CONFIG), old_modules)
            wanted, modules = migration(old, new, old_data, mapping_path, mapping_sha, review_path, review_sha)
            transaction = uuid.uuid4().hex; tx = self.path('transactions/' + transaction); tx.mkdir(parents=True)
            plan = {'oldSelection': old_selection, 'oldSelectionSha256': expected_active_sha, 'oldData': old_snapshot,
                    'mappingSha256': mapping_sha, 'mapReviewSha256': review_sha, 'transactionId': transaction}
            write_new(tx / '00-plan.json', plan)
            _checkpoint('after-plan')
            release = self.stage_release(new_package, new_manifest_sha, transaction)
            copy_tree(old_data, self.path('backups/' + transaction), old_snapshot)
            _checkpoint('after-backup')
            data = self.path('data/' + transaction); copy_tree(old_data, data, old_snapshot)
            for module in modules:
                target = data / 'business/store' / module['sha256']
                declared = child(new['path'], module['manifest'])
                pair = {module['libraryName']: module['library'], 'module.json': declared}
                if target.exists():
                    need(snapshot(target) == {'files': {name: {'bytes': path.stat().st_size, 'sha256': digest(path)} for name, path in pair.items()}, 'directories': []}, 'Existing hash directory identity differs')
                else:
                    target.mkdir()
                    for name, origin in pair.items():
                        with child(target, name).open('xb') as stream: stream.write(origin.read_bytes())
                need(digest(target / module['libraryName']) == module['sha256']
                     and digest(target / 'module.json') == digest(declared), 'Installed module differs')
            file = data / BUSINESS; temp = data / 'business/candidate-session.tmp'
            write_new(temp, wanted); os.replace(temp, file)
            selection = {'schema': 'cordis.preview.selection/1', 'selectionId': uuid.uuid4().hex, 'transactionId': transaction,
                         'release': release.relative_to(self.root).as_posix(), 'manifestSha256': new_manifest_sha,
                         'data': data.relative_to(self.root).as_posix(), 'previousSha256': expected_active_sha}
            need(snapshot(old_data) == old_snapshot, 'Old data changed during preparation')
            need(package(new['path'], new_manifest_sha)['inventory'] == new['inventory'], 'Source package changed during preparation')
            prepared = {'planSha256': digest(tx / '00-plan.json'), 'selection': selection, 'selectionSha256': value_sha(selection), 'oldData': old_snapshot,
                        'candidateData': snapshot(data), 'businessValue': wanted, 'configurationValue': read(data / CONFIG),
                        'modules': [{key: str(value) if isinstance(value, Path) else value for key, value in module.items()} for module in modules]}
            write_new(tx / '10-prepared.json', prepared); _checkpoint('after-prepared')
            return {'transactionId': transaction, 'candidate': selection, 'activeUnchanged': True}

    def activate(self, transaction, receipt_path, receipt_sha):
        with self.lease():
            self.quiet(); tx, plan, prepared = self.transaction(transaction)
            need(not (tx / '30-activation-intent.json').exists(), 'Existing activation intent requires recovery, never replay')
            need(digest(self.path('active.json')) == plan['oldSelectionSha256']
                 and snapshot(self.path(plan['oldSelection']['data'])) == plan['oldData'], 'Old selection/data CAS changed')
            package(self.path(prepared['selection']['release']), prepared['selection']['manifestSha256'])
            data = self.validate_runtime(prepared['selection'], receipt_path, receipt_sha, transaction, plan['mappingSha256'],
                                         prepared['businessValue'], prepared['configurationValue'], prepared['modules'])
            protected = {name: row for name, row in prepared['candidateData']['files'].items() if name not in (CONFIG, BUSINESS)}
            need({name: row for name, row in data['files'].items() if name not in (CONFIG, BUSINESS)} == protected
                 and data['directories'] == prepared['candidateData']['directories'], 'Candidate nonconfiguration data changed')
            write_new(tx / '20-validated.json', {'preparedSha256': digest(tx / '10-prepared.json'),
                      'receiptPath': str(safe(receipt_path)), 'receiptSha256': receipt_sha, 'data': data})
            _checkpoint('after-validated')
            self.quiet()
            need(snapshot(self.path(plan['oldSelection']['data'])) == plan['oldData'], 'Old data changed before activation')
            write_new(tx / '30-activation-intent.json', {'validatedSha256': digest(tx / '20-validated.json'),
                      'oldSha256': plan['oldSelectionSha256'], 'selection': prepared['selection'], 'newSha256': prepared['selectionSha256']})
            self.replace_selector(prepared['selection'], plan['oldSelectionSha256'])
            write_new(tx / '40-activated.json', {'activeSha256': digest(self.path('active.json')), 'validationReceiptSha256': receipt_sha})
            return {'activated': True, 'selection': prepared['selection'], 'runtimeEvidenceWasExternal': True}

    def recover(self, transaction, rollback=False, resume=False):
        with self.lease():
            need(not (rollback and resume), 'Choose rollback or validated resume')
            need(isinstance(transaction, str) and re.fullmatch('[0-9a-f]{32}', transaction), 'Invalid transaction ID')
            self.quiet(); tx = self.path('transactions/' + transaction); plan = read(tx / '00-plan.json')
            self.check_selection(plan['oldSelection'])
            need(plan['transactionId'] == transaction and value_sha(plan['oldSelection']) == plan['oldSelectionSha256'], 'Recovery plan binding differs')
            current = digest(self.path('active.json'))
            rollback_intent = tx / 'rollback-intent.json'
            if rollback_intent.exists():
                previous = read(rollback_intent)
                self.check_selection(previous['selection'])
                _, _, prepared_rollback = self.transaction(transaction)
                need(previous['selection']['transactionId'] == transaction
                     and previous['selection']['previousSha256'] == previous['expectedCurrentSha256'] == prepared_rollback['selectionSha256']
                     and all(previous['selection'][key] == plan['oldSelection'][key] for key in ('release', 'manifestSha256', 'data')),
                     'Rollback intent differs from the exact old app/data tuple')
                if current == value_sha(previous['selection']):
                    if not (tx / 'rolled-back.json').exists(): write_new(tx / 'rolled-back.json', {'activeSha256': current, 'observedAfterInterruption': True})
                    return {'state': 'rolled-back', 'filesDeleted': False}
            if current == plan['oldSelectionSha256']:
                need(snapshot(self.path(plan['oldSelection']['data'])) == plan['oldData'], 'Old recovery data changed; retain all versions')
                if not resume:
                    return {'state': 'old-selection-not-committed', 'filesDeleted': False}
                tx, plan, prepared = self.transaction(transaction)
                validated = read(tx / '20-validated.json')
                need(validated['preparedSha256'] == digest(tx / '10-prepared.json'), 'Validated preparation binding differs')
                package(self.path(prepared['selection']['release']), prepared['selection']['manifestSha256'])
                data = self.validate_runtime(prepared['selection'], validated['receiptPath'], validated['receiptSha256'], transaction,
                    plan['mappingSha256'], prepared['businessValue'], prepared['configurationValue'], prepared['modules'])
                need(data == validated['data'], 'Validated candidate changed after interruption')
                intent = {'validatedSha256': digest(tx / '20-validated.json'), 'oldSha256': plan['oldSelectionSha256'],
                          'selection': prepared['selection'], 'newSha256': prepared['selectionSha256']}
                intent_path = tx / '30-activation-intent.json'
                if intent_path.exists(): need(read(intent_path) == intent, 'Existing activation intent differs')
                else: write_new(intent_path, intent)
                self.quiet()
                need(snapshot(self.path(plan['oldSelection']['data'])) == plan['oldData']
                     and snapshot(self.path(prepared['selection']['data'])) == data, 'Recovery data changed before commit')
                self.replace_selector(prepared['selection'], current)
                write_new(tx / '40-activated.json', {'activeSha256': digest(self.path('active.json')), 'resumedValidatedCommit': True})
                return {'state': 'new-selection-committed', 'filesDeleted': False}
            tx, plan, prepared = self.transaction(transaction)
            intent_path = tx / '30-activation-intent.json'
            need(intent_path.exists(), 'Foreign selection; no recorded activation intent')
            intent = read(intent_path)
            need(intent['selection'] == prepared['selection'] and intent['newSha256'] == prepared['selectionSha256']
                 and intent['oldSha256'] == plan['oldSelectionSha256']
                 and intent['validatedSha256'] == digest(tx / '20-validated.json')
                 and current == intent['newSha256'], 'Foreign selector; no recovery guessing')
            if not rollback:
                if not (tx / '40-activated.json').exists(): write_new(tx / '40-activated.json', {'activeSha256': current, 'observedAfterInterruption': True})
                return {'state': 'new-selection-committed', 'filesDeleted': False}
            old = plan['oldSelection']; package(self.path(old['release']), old['manifestSha256'])
            need(snapshot(self.path(old['data'])) == plan['oldData'], 'Old rollback data changed; retain all versions')
            if rollback_intent.exists():
                selection = read(rollback_intent)['selection']
            else:
                selection = {**old, 'selectionId': uuid.uuid4().hex, 'transactionId': transaction, 'previousSha256': current}
                write_new(rollback_intent, {'selection': selection, 'expectedCurrentSha256': current,
                                          'retainedNewData': snapshot(self.path(intent['selection']['data']))})
            self.replace_selector(selection, current)
            write_new(tx / 'rolled-back.json', {'activeSha256': digest(self.path('active.json')), 'filesDeleted': False})
            return {'state': 'rolled-back', 'filesDeleted': False}

    def launch(self, entry, transaction=None, driver=None, driver_sha=None):
        need(entry in ('configuration', 'business'), 'Only the two fixed entrypoints can launch')
        need(os.name == 'nt', 'Real process receipt requires Windows')
        if driver is not None:
            driver = safe(driver)
            need(hex64(driver_sha) and digest(driver) == driver_sha, 'External QA driver SHA differs')
        with self.lease():
            self.quiet()
            selection = self.transaction(transaction)[2]['selection'] if transaction else self.selector()
            pack = package(self.path(selection['release']), selection['manifestSha256']); data = self.path(selection['data'])
            identifier = uuid.uuid4().hex; output = self.path('launches/' + identifier); output.mkdir(parents=True)
            executable = pack['path'] / ('cordis_config_workbench.exe' if entry == 'configuration' else 'cordis_platform_workbench_demo.exe')
            arguments = ['--file', str(data / CONFIG)] if entry == 'configuration' else [str(data / BUSINESS), str(data / 'business/store')]
            environment = dict(os.environ)
            for key in list(environment):
                if key.upper().startswith(('QT', 'QML', 'VSCMD', 'CMAKE', 'CONDA')) or key.upper() in ('PATH', 'QTDIR', 'INCLUDE', 'LIB', 'LIBPATH', 'CL', '_CL_', 'LINK', '_LINK_', 'PYTHONPATH', 'VSINSTALLDIR', 'VCINSTALLDIR'):
                    del environment[key]
            system = environment.get('SystemRoot', environment.get('SYSTEMROOT', r'C:\Windows'))
            environment['PATH'] = os.pathsep.join((str(Path(system) / 'System32'), system, str(Path(system) / 'System32/Wbem')))
            environment['QT_QPA_PLATFORM'] = 'windows'
            record = {'schema': 'cordis.preview.update-launch/1', 'launchId': identifier, 'entry': entry,
                      'tupleSha256': value_sha(selection), 'command': [str(executable), *arguments], 'error': None,
                      'forcedCloseOrKillPerformed': False, 'runtimeAcceptanceClaimed': False, 'toolSha256': digest(Path(__file__))}
            process = subprocess.Popen(record['command'], cwd=str(output), env=environment, shell=False,
                                       stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            record.update(pid=process.pid, creationFileTime=None)
            streams = []; threads = []; ui_process = None
            def drain(stream, path, facts):
                target = None
                try:
                    try: target = path.open('xb')
                    except OSError as error: facts['error'] = str(error)
                    while block := stream.read(65536):
                        facts['seen'] += len(block); room = max(0, 4194304 - facts['saved'])
                        if target is not None:
                            try: target.write(block[:room]); facts['saved'] += min(room, len(block))
                            except OSError as error: facts['error'] = str(error); target.close(); target = None
                    facts['eof'] = True
                except Exception as error: facts['error'] = str(error)
                finally:
                    if target is not None: target.close()
                    stream.close()
            def capture(child_process, prefix):
                for name, stream in (('stdout', child_process.stdout), ('stderr', child_process.stderr)):
                    facts = {'name': prefix + name, 'seen': 0, 'saved': 0, 'eof': False, 'error': None}; streams.append(facts)
                    thread = threading.Thread(target=drain, args=(stream, output / (prefix + name + '.raw'), facts), daemon=True)
                    thread.start(); threads.append(thread)
            try:
                capture(process, '')
                class FileTime(ctypes.Structure):
                    _fields_ = [('low', ctypes.c_uint32), ('high', ctypes.c_uint32)]
                kernel = ctypes.WinDLL('kernel32', use_last_error=True); times = [FileTime() for _ in range(4)]
                kernel.GetProcessTimes.argtypes = [ctypes.c_void_p] + [ctypes.POINTER(FileTime)] * 4
                created = kernel.GetProcessTimes(int(process._handle), *(ctypes.byref(item) for item in times))
                need(bool(created), 'Cannot observe actual process creation time')
                record['creationFileTime'] = str((times[0].high << 32) | times[0].low)
                record['processHandle'] = hex(int(process._handle)); record['processHandleObserved'] = True
                target = describe(process.pid); wrapper = describe(os.getpid())
                need(target['creationFileTime'] == record['creationFileTime'] and target['parentPid'] == wrapper['pid']
                     and target['sessionId'] == wrapper['sessionId'] and target['ownerSid'] == wrapper['ownerSid']
                     and os.path.normcase(target['path']) == os.path.normcase(str(executable)), 'Actual child identity differs')
                ui_tools = [ROOT / name for name in ('tools/preview_ui.py', 'tools/preview_ui_action.ps1',
                            'delivery/windows/verification/preview-ui.psm1', 'delivery/windows/verification/preview-windows.psm1')]
                evidence = output / 'ui-evidence'; evidence.mkdir()
                registration = {'schema': 'cordis.preview.update-launch.registration/1', 'launchId': identifier,
                    'transactionId': selection['transactionId'], 'entry': entry, 'tuple': selection, 'tupleSha256': value_sha(selection),
                    'mappingSha256': self.selection_mapping(selection),
                    'packageRoot': str(pack['path']), 'dataRoot': str(data), 'configurationFile': str(data / CONFIG),
                    'businessFile': str(data / BUSINESS), 'storeRoot': str(data / 'business/store'),
                    'manifestSha256': selection['manifestSha256'], 'target': target, 'wrapper': wrapper,
                    'driverPath': str(driver) if driver is not None else None, 'driverSha256': driver_sha,
                    'evidenceRoot': str(evidence), 'uiTools': [{'path': str(path), 'bytes': path.stat().st_size, 'sha256': digest(path)} for path in ui_tools]}
                write_new(output / 'registration.json', registration)
                record['registrationSha256'] = digest(output / 'registration.json')
                write_new(output / 'started.json', record)
                if driver is not None:
                    need(digest(driver) == driver_sha, 'External driver changed before launch')
                    ui_process = subprocess.Popen([sys.executable, '-I', '-B', str(driver), '--registration', str(output / 'registration.json'),
                        '--expected-registration-sha256', record['registrationSha256']], cwd=str(evidence), env=environment,
                        shell=False, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                        creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
                    capture(ui_process, 'driver-')
                    record['uiDriverPid'] = ui_process.pid
                record['exitCode'] = process.wait()
            except BaseException as error:
                record['error'] = repr(error)
                while process.poll() is None:
                    try: process.wait(timeout=.25)
                    except (subprocess.TimeoutExpired, KeyboardInterrupt): continue
                record['exitCode'] = process.returncode
            if ui_process is not None:
                while ui_process.poll() is None:
                    try: ui_process.wait(timeout=.25)
                    except (subprocess.TimeoutExpired, KeyboardInterrupt): continue
                record['uiDriverExitCode'] = ui_process.returncode
                if ui_process.returncode: record['error'] = record['error'] or 'External UI driver failed; no input replay'
            for thread in threads: thread.join(2)
            record['normalExitObserved'] = record['exitCode'] == 0
            record['streams'] = streams
            if any(not item['eof'] or item['error'] or item['seen'] != item['saved'] for item in streams): record['error'] = record['error'] or 'Incomplete/truncated logs'
            try:
                record['dataAfter'] = snapshot(data)
                need(snapshot(pack['path']) == pack['inventory'], 'Application changed during launch')
            except Exception as error: record['error'] = record['error'] or repr(error)
            write_new(output / 'exit.json', record)
            return {'launchId': identifier, 'receiptSha256': digest(output / 'exit.json'), 'exitCode': record['exitCode'], 'runtimeAcceptanceClaimed': False}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__); parser.add_argument('--root', type=Path, required=True)
    commands = parser.add_subparsers(dest='command', required=True)
    init = commands.add_parser('init')
    for name in ('old-package', 'manifest-sha', 'business-source', 'data-index', 'data-index-sha', 'configuration-source', 'configuration-sha'): init.add_argument('--' + name, required=True)
    prepare = commands.add_parser('prepare')
    for name in ('new-package', 'new-manifest-sha', 'mapping', 'mapping-sha', 'map-review', 'map-review-sha', 'expected-active-sha', 'old-validation', 'old-validation-sha'): prepare.add_argument('--' + name, required=True)
    launch = commands.add_parser('launch'); launch.add_argument('--entry', choices=('configuration', 'business'), required=True); launch.add_argument('--candidate')
    launch.add_argument('--driver', type=Path); launch.add_argument('--driver-sha')
    activate = commands.add_parser('activate')
    for name in ('transaction', 'validation', 'validation-sha'): activate.add_argument('--' + name, required=True)
    recover = commands.add_parser('recover'); recover.add_argument('--transaction', required=True)
    recovery = recover.add_mutually_exclusive_group(); recovery.add_argument('--rollback', action='store_true'); recovery.add_argument('--resume', action='store_true')
    commands.add_parser('status'); args = parser.parse_args(argv)
    try:
        owner = Updater(args.root)
        if args.command == 'init': result = owner.initialize(args.old_package, args.manifest_sha, args.business_source, args.data_index, args.data_index_sha, args.configuration_source, args.configuration_sha)
        elif args.command == 'prepare': result = owner.prepare(args.new_package, args.new_manifest_sha, args.mapping, args.mapping_sha, args.map_review, args.map_review_sha, args.expected_active_sha, args.old_validation, args.old_validation_sha)
        elif args.command == 'activate': result = owner.activate(args.transaction, args.validation, args.validation_sha)
        elif args.command == 'recover': result = owner.recover(args.transaction, args.rollback, args.resume)
        elif args.command == 'launch': result = owner.launch(args.entry, args.candidate, args.driver, args.driver_sha)
        else: result = {'selection': owner.selector(), 'activeSha256': digest(owner.path('active.json')), 'runtimeAcceptanceClaimed': False}
        print(json.dumps(result, ensure_ascii=False, indent=2)); return 0
    except (OSError, ValueError, KeyError, TypeError, Refused, subprocess.SubprocessError) as error:
        print('Offline update refused: ' + str(error), file=sys.stderr); return 1


if __name__ == '__main__':
    raise SystemExit(main())
