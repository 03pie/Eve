"""Copy a frozen external reader and matching host into a standalone Windows ZIP."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import stat
import zipfile

import package_preview as common
from preview_pe import file_version, inspect_pe

ROOT = Path(__file__).resolve().parent.parent
ASSETS = ROOT / 'delivery/windows/reader'
HOST_FILES = {'cordis_plugin_workbench.exe', 'cordis_core.dll', 'cordis_qt.dll', 'cordis_ui.dll', 'ElaWidgetTools.dll'}
DEPLOYMENT_SHA = '41cd2aa93b23f0780bd1f7fb6a934a82933556bf49303b43e360997ca640702f'
DEPLOYMENT_MANIFEST = 'fdff0203a649922a43b3668574b30c0eec09313f5d038065467515072e45b13b'
SDK_SHA = '58e0a1e0ea1aa9ed02b5c2d6bccc1f6d8b6187dcc57a485ede9d79f7562a3cfd'
META = {'reader-manifest.json', 'SHA256SUMS.txt'}


def need(condition, message):
    if not condition:
        raise ValueError(message)


def file_map(folder):
    result = {}
    for path in folder.rglob('*'):
        common.reject_reparse(path)
        if path.is_file():
            result[path.relative_to(folder).as_posix()] = {'bytes': path.stat().st_size, 'sha256': common.sha(path)}
    return result


def checked_zip(archive):
    """Safe exact file inventory; callers select only explicitly named members."""
    observed = {}
    seen = set()
    for entry in archive.infolist():
        name = common.relative_path(entry.filename.rstrip('/'))
        need(name.casefold() not in seen and not entry.flag_bits & 1 and
             stat.S_IFMT(entry.external_attr >> 16) != stat.S_IFLNK and not entry.external_attr & 0x400,
             'Linked/encrypted/duplicate ZIP entry')
        seen.add(name.casefold())
        if entry.is_dir():
            continue
        digest, count = hashlib.sha256(), 0
        with archive.open(entry) as source:
            while block := source.read(1024 * 1024):
                count += len(block); digest.update(block)
        need(count == entry.file_size, 'ZIP member length differs')
        observed[name] = {'bytes': count, 'sha256': digest.hexdigest()}
    return observed


def select_sources(plan):
    need(plan['schema'] == 'cordis.reader.package-inputs/1' and plan['rootFreezeApproved'] is True,
         'Explicit root-reviewed input freeze required')
    need(set(plan['hostFiles']) == HOST_FILES, 'Exactly the five matching host files are required')
    module_root = common.reject_reparse(Path(plan['pluginRoot'])).resolve()
    module = common.read_json(common.regular(module_root / 'module.json'))
    library = common.relative_path(module['library'])
    need('/' not in library and library.lower().endswith('.dll') and module['moduleVersion'] == '1.1.0' and
         module['loader'] == 'qt-ui' and module['unloadPolicy'] == 'resident' and len(module['plugins']) == 1,
         'Expected the final resident 1.1.0 reader module')
    need(set(plan['pluginFiles']) == {library, 'module.json'} and
         plan['pluginFiles'][library]['sha256'] == module['sha256'], 'Plugin manifest/DLL frozen identity differs')
    sources = []
    for root, records, prefix, role in ((Path(plan['hostRoot']), plan['hostFiles'], '', 'matching-host'),
                                        (module_root, plan['pluginFiles'], 'plugins/current/', 'external-reader')):
        for name, facts in records.items():
            path = common.regular(root / common.relative_path(name))
            need(type(facts['bytes']) is int and path.stat().st_size == facts['bytes'] and common.sha(path) == facts['sha256'],
                 'Frozen source differs: ' + str(path))
            sources.append({'path': prefix + name, **facts, 'role': role,
                            'source': {'path': str(path), 'identity': common.identity(path)}})
    for name in ('README.txt', 'verify-reader.ps1'):
        path = common.regular(ASSETS / name)
        sources.append({'path': name, 'bytes': path.stat().st_size, 'sha256': common.sha(path), 'role': 'reader-support',
                        'source': {'path': str(path), 'identity': common.identity(path)}})
    return sources, module


def deployment_members(payload):
    selected = {'Qt5Core.dll', 'Qt5Gui.dll', 'Qt5Widgets.dll', 'qt.conf', *common.CRT_FILES,
                *('plugins/' + n for n in common.QT_PLUGINS), *(n for n in payload if n.startswith('licenses/'))}
    need(selected <= payload.keys(), 'Fixed Qt deployment member is missing')
    return selected


def package(args):
    producers = {p: common.sha(common.regular(p)) for p in
                 (Path(__file__).resolve(), Path(common.__file__).resolve(), ROOT / 'tools/preview_pe.py')}
    input_path = common.regular(args.inputs)
    original = input_path.read_bytes()
    need(hashlib.sha256(original).hexdigest() == args.inputs_sha256, 'Input freeze external SHA differs')
    plan = json.loads(original, object_pairs_hook=common.unique_object)
    sources, module = select_sources(plan)
    output = common.reject_reparse(args.output).resolve()
    need(args.output.is_absolute() and not output.exists() and output.name and
         output != ROOT and not ROOT.is_relative_to(output), 'Output must be an independent new directory')
    archive, sidecar = Path(str(output) + '.zip'), Path(str(output) + '.verification.json')
    roundtrip = output.with_name(output.name + '-verification')
    for path in (archive, sidecar, roundtrip, archive.with_suffix('.zip.sha256')):
        common.reject_reparse(path); need(not path.exists(), 'Package output already exists')
    for source in sources:
        path = Path(source['source']['path'])
        need(not path.is_relative_to(output) and not output.is_relative_to(path.parent), 'Input/output overlap')
    runtime, sdk = common.regular(Path(plan['qtRuntimeArchive'])), common.regular(Path(plan['sdkArchive']))
    need(common.sha(runtime) == DEPLOYMENT_SHA and common.sha(sdk) == SDK_SHA, 'Fixed deployment/SDK ZIP differs')
    with zipfile.ZipFile(sdk) as installed:
        sdk_files = json.loads(installed.read('SDK-MANIFEST.json'))['files']
    for name in HOST_FILES - {'cordis_plugin_workbench.exe'}:
        sdk_name = 'ElaWidgetTools/bin/' + name if name == 'ElaWidgetTools.dll' else 'bin/' + name
        need(plan['hostFiles'][name] == sdk_files[sdk_name], 'Host shared runtime differs from the reviewed SDK')
    with zipfile.ZipFile(runtime) as deployed:
        observed = checked_zip(deployed)
        prefix = 'Cordis-0.1.5-preview.3-windows-x64/'
        mp = prefix + 'build-manifest.json'
        need(observed[mp]['sha256'] == DEPLOYMENT_MANIFEST, 'Fixed Qt deployment manifest differs')
        manifest = json.loads(deployed.read(mp))
        payload = {row['path']: {'bytes': row['bytes'], 'sha256': row['sha256']} for row in manifest['files']}
        need(len(payload) == 136 and len(observed) == 138 and
             all(observed[prefix + name] == facts for name, facts in payload.items()), 'Deployment exact payload differs')
        selected = deployment_members(payload)
        output.mkdir(parents=True)
        common.copy_inputs(sources, output)
        for name in sorted(selected):
            path = output / common.relative_path(name); path.parent.mkdir(parents=True, exist_ok=True)
            with deployed.open(prefix + name) as source, path.open('xb') as destination:
                shutil.copyfileobj(source, destination)
            facts = payload[name]
            need(common.sha(path) == facts['sha256'] and path.stat().st_size == facts['bytes'], 'Deployment copy differs')
            sources.append({'path': name, **facts, 'role': 'fixed-qt-crt-or-license',
                            'source': {'archive': str(runtime), 'archiveSha256': DEPLOYMENT_SHA, 'entry': prefix + name}})
    # Only static PE parsing; no plugin/runtime image is loaded by this tool.
    available = {r['path'].casefold() for r in sources}
    system = set()
    for row in sources:
        if not row['path'].lower().endswith(('.dll', '.exe')):
            continue
        image = output / row['path']; row['pe'] = inspect_pe(image)
        row['pe']['fileVersion'] = file_version(image)
        if Path(row['path']).name in {'Qt5Core.dll', 'Qt5Gui.dll', 'Qt5Widgets.dll'}:
            need(row['pe']['fileVersion'] == '5.15.2.0', 'Qt file version differs')
        for name in row['pe']['imports'] + row['pe']['delayImports']:
            adjacent = (Path(row['path']).parent / name).as_posix().casefold()
            if name in available or adjacent in available:
                continue
            need(name[:-4] in common.SYSTEM_DLLS | {'combase', 'runtimeobject', 'shcore'} or
                 re.fullmatch(r'(?:api|ext)-ms-win-[a-z0-9-]+\.dll', name), 'Unresolved PE dependency: ' + name)
            system.add(name)
    expected = {row['path']: {'bytes': row['bytes'], 'sha256': row['sha256']} for row in sources}
    need(len(expected) == len(sources) and file_map(output) == expected, 'Reader exact source/copy set differs')
    reader_manifest = {'schema': 'cordis.reader.package/1', 'applicationVersion': '0.1.5', 'pluginVersion': '1.1.0',
                       'pluginId': module['plugins'][0], 'moduleId': module['moduleId'], 'platform': 'windows-x64',
                       'files': sorted(sources, key=lambda row: row['path']), 'declaredSystemImports': sorted(system),
                       'pdfBackend': 'Windows.Data.Pdf.PdfDocument', 'inputSha256': args.inputs_sha256,
                       'sdkArchiveSha256': SDK_SHA, 'qtDeploymentArchiveSha256': DEPLOYMENT_SHA,
                       'producerSha256': {str(p): value for p, value in producers.items()},
                       'nativeApprovalGranted': False, 'runtimeAcceptancePending': True}
    common.write_exclusive(output / 'reader-manifest.json', common.canonical(reader_manifest))
    manifest_sha = common.sha(output / 'reader-manifest.json')
    expected['reader-manifest.json'] = {'bytes': (output / 'reader-manifest.json').stat().st_size, 'sha256': manifest_sha}
    common.write_exclusive(output / 'SHA256SUMS.txt', ''.join(f'{f["sha256"]}  {n}\n' for n, f in sorted(expected.items())).encode())
    expected = file_map(output)
    with zipfile.ZipFile(archive, 'x', compression=zipfile.ZIP_DEFLATED) as packed:
        for name in sorted(expected): packed.write(output/name, output.name+'/'+name)
    with zipfile.ZipFile(archive) as packed:
        actual = checked_zip(packed)
        need(actual == {output.name+'/'+n: f for n, f in expected.items()}, 'ZIP exact inventory differs')
        roundtrip.mkdir()
        for name in expected:
            destination = roundtrip / name; destination.parent.mkdir(parents=True, exist_ok=True)
            with packed.open(output.name+'/'+name) as source, destination.open('xb') as target:
                shutil.copyfileobj(source, target)
    need(file_map(roundtrip) == expected and file_map(output) == expected, 'Full extracted inventory differs')
    for row in sources:
        if 'path' in row['source']:
            path = common.regular(Path(row['source']['path']))
            need(common.sha(path) == row['sha256'] and common.identity(path) == row['source']['identity'], 'Source changed at closure')
    need(common.sha(input_path) == args.inputs_sha256 and common.sha(runtime) == DEPLOYMENT_SHA and common.sha(sdk) == SDK_SHA,
         'Frozen input/archive changed')
    need(all(common.sha(p) == value for p, value in producers.items()), 'Package producer changed during collection')
    result = {'schema': 'cordis.reader.archive-verification/1', 'passed': True, 'archivePath': str(archive),
              'archiveSha256': common.sha(archive), 'archiveBytes': archive.stat().st_size, 'manifestSha256': manifest_sha,
              'files': len(expected), 'uncompressedBytes': sum(f['bytes'] for f in expected.values()),
              'fullExtractionLengthAndSha256Verified': True, 'sourceUnchanged': True, 'payloadExecuted': False,
              'runtimeAcceptancePending': True, 'P7Complete': False}
    common.write_exclusive(sidecar, common.canonical(result))
    common.write_exclusive(archive.with_suffix('.zip.sha256'), (result['archiveSha256']+'  '+archive.name+'\n').encode())
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument('--inputs', type=Path, required=True)
    cli.add_argument('--inputs-sha256', required=True)
    cli.add_argument('--output', type=Path, required=True)
    package(cli.parse_args())
