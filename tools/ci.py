"""Finite CI stages. A successful candidate or static ZIP check never promotes a runtime."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import platform
import re
import shutil
import stat
import subprocess
import sys
import zipfile

import export_native_qualification as qualification
import package_preview as packaging

ROOT = Path(__file__).resolve().parents[1]
CATALOG = 'qualification/native-business-p6-3-20261123.json'
CATALOG_SHA = '44758309aede7a215874d4d8307fd3421e20da2befbdd5f288516de5b7382246'
VERIFIER = 'delivery/windows/verification/verify-package.ps1'
VERIFIER_SHA = '321a0aa891390d5c57a88aa1d01cadf121c511a1970b9c6d8ee776ce526d9177'
PRODUCTS = {'p5.platform_product_' + n for n in ('catalog', 'roundtrip', 'empty', 'wrong_hash', 'changed_candidate', 'restart')}
ENVELOPE = ('tools/ci.py', 'tests/tools/test_ci.py', '.github/workflows/ci.yml', 'CMakePresets.json',
            'docs/adr/0060-layered-ci-and-artifact-promotion.md')


class Stop(RuntimeError):
    def __init__(self, state, message):
        super().__init__(message)
        self.state = state


def need(ok, message, state='Failed'):
    if not ok:
        raise Stop(state, message)


def sha(path):
    return qualification.sha(packaging.regular(path))


def write(path, value):
    with Path(path).open('x', encoding='utf-8', newline='\n') as stream:
        json.dump(value, stream, indent=2, ensure_ascii=False)
        stream.write('\n')


def inputs(output):
    return {'sources': qualification.source_inputs(ROOT, output),
            'ciEnvelope': {name: sha(ROOT / name) for name in ENVELOPE}}


def tool(value):
    found = shutil.which(str(value))
    need(found is not None, 'Required tool unavailable: ' + str(value), 'PendingEnvironment')
    return str(packaging.regular(found).resolve())


class Run:
    def __init__(self, output, stage):
        self.output, self.env = output, dict(os.environ)
        self.result = {'schema': 'cordis.ci.stage/1', 'stage': stage, 'state': 'Running', 'commands': [],
                       'runtimeApproved': False, 'fullSupportMatrixClaimed': False, 'gui': 'Pending',
                       'cleanMachine': 'Pending', 'python': {'path': sys.executable, 'version': platform.python_version()}}

    def call(self, arguments, label):
        directory = self.output / ('%02d-' % (len(self.result['commands']) + 1) + label)
        directory.mkdir()
        record = {'argv': [str(v) for v in arguments], 'cwd': str(ROOT)}
        write(directory / 'command.json', record)
        print('COMMAND ' + json.dumps(record), flush=True)
        with (directory / 'stdout.raw').open('xb') as out, (directory / 'stderr.raw').open('xb') as err:
            process = subprocess.Popen(record['argv'], cwd=ROOT, env=self.env, stdout=out, stderr=err,
                                       creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            record['pid'] = process.pid
            try:
                write(directory / 'started.json', {'pid': process.pid})
                process.wait()
            except BaseException as error:
                record['interrupted'] = repr(error)
            finally:
                while process.poll() is None:
                    try: process.wait()
                    except KeyboardInterrupt as error: record['interrupted'] = repr(error)
                record['exitCode'] = process.returncode
        record['naturalExitObserved'] = True
        record['logs'] = {}
        for name in ('stdout', 'stderr'):
            path = directory / (name + '.raw')
            raw = path.read_bytes()
            record['logs'][name] = {'path': str(path), 'bytes': len(raw), 'sha256': hashlib.sha256(raw).hexdigest(),
                                    'decodedConsoleComplete': len(raw) <= 4194304}
            for line in raw[:4194304].decode('utf-8', errors='backslashreplace').splitlines():
                print('[' + label + ' ' + name + ' decoded] ' + line)
        self.result['commands'].append(record)
        write(directory / 'result.json', record)
        print('EXIT ' + str(record['exitCode']), flush=True)
        need(all(v['decodedConsoleComplete'] for v in record['logs'].values()), 'Console export limit exceeded; raw retained')
        need(record['exitCode'] == 0 and 'interrupted' not in record, 'Actual command failed/interrupted: ' + label)
        return directory


def ctest(run, executable, tree, label, expected=None):
    select = [executable, '--test-dir', str(tree), '--show-only=json-v1']
    pattern = ['-R', r'^p5\.platform_product_'] if expected is not None else []
    selected = run.call(select + pattern, label + '-selection') / 'stdout.raw'
    tests = qualification.read_json(selected)['tests']
    names = [t['name'] for t in tests]
    need(names and len(names) == len(set(names)), 'Missing/duplicate selected tests')
    need(not any(p['value'] for t in tests for p in t['properties'] if p['name'] == 'DISABLED'), 'Disabled selected test')
    if expected is not None:
        need(set(names) == expected and len(names) == 6, 'Exact six product tests required')
    actual = run.call([executable, '--test-dir', str(tree), '-j', '1', '--output-on-failure', '--timeout', '900', *pattern], label)
    passed = qualification.passed_names((actual / 'stdout.raw').read_bytes())
    need(passed == set(names), 'Actual Passed names differ from complete selection')
    return {'selected': sorted(names), 'passed': sorted(passed), 'selectionSha256': sha(selected)}


def source(run, args):
    cmake, test, ninja = tool(args.cmake), tool(args.ctest), tool(args.ninja)
    for name, executable in (('cmake', cmake), ('ctest', test), ('ninja', ninja)):
        run.call([executable, '--version'], name + '-version')
    tree = run.output / 'build'
    run.call([cmake, '--preset', args.preset, '-B', str(tree), '-DCORDIS_REQUIRE_QT5152=OFF',
              '-DCORDIS_BUILD_ELA_UI=OFF', '-DCORDIS_SHARED_CORE=OFF', '-DCORDIS_BUILD_EXAMPLES=ON',
              '-DBUILD_TESTING=ON', '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON', '-DCMAKE_MAKE_PROGRAM=' + ninja], 'configure')
    compiler = list((tree / 'CMakeFiles').glob('*/CMakeCXXCompiler.cmake'))
    need(len(compiler) == 1, 'Compiler metadata is missing/ambiguous')
    text = compiler[0].read_text(encoding='utf-8')
    fields = ('CMAKE_CXX_COMPILER_ID', 'CMAKE_CXX_COMPILER_VERSION', 'CMAKE_CXX_SIZEOF_DATA_PTR')
    values = {k: re.search(r'set\(' + k + r' "([^"\n]+)"\)', text) for k in fields}
    need(all(values.values()), 'Incomplete compiler metadata')
    run.result['compiler'] = {k: v.group(1) for k, v in values.items()}
    run.result['compiler']['metadataSha256'] = sha(compiler[0])
    run.call([cmake, '--build', str(tree), '--parallel', '2'], 'build')
    run.result['tests'] = ctest(run, test, tree, 'full-core')
    for label, pattern, count in (('packaging-units', 'test_preview_*packaging.py', 55),
                                  ('update-units', 'test_preview_update.py', 24), ('ci-units', 'test_ci.py', None),
                                  ('readonly-owner-units', 'test_record_readonly_save.py', 2)):
        logs = run.call([sys.executable, '-B', '-m', 'unittest', 'discover', '-s', 'tests/tools', '-p', pattern], label)
        data = (logs / 'stdout.raw').read_bytes() + (logs / 'stderr.raw').read_bytes()
        match = re.search(rb'Ran (\d+) tests? in ', data)
        need(match and (int(match[1]) == count if count is not None else int(match[1]) > 0)
             and re.search(rb'(?m)^OK\s*$', data) and b'skipped=' not in data, 'Incomplete fixture execution')
    return 'SourceVerified'


def profiles(run, root, mismatch):
    rows, kits, compilers = [], set(), set()
    for identity in qualification.PROFILES:
        tree = packaging.reject_reparse(root / identity).resolve()
        need(not (tree == run.output or tree in run.output.parents or run.output in tree.parents), 'Output overlaps a profile')
        try:
            row, cache, _ = qualification.inspect_profile(ROOT, tree, identity)
            kit = Path(cache['Qt5Core_DIR']).parents[2]
            for name in ('CMAKE_COMMAND', 'CMAKE_MAKE_PROGRAM', 'CMAKE_CXX_COMPILER'):
                need(Path(cache[name]).is_file(), 'Profile tool unavailable: ' + name, 'PendingEnvironment')
            kits.add(kit.resolve()); compilers.add(Path(cache['CMAKE_CXX_COMPILER']).resolve().parent)
        except OSError as error:
            raise Stop('PendingEnvironment', 'Profile environment unavailable: ' + str(error)) from error
        except (KeyError, ValueError) as error:
            raise Stop(mismatch, 'Profile precondition differs: ' + str(error)) from error
        rows.append(row)
    need(len(kits) == 1, 'One matching Qt kit is required', 'PendingEnvironment')
    for key in list(run.env):
        if key.upper().startswith(('QT', 'QML')):
            del run.env[key]
    kit = kits.pop()
    run.env['PATH'] = os.pathsep.join([str(kit / 'bin'), *sorted(map(str, compilers)), os.environ.get('PATH', '')])
    run.env['QT_PLUGIN_PATH'] = str(kit / 'plugins')
    run.env['QT_QPA_PLATFORM_PLUGIN_PATH'] = str(kit / 'plugins/platforms')
    return rows


def candidate(run, args):
    test = tool(args.ctest)
    before = profiles(run, args.build_root.resolve(), 'PendingEnvironment')
    run.result['profilesBefore'] = before
    command = [sys.executable, '-B', str(ROOT / 'tools/export_native_qualification.py'), '--repo-root', str(ROOT)]
    run.call(command + ['--qualify', '--build-root', str(args.build_root.resolve()), '--run-dir', str(run.output / 'capture'), '--ctest', test], 'qualify')
    need(qualification.read_json(run.output / 'capture/qualification.json')['status'] == 'Completed', 'Capture not completed')
    run.call(command + ['--evidence', str(run.output / 'capture/qualification.json'), '--output', str(run.output / 'native-business-candidate.json')], 'export')
    need(profiles(run, args.build_root.resolve(), 'Failed') == before, 'Measured profiles changed')
    run.result['candidateSha256'] = sha(run.output / 'native-business-candidate.json')
    return 'PendingReview'


def product(run, args):
    need((ROOT / CATALOG).is_file() and sha(ROOT / CATALOG) == CATALOG_SHA, 'Fixed reviewed catalog differs', 'PendingProductValidation')
    test = tool(args.ctest)
    before = profiles(run, args.build_root.resolve(), 'PendingProductValidation')
    need(before == qualification.read_json(ROOT / CATALOG)['profiles'], 'Current bytes/profile are not reviewed', 'PendingProductValidation')
    run.result['profilesBefore'] = before
    run.result['tests'] = {p['id']: ctest(run, test, args.build_root / p['id'], p['id'], PRODUCTS) for p in before}
    need(profiles(run, args.build_root.resolve(), 'Failed') == before, 'Reviewed profile changed during product tests')
    return 'ProductVerified'


def release(run, args):
    archive = packaging.regular(args.zip).resolve()
    need(archive.stat().st_nlink == 1 and sha(archive) == args.zip_sha, 'External ZIP anchor/link differs')
    extracted = run.output / 'extracted'; extracted.mkdir()
    seen, inventory, roots = set(), {}, set()
    with zipfile.ZipFile(archive) as zipped:
        entries = zipped.infolist()
        need(len(entries) <= 10000 and sum(e.file_size for e in entries) <= 1073741824, 'ZIP inventory budget exceeded')
        for entry in entries:
            name = packaging.relative_path(entry.filename.rstrip('/'))
            mode = stat.S_IFMT(entry.external_attr >> 16)
            need(name.casefold() not in seen and not entry.flag_bits & 1 and mode in (0, stat.S_IFREG, stat.S_IFDIR)
                 and not entry.external_attr & 0x400, 'Duplicate/case-colliding, encrypted or linked ZIP entry')
            seen.add(name.casefold()); roots.add(PurePosixPath(name).parts[0])
            target = extracted / name
            if entry.is_dir():
                target.mkdir(parents=True, exist_ok=True); continue
            target.parent.mkdir(parents=True, exist_ok=True)
            with zipped.open(entry) as reader, target.open('xb') as writer:
                shutil.copyfileobj(reader, writer)
            need(target.stat().st_size == entry.file_size, 'Extracted ZIP length differs')
            inventory[name] = {'bytes': entry.file_size, 'sha256': sha(target)}
    need(len(roots) == 1, 'One package root required')
    package = extracted / roots.pop()
    need(package.is_dir() and sha(package / 'build-manifest.json') == args.manifest_sha, 'External manifest anchor differs')
    verifier = ROOT / VERIFIER
    need(sha(verifier) == VERIFIER_SHA == sha(package / 'verification/verify-package.ps1'), 'Repository/package stock verifier differs')
    run.call([tool(str(Path(os.environ.get('SystemRoot', r'C:\Windows')) / 'System32/WindowsPowerShell/v1.0/powershell.exe')),
              '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', str(verifier), '-PackageRoot', str(package),
              '-ExpectedManifestSha256', args.manifest_sha, '-ReportPath', str(run.output / 'stock-verification.json')], 'stock-verifier')
    need(qualification.read_json(run.output / 'stock-verification.json')['verified'] is True, 'Stock verifier refused')
    packaging.verify_payload(extracted, {n: facts['sha256'] for n, facts in inventory.items()})
    need(all((extracted / n).stat().st_size == facts['bytes'] for n, facts in inventory.items())
         and sha(archive) == args.zip_sha, 'Package/ZIP changed during verification')
    run.result.update(zipSha256=args.zip_sha, manifestSha256=args.manifest_sha, inventory=inventory, stockVerifierSha256=VERIFIER_SHA)
    return 'StaticVerified'


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='stage', required=True)
    for name in ('source', 'candidate', 'product', 'release'):
        child = sub.add_parser(name); child.add_argument('--output', type=Path, required=True)
        if name == 'source':
            child.add_argument('--preset', choices=('core-gcc-debug', 'core-gcc-release', 'core-msvc-debug', 'core-msvc-release'), required=True)
            child.add_argument('--cmake', default='cmake'); child.add_argument('--ninja', default='ninja')
        if name in ('source', 'candidate', 'product'): child.add_argument('--ctest', default='ctest')
        if name in ('candidate', 'product'): child.add_argument('--build-root', type=Path, required=True)
        if name == 'release':
            child.add_argument('--zip', type=Path, required=True)
            child.add_argument('--zip-sha', required=True); child.add_argument('--manifest-sha', required=True)
    args = parser.parse_args(argv)
    output = packaging.reject_reparse(args.output).resolve()
    if output.exists() or output == ROOT or ROOT.is_relative_to(output) or (output.is_relative_to(ROOT) and not output.is_relative_to(ROOT / 'build')):
        print('Refused existing/overlapping/non-build output', file=sys.stderr); return 2
    if args.stage in ('candidate', 'product'):
        for name in qualification.PROFILES:
            tree = packaging.reject_reparse(args.build_root / name).resolve()
            if output == tree or tree in output.parents or output in tree.parents:
                print('Refused profile/output overlap', file=sys.stderr); return 2
    output.mkdir(parents=True); run = Run(output, args.stage)
    try:
        before = inputs(output); run.result['inputsBefore'] = before
        run.result['state'] = {'source': source, 'candidate': candidate, 'product': product, 'release': release}[args.stage](run, args)
    except Stop as error:
        run.result.update(state=error.state, error=str(error))
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        run.result.update(state='Failed', error=repr(error))
    finally:
        if 'inputsBefore' in run.result:
            try:
                run.result['inputsAfter'] = inputs(output)
                need(run.result['inputsAfter'] == run.result['inputsBefore'], 'CI/source inputs changed during execution')
            except (OSError, ValueError, Stop) as error: run.result.update(state='Failed', error=str(error))
        write(output / 'result.json', run.result)
        print(json.dumps(run.result, indent=2, ensure_ascii=False), flush=True)
    return 0 if run.result['state'] in ('SourceVerified', 'PendingReview', 'ProductVerified', 'StaticVerified') else 2


if __name__ == '__main__':
    raise SystemExit(main())