"""Finite orchestration gates. File fixtures and one Python exit fixture never approve a runtime."""
import argparse
import contextlib
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
import ci


class CiGates(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='cordis-ci-')
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name).resolve()

    def run_context(self, name='output'):
        path = self.base / name
        path.mkdir()
        return ci.Run(path, 'fixture')

    def invoke(self, arguments, **patches):
        with contextlib.ExitStack() as stack:
            stack.enter_context(contextlib.redirect_stdout(io.StringIO()))
            stack.enter_context(contextlib.redirect_stderr(io.StringIO()))
            stack.enter_context(mock.patch.object(ci, 'inputs', return_value={'fixture': 'stable'}))
            for name, value in patches.items():
                stack.enter_context(mock.patch.object(ci, name, value))
            return ci.main(arguments)

    def native_args(self):
        return argparse.Namespace(build_root=self.base / 'profiles', ctest='ctest')

    def selection(self, names):
        directory = self.base / 'selected'
        directory.mkdir()
        (directory / 'stdout.raw').write_text(json.dumps({'tests': [{'name': n, 'properties': []} for n in names]}), encoding='utf-8')
        return directory

    def archive(self, entries):
        path = self.base / 'fixture.zip'
        with zipfile.ZipFile(path, 'x') as zipped:
            for name, raw in entries:
                zipped.writestr(name, raw)
        return path

    def release_args(self, archive, manifest='0' * 64):
        return argparse.Namespace(zip=archive, zip_sha=ci.sha(archive), manifest_sha=manifest)

    def static_fixture(self):
        manifest = b'{"fileFixtureOnly":true}\n'
        archive = self.archive([('fixture/build-manifest.json', manifest),
                                ('fixture/verification/verify-package.ps1', (ci.ROOT / ci.VERIFIER).read_bytes())])
        return self.release_args(archive, ci.hashlib.sha256(manifest).hexdigest())

    def test_existing_output_never_overwrites_original(self):
        output = self.base / 'prior'; output.mkdir()
        (output / 'original').write_bytes(b'preserve')
        code = self.invoke(['source', '--preset', 'core-msvc-debug', '--output', str(output)])
        self.assertNotEqual(code, 0)
        self.assertEqual(list(output.iterdir()), [output / 'original'])
        self.assertEqual((output / 'original').read_bytes(), b'preserve')

    def test_profile_output_overlap_refuses_before_directory_creation(self):
        tree = self.base / 'profiles'
        output = tree / 'ela-msvc-release' / 'unsafe-output'
        code = self.invoke(['candidate', '--build-root', str(tree), '--output', str(output)])
        self.assertNotEqual(code, 0)
        self.assertFalse(tree.exists())

    def test_missing_tool_is_pending_environment_with_nonzero_exit(self):
        output = self.base / 'source'
        code = self.invoke(['source', '--preset', 'core-msvc-debug', '--output', str(output), '--cmake', 'cordis-tool-does-not-exist'])
        self.assertNotEqual(code, 0)
        result = json.loads((output / 'result.json').read_bytes())
        self.assertEqual(result['state'], 'PendingEnvironment')
        self.assertEqual(result['commands'], [])
        self.assertFalse(result['runtimeApproved'])

    def test_changed_inputs_cannot_keep_a_success_state(self):
        output = self.base / 'source'
        with mock.patch.object(ci, 'inputs', side_effect=[{'source': 'before'}, {'source': 'after'}]), \
             mock.patch.object(ci, 'source', return_value='SourceVerified'), contextlib.redirect_stdout(io.StringIO()):
            code = ci.main(['source', '--preset', 'core-gcc-debug', '--output', str(output)])
        self.assertNotEqual(code, 0)
        self.assertEqual(json.loads((output / 'result.json').read_bytes())['state'], 'Failed')

    def test_actual_child_failure_retains_pid_exit_and_raw_without_success(self):
        run = self.run_context()
        with contextlib.redirect_stdout(io.StringIO()), self.assertRaises(ci.Stop) as caught:
            run.call([sys.executable, '-c', 'import sys;print("out");print("err",file=sys.stderr);sys.exit(7)'], 'actual-exit')
        self.assertEqual(caught.exception.state, 'Failed')
        record = run.result['commands'][0]
        self.assertGreater(record['pid'], 0)
        self.assertEqual(record['exitCode'], 7)
        self.assertTrue(record['naturalExitObserved'])
        self.assertEqual(Path(record['logs']['stdout']['path']).read_bytes().strip(), b'out')
        self.assertEqual(Path(record['logs']['stderr']['path']).read_bytes().strip(), b'err')

    def test_candidate_only_calls_public_capture_then_new_export(self):
        run = self.run_context(); run.call = mock.Mock()
        with mock.patch.object(ci, 'profiles', return_value=[{'fixture': True}]), mock.patch.object(ci, 'tool', return_value='ctest'), \
             mock.patch.object(ci.qualification, 'read_json', return_value={'status': 'Completed'}), mock.patch.object(ci, 'sha', return_value='a' * 64):
            self.assertEqual(ci.candidate(run, self.native_args()), 'PendingReview')
        calls = [c.args[0] for c in run.call.call_args_list]
        self.assertEqual(len(calls), 2)
        self.assertIn('--qualify', calls[0]); self.assertIn('--evidence', calls[1])
        self.assertEqual(calls[1][-1], str(run.output / 'native-business-candidate.json'))
        self.assertNotIn(str(ci.ROOT / ci.CATALOG), calls[1])
        self.assertFalse(run.result['runtimeApproved'])

    def test_failed_capture_never_reaches_export(self):
        run = self.run_context(); run.call = mock.Mock(side_effect=ci.Stop('Failed', 'capture exit1'))
        with mock.patch.object(ci, 'profiles', return_value=[]), mock.patch.object(ci, 'tool', return_value='ctest'), self.assertRaises(ci.Stop):
            ci.candidate(run, self.native_args())
        self.assertEqual(run.call.call_count, 1)

    def test_unreviewed_product_bytes_never_run_or_build(self):
        run = self.run_context(); run.call = mock.Mock()
        with mock.patch.object(ci, 'sha', return_value=ci.CATALOG_SHA), mock.patch.object(ci, 'tool', return_value='ctest'), \
             mock.patch.object(ci, 'profiles', return_value=[{'different': True}]), \
             mock.patch.object(ci.qualification, 'read_json', return_value={'profiles': []}), self.assertRaises(ci.Stop) as caught:
            ci.product(run, self.native_args())
        self.assertEqual(caught.exception.state, 'PendingProductValidation')
        run.call.assert_not_called()

    def test_changed_fixed_catalog_has_no_fallback(self):
        run = self.run_context(); run.call = mock.Mock()
        with mock.patch.object(ci, 'sha', return_value='0' * 64), self.assertRaises(ci.Stop) as caught:
            ci.product(run, self.native_args())
        self.assertEqual(caught.exception.state, 'PendingProductValidation')
        run.call.assert_not_called()

    def test_product_selection_cannot_drop_a_positive_case(self):
        selected = self.selection(sorted(ci.PRODUCTS)[:-1]); run = self.run_context(); run.call = mock.Mock(return_value=selected)
        with self.assertRaises(ci.Stop): ci.ctest(run, 'ctest', self.base, 'product', ci.PRODUCTS)
        self.assertEqual(run.call.call_count, 1)

    def test_passed_names_must_equal_actual_selection(self):
        selected = self.selection(sorted(ci.PRODUCTS)); actual = self.base / 'actual'; actual.mkdir()
        (actual / 'stdout.raw').write_bytes(b'1/6 Test #1: p5.platform_product_catalog .... Passed 0.01 sec\n')
        run = self.run_context(); run.call = mock.Mock(side_effect=[selected, actual])
        with self.assertRaises(ci.Stop): ci.ctest(run, 'ctest', self.base, 'product', ci.PRODUCTS)

    def test_zip_escape_and_case_collision_refuse_before_any_verifier(self):
        for index, entries in enumerate([[('../escape', b'x')], [('fixture/A', b'x'), ('fixture/a', b'y')]]):
            with self.subTest(index=index):
                archive = self.archive(entries); run = self.run_context('out' + str(index)); run.call = mock.Mock()
                with self.assertRaises((ValueError, ci.Stop)): ci.release(run, self.release_args(archive))
                run.call.assert_not_called(); self.assertFalse((self.base / 'escape').exists())
                archive.unlink()

    def test_hardlinked_input_is_refused_before_extraction(self):
        archive = self.archive([('fixture/file', b'fixture')]); alias = self.base / 'alias.zip'; os.link(archive, alias)
        run = self.run_context(); run.call = mock.Mock()
        with self.assertRaises(ci.Stop): ci.release(run, self.release_args(archive))
        self.assertFalse((run.output / 'extracted').exists()); run.call.assert_not_called()

    def test_release_executes_only_repository_stock_verifier_and_stays_static(self):
        args = self.static_fixture(); run = self.run_context()
        def verified(*_): ci.write(run.output / 'stock-verification.json', {'verified': True})
        run.call = mock.Mock(side_effect=verified)
        with mock.patch.object(ci, 'tool', return_value='powershell'):
            self.assertEqual(ci.release(run, args), 'StaticVerified')
        argv = run.call.call_args.args[0]
        self.assertEqual(argv[argv.index('-File') + 1], str(ci.ROOT / ci.VERIFIER))
        self.assertFalse(run.result['runtimeApproved']); self.assertEqual(run.result['gui'], 'Pending')

    def test_zip_change_during_static_verification_cannot_pass(self):
        args = self.static_fixture(); run = self.run_context()
        def changed(*_):
            ci.write(run.output / 'stock-verification.json', {'verified': True})
            with args.zip.open('ab') as stream: stream.write(b'changed')
        run.call = mock.Mock(side_effect=changed)
        with mock.patch.object(ci, 'tool', return_value='powershell'), self.assertRaises(ci.Stop): ci.release(run, args)


    def test_started_receipt_failure_still_holds_actual_child_until_natural_exit(self):
        run = self.run_context(); original = ci.write
        def fail_started(path, value):
            if Path(path).name == 'started.json': raise OSError('one receipt write failed')
            return original(path, value)
        with mock.patch.object(ci, 'write', side_effect=fail_started), contextlib.redirect_stdout(io.StringIO()), self.assertRaises(ci.Stop):
            run.call([sys.executable, '-c', 'import time;time.sleep(.05);print("natural")'], 'started-failure')
        record = run.result['commands'][0]
        self.assertEqual(record['exitCode'], 0)
        self.assertTrue(record['naturalExitObserved'])
        self.assertIn('one receipt write failed', record['interrupted'])
        self.assertEqual(Path(record['logs']['stdout']['path']).read_bytes().strip(), b'natural')

    def test_extra_file_created_by_static_verifier_cannot_pass(self):
        args = self.static_fixture(); run = self.run_context()
        def appended(*_):
            ci.write(run.output / 'stock-verification.json', {'verified': True})
            (run.output / 'extracted/fixture/unlisted').write_bytes(b'unlisted')
        run.call = mock.Mock(side_effect=appended)
        with mock.patch.object(ci, 'tool', return_value='powershell'), self.assertRaises(ValueError): ci.release(run, args)



    def test_source_loop_creates_three_fixed_windows_legal_suite_directories(self):
        """Directory creation is real; CMake/CTest/unit outputs are controlled file fixtures."""
        run = self.run_context(); directories = []; suite_labels = []
        def directory_only(argv, label):
            directory = run.output / ('%02d-' % len(directories) + label)
            directory.mkdir(); directories.append(directory)
            if '--preset' in argv:
                compiler = run.output / 'build/CMakeFiles/fixture/CMakeCXXCompiler.cmake'
                compiler.parent.mkdir(parents=True)
                compiler.write_text('set(CMAKE_CXX_COMPILER_ID "fixture")\nset(CMAKE_CXX_COMPILER_VERSION "1")\nset(CMAKE_CXX_SIZEOF_DATA_PTR "8")\n', encoding='utf-8')
            stdout, stderr = b'', b''
            if '-m' in argv:
                suite_labels.append(label)
                count = {'test_preview_*packaging.py': 55, 'test_preview_update.py': 24, 'test_ci.py': 1}[argv[-1]]
                stderr = ('Ran %d tests in 0.001s\n\nOK\n' % count).encode()
            (directory / 'stdout.raw').write_bytes(stdout); (directory / 'stderr.raw').write_bytes(stderr)
            return directory
        run.call = mock.Mock(side_effect=directory_only)
        args = argparse.Namespace(cmake='fixture', ctest='fixture', ninja='fixture', preset='core-msvc-debug')
        with mock.patch.object(ci, 'tool', return_value='fixture'), mock.patch.object(ci, 'ctest', return_value={'fileFixtureOnly': True}):
            self.assertEqual(ci.source(run, args), 'SourceVerified')
        self.assertEqual(suite_labels, ['packaging-units', 'update-units', 'ci-units'])
        self.assertTrue(all(path.is_dir() for path in directories))



if __name__ == '__main__':
    unittest.main()