"""Use temporary text copies and Python children; never launch a preview product."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import verify_preview_failures as failures


def put(root, relative, data=b"fixture"):
    path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return path


class PreviewFailuresTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="cordis-failures-unit-")
        self.root = Path(self.temporary.name)
        self.package = self.root / "source-package"
        self.package.mkdir()
        put(self.package, failures.PACKAGE_NAME, b"not-an-executable-fixture")
        put(self.package, "cordis_core.dll", b"not-a-library-fixture")
        library = put(self.package, failures.COUNTER_B, b"counter fixture")
        put(self.package, failures.MANIFEST_B, json.dumps({"abi": {"major": 1, "minor": 0},
            "sha256": failures.sha(library)}).encode("utf-8"))
        put(self.package, "verification/verify-package.ps1",
            (ROOT / "delivery/windows/verification/verify-package.ps1").read_bytes())
        put(self.package, "verification/preview-acl.ps1", failures.ACL_SCRIPT.read_bytes())
        for index in range(126):
            put(self.package, "materials/file-%03d.txt" % index, ("file %d" % index).encode())
        rows = [{"path": p.relative_to(self.package).as_posix(), "role": "test-payload", "bytes": p.stat().st_size,
                 "sha256": failures.sha(p), "source": {"path": str(p), "sha256": failures.sha(p)}}
                for p in sorted(self.package.rglob("*")) if p.is_file()]
        value = {"schema": "cordis.preview.package/1", "deliveryVersion": "0.1.5-preview.1", "applicationVersion": "0.1.5",
                 "scope": "internal-technical-preview", "platform": "windows-x64", "profile": {}, "qualification": {}, "files": rows}
        manifest = put(self.package, "build-manifest.json", (json.dumps(value, ensure_ascii=False, indent=2) + "\n").encode())
        sums = {row["path"]: row["sha256"] for row in rows}
        sums["build-manifest.json"] = failures.sha(manifest)
        put(self.package, "SHA256SUMS.txt", "".join(h + "  " + p + "\n" for p, h in sorted(sums.items())).encode())
        self.before = failures.snapshot(self.package)
        self.assertEqual(len(self.before), 134)
        self.baseline = self.root / "baseline"
        self.baseline.mkdir()
        put(self.baseline, "session.yaml", b"formatVersion: 1\nmodules: {counter: {manifest: saved-hash/module.json}}\n")
        put(self.baseline, "store/saved-hash/module.json", b"actual-locator-fixture")
        self.baseline_before = failures.snapshot(self.baseline)

    def tearDown(self):
        self.temporary.cleanup()

    def arguments(self, **overrides):
        values = dict(package_root=str(self.package), output=str(self.root / "output"),
                      baseline_data_root=str(self.baseline), expected_manifest_sha256=failures.sha(self.package / "build-manifest.json"),
                      timeout_seconds=5, prepare_only=True, prepare_acl_only=False)
        values.update(overrides)
        return argparse.Namespace(**values)

    @staticmethod
    def fake_verify(package, output, expected_manifest=None):
        digest = failures.sha(package / "build-manifest.json")
        if expected_manifest is not None:
            failures.require(digest == expected_manifest, "test anchor differs")
        failures.write_json(output / "verification.json", {"unitFixture": True, "verified": True})
        return digest

    def prepare(self, **overrides):
        with mock.patch.object(failures, "verify_source", side_effect=self.fake_verify) as verify, \
             mock.patch.object(failures, "shared_observation", return_value={"unitFixture": True}), \
             mock.patch.object(failures, "observe_child", side_effect=AssertionError("product launch forbidden")):
            report = failures.run(self.arguments(**overrides))
        return report, verify.call_count

    def test_preparation_copies_exact_bytes_and_never_rewrites_saved_locators(self):
        report, count = self.prepare()
        self.assertTrue(report["prepared"])
        self.assertFalse(report["passed"])
        self.assertTrue(report["preparationOnly"])
        self.assertEqual(count, 6)
        self.assertEqual(failures.snapshot(self.package), self.before)
        self.assertEqual(failures.snapshot(self.baseline), self.baseline_before)
        output = self.root / "output"
        self.assertFalse((output / "incomplete.json").exists())
        for case in failures.CASES:
            self.assertEqual(failures.snapshot(output / case / "data"), self.baseline_before)
            changed = failures.snapshot(output / case / "application")
            differences = {name for name in set(changed) | set(self.before) if changed.get(name) != self.before.get(name)}
            self.assertEqual(len(differences), 1)
        registered = json.loads(Path(report["aclRegistration"]).read_text(encoding="utf-8"))
        self.assertEqual(registered["scopeIdentity"], failures.file_identity(report["aclScopeParent"]))
        self.assertEqual(registered["createdBy"]["path"], str(failures.ACL_SCRIPT))
        self.assertEqual(registered["createdBy"]["sha256"], failures.sha(failures.ACL_SCRIPT))
        self.assertFalse(report["aclPreparation"]["protectionApplied"])
        self.assertEqual(registered["targets"]["application"]["initialFiles"], self.before)
        self.assertEqual(registered["targets"]["data"]["initialFiles"], self.baseline_before)

    def test_acl_only_preparation_has_no_negative_or_product_launches(self):
        report, count = self.prepare(prepare_only=False, prepare_acl_only=True)
        self.assertTrue(report["prepared"])
        self.assertEqual(report["cases"], [])
        self.assertEqual(count, 1)

    def test_baseline_evidence_is_not_copied_or_reinterpreted_as_user_data(self):
        put(self.baseline, "old-evidence/report.json", b"historical receipt")
        report, _ = self.prepare(prepare_only=False, prepare_acl_only=True)
        self.assertTrue(report["prepared"])
        self.assertEqual(failures.snapshot(Path(report["aclData"])), self.baseline_before)
        self.assertEqual((self.baseline / "old-evidence/report.json").read_bytes(), b"historical receipt")

    def test_no_baseline_does_not_claim_old_data_preservation(self):
        report, _ = self.prepare(baseline_data_root=None, prepare_acl_only=True, prepare_only=False)
        self.assertTrue(report["prepared"])
        self.assertFalse(report["oldDataPreservationClaimed"])
        registered = json.loads(Path(report["aclRegistration"]).read_text(encoding="utf-8"))
        self.assertEqual(registered["targets"]["data"]["initialFiles"], {})

    def test_external_manifest_anchor_is_required_before_creating_output(self):
        with self.assertRaisesRegex(ValueError, "external manifest"):
            failures.run(self.arguments(expected_manifest_sha256=None))
        self.assertFalse((self.root / "output").exists())

    def test_live_first_case_stops_remaining_cases_without_reading_live_copy(self):
        live = False
        snapshot = failures.snapshot

        def checked_snapshot(path):
            if live and "missing-source-counter-b" in str(path):
                raise AssertionError("live application/data must not be hashed")
            return snapshot(path)

        def unknown(*args, **kwargs):
            nonlocal live
            live = True
            return {"exitObserved": False, "livePid": 123, "processTerminatedByTool": False}

        with mock.patch.object(failures, "verify_source", side_effect=self.fake_verify), \
             mock.patch.object(failures, "shared_observation", return_value={"unitFixture": True}), \
             mock.patch.object(failures, "observe_child", side_effect=unknown) as observer, \
             mock.patch.object(failures, "snapshot", side_effect=checked_snapshot):
            report = failures.run(self.arguments(prepare_only=False))
        self.assertFalse(report["passed"])
        self.assertEqual(observer.call_count, 1)
        self.assertEqual(len(report["cases"]), 1)
        self.assertEqual(len(report["remainingCasesNotStarted"]), 4)
        self.assertNotIn("dataAfter", report["cases"][0])
        self.assertTrue((self.root / "output/incomplete.json").exists())

    def test_existing_or_overlapping_output_and_bad_baseline_are_refused(self):
        with self.assertRaises(ValueError):
            failures.run(self.arguments(output=str(self.package / "nested")))
        with self.assertRaises(ValueError):
            failures.run(self.arguments(output=str(self.root)))
        with self.assertRaises(ValueError):
            failures.run(self.arguments(baseline_data_root=str(self.package)))
        self.assertEqual(failures.snapshot(self.package), self.before)

    def test_copy_rejects_changed_source_and_does_not_overwrite_destination(self):
        source = self.package / "materials/file-000.txt"
        source.write_bytes(b"changed")
        with self.assertRaisesRegex(ValueError, "recorded baseline"):
            failures.copy_tree(self.package, self.root / "copy", self.before)
        self.assertFalse((self.root / "copy").exists())
        existing = self.root / "existing"
        existing.mkdir()
        put(existing, "keep.txt", b"keep")
        with self.assertRaises(FileExistsError):
            failures.copy_tree(self.package, existing, failures.snapshot(self.package))
        self.assertEqual((existing / "keep.txt").read_bytes(), b"keep")

    def test_hard_links_and_directory_junctions_are_refused(self):
        linked = self.root / "hard-link.txt"
        os.link(self.package / "materials/file-000.txt", linked)
        with self.assertRaisesRegex(ValueError, "hard-linked"):
            failures.snapshot(self.package)
        linked.unlink()
        junction = self.root / "junction"
        result = subprocess.run(["cmd.exe", "/d", "/c", "mklink", "/J", str(junction), str(self.package)],
                                capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        try:
            with self.assertRaisesRegex(ValueError, "reparse"):
                failures.safe_path(junction / "materials/file-000.txt")
        finally:
            os.rmdir(junction)

    def test_child_environment_removes_developer_paths_without_claiming_knownfolder_isolation(self):
        actual = failures.child_environment({"SystemRoot": r"C:\Windows", "Path": "E:/Qt/bin;VS/bin",
            "QT_PLUGIN_PATH": "E:/Qt/plugins", "QML2_IMPORT_PATH": "other", "QTDIR": "E:/Qt",
            "INCLUDE": "sdk", "LIB": "sdk", "CL": "/MDd", "VSCMD_VER": "v", "CMAKE_PREFIX_PATH": "Qt",
            "LOCALAPPDATA": "actual-user-data", "APPDATA": "actual-user-roaming"})
        self.assertEqual(actual["QT_QPA_PLATFORM"], "windows")
        self.assertNotIn("E:/Qt", actual["PATH"])
        self.assertNotIn("INCLUDE", actual)
        self.assertNotIn("QT_PLUGIN_PATH", actual)
        self.assertNotIn("QML2_IMPORT_PATH", actual)
        self.assertEqual(actual["LOCALAPPDATA"], "actual-user-data")
        self.assertEqual(actual["APPDATA"], "actual-user-roaming")

    def test_nongui_child_captures_both_streams_and_restores_error_mode(self):
        directory = self.root / "child"
        directory.mkdir()
        with failures.inherited_error_mode() as original:
            expected_mode = original["inherited"]
            result = failures.observe_child([sys.executable, "-c", "import sys;print('out');print('error',file=sys.stderr);sys.exit(7)"],
                                            directory, directory, 5, observe=lambda *args: {"unitFixture": True})
            import ctypes
            self.assertEqual(ctypes.windll.kernel32.GetErrorMode(), expected_mode)
        self.assertEqual(result["exitCode"], 7)
        self.assertTrue(result["captureComplete"])
        self.assertFalse(result["processTerminatedByTool"])
        self.assertEqual((directory / "stdout.raw").read_bytes().strip(), b"out")
        self.assertEqual((directory / "stderr.raw").read_bytes().strip(), b"error")

    def test_timeout_is_unknown_live_without_kill_retry_or_data_reads(self):
        directory = self.root / "live-child"
        directory.mkdir()
        captured = []
        real_popen = subprocess.Popen

        def launch(*args, **kwargs):
            child = real_popen(*args, **kwargs)
            captured.append(child)
            return child

        try:
            with mock.patch.object(failures.subprocess, "Popen", side_effect=launch):
                result = failures.observe_child([sys.executable, "-c", "import time;time.sleep(30)"], directory,
                                                directory, 0.15, observe=lambda *args: {"unitFixture": True})
            self.assertEqual(len(captured), 1)
            self.assertEqual(result["status"], "unknown-live")
            self.assertEqual(result["livePid"], captured[0].pid)
            self.assertIsNone(captured[0].poll())
            self.assertEqual(result["retryCount"], 0)
            self.assertFalse(result["processTerminatedByTool"])
            self.assertNotIn("logs", result)
        finally:
            # This test alone owns the exact temporary Python child; no preview is ever launched.
            for child in captured:
                child.kill()
                child.wait(timeout=5)
            time.sleep(0.1)

    def test_output_capture_budget_discards_excess_without_blocking_child(self):
        directory = self.root / "large-child"
        directory.mkdir()
        with mock.patch.object(failures, "MAX_LOG_BYTES", 1024):
            result = failures.observe_child([sys.executable, "-c", "import sys;sys.stdout.write('x'*8192)"],
                                            directory, directory, 5, observe=lambda *args: {"unitFixture": True})
        self.assertEqual(result["exitCode"], 0)
        self.assertEqual((directory / "stdout.raw").stat().st_size, 1024)
        self.assertEqual(result["streams"]["stdout"]["receivedBytes"], 8192)
        self.assertTrue(result["captureTruncated"])

    def invoke_acl(self, action, output, registration=None, target="application"):
        scope = output / "permissions"
        return subprocess.run([failures.powershell(), "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
            "-File", str(failures.ACL_SCRIPT), "-Action", action, "-ScopeParent", str(scope),
            "-Registration", str(registration or scope / "acl-registration.json"), "-Target", target],
            capture_output=True, timeout=40)

    def test_temporary_acl_protect_probe_restore_preserves_all_file_bytes(self):
        report, _ = self.prepare(prepare_acl_only=True, prepare_only=False)
        self.assertTrue(report["prepared"])
        output = self.root / "output"
        application = Path(report["aclApplication"])
        try:
            protected = self.invoke_acl("Protect", output)
            self.assertEqual(protected.returncode, 0, protected.stderr.decode(errors="replace"))
            value = json.loads(protected.stdout.decode("utf-8-sig"))
            self.assertTrue(value["probe"]["passed"])
            self.assertFalse(value["probe"]["deleteProbeUnlinksFile"])
            probed = self.invoke_acl("Probe", output)
            self.assertEqual(probed.returncode, 0, probed.stderr.decode(errors="replace"))
            self.assertEqual(failures.snapshot(application), self.before)
        finally:
            if (Path(report["aclScopeParent"]) / "acl-state-application.json").exists():
                restored = self.invoke_acl("Restore", output)
                self.assertEqual(restored.returncode, 0, restored.stderr.decode(errors="replace"))
                restored_value = json.loads(restored.stdout.decode('utf-8-sig'))
                self.assertEqual(restored_value['comparison']['allowedControlFlagsDifference'], 0x0400)
                self.assertEqual(restored_value['comparison']['mismatches'], [])
                self.assertGreater(restored_value['comparison']['objectCount'], 0)
                self.assertLessEqual(restored_value['comparison']['normalizedObjectCount'],
                                     restored_value['comparison']['objectCount'])
        put(application, "after-restore.txt", b"writable again")

    def test_acl_scope_and_target_identity_are_validated_before_changes(self):
        report, _ = self.prepare(prepare_acl_only=True, prepare_only=False)
        output = self.root / "output"
        source_registration = Path(report["aclRegistration"])
        registered = json.loads(source_registration.read_text(encoding="utf-8"))
        registered["targets"]["application"]["relativePath"] = "../../source-package"
        source_registration.write_text(json.dumps(registered), encoding="utf-8")
        rejected = self.invoke_acl("Protect", output)
        self.assertNotEqual(rejected.returncode, 0)
        self.assertFalse((Path(report["aclScopeParent"]) / "acl-state-application.json").exists())
        self.assertEqual(failures.snapshot(self.package), self.before)

    def prepare_stock(self, scope, anchor=None, script=None):
        return subprocess.run([failures.powershell(), "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
            "-File", str(script or failures.ACL_SCRIPT), "-Action", "Prepare", "-ScopeParent", str(scope),
            "-SourcePackage", str(self.package), "-ExpectedManifestSha256", anchor or failures.sha(self.package / "build-manifest.json"),
            "-BaselineDataRoot", str(self.baseline)], capture_output=True, timeout=60)

    def test_stock_powershell_prepare_rejects_wrong_anchor_before_creating_scope(self):
        scope = self.root / "wrong-anchor"
        result = self.prepare_stock(scope, "0" * 64)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(scope.exists())
        self.assertEqual(failures.snapshot(self.package), self.before)

    def test_stock_powershell_prepare_rejects_existing_scope_and_source_overlap(self):
        scope = self.root / "existing-scope"
        scope.mkdir(); put(scope, "preserve.txt", b"keep")
        self.assertNotEqual(self.prepare_stock(scope).returncode, 0)
        self.assertEqual((scope / "preserve.txt").read_bytes(), b"keep")
        self.assertNotEqual(self.prepare_stock(self.package / "nested").returncode, 0)
        self.assertFalse((self.package / "nested").exists())

    def test_restore_remains_owned_after_same_original_script_is_repaired(self):
        script = self.package / "verification/preview-acl.ps1"
        scope = self.root / "stock-script-scope"
        prepared = self.prepare_stock(scope, script=script)
        self.assertEqual(prepared.returncode, 0, prepared.stderr.decode(errors="replace"))
        arguments = [failures.powershell(), "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File", str(script),
                     "-ScopeParent", str(scope), "-Registration", str(scope / "acl-registration.json"), "-Target", "application"]
        try:
            protected = subprocess.run(arguments + ["-Action", "Protect"], capture_output=True, timeout=40)
            self.assertEqual(protected.returncode, 0, protected.stderr.decode(errors="replace"))
            with script.open("ab") as target:
                target.write(b"\n# This temporary owned script models a recovery fix.\n")
            probe = subprocess.run(arguments + ["-Action", "Probe"], capture_output=True, timeout=40)
            self.assertNotEqual(probe.returncode, 0)
        finally:
            if (scope / "acl-state-application.json").exists():
                restored = subprocess.run(arguments + ["-Action", "Restore"], capture_output=True, timeout=40)
                self.assertEqual(restored.returncode, 0, restored.stderr.decode(errors="replace"))
        self.assertEqual(failures.snapshot(scope / "acl/application"), self.before)

    def test_duplicate_restore_rows_are_rejected_before_any_acl_write(self):
        report, _ = self.prepare(prepare_acl_only=True, prepare_only=False)
        output = self.root / "output"
        scope = Path(report["aclScopeParent"])
        state_path = scope / "acl-state-application.json"
        protected = self.invoke_acl("Protect", output)
        self.assertEqual(protected.returncode, 0, protected.stderr.decode(errors="replace"))
        original = state_path.read_bytes()
        try:
            state = json.loads(original.decode("utf-8-sig"))
            state["objects"][-1] = dict(state["objects"][0])
            state_path.write_text(json.dumps(state), encoding="utf-8")
            refused = self.invoke_acl("Restore", output)
            self.assertNotEqual(refused.returncode, 0)
            self.assertIn(b"Duplicate path/object", refused.stderr)
            self.assertFalse(list(scope.glob("acl-restore-*.json")))
            probe = self.invoke_acl("Probe", output)
            self.assertEqual(probe.returncode, 0, probe.stderr.decode(errors="replace"))
        finally:
            state_path.write_bytes(original)
            restored = self.invoke_acl("Restore", output)
            self.assertEqual(restored.returncode, 0, restored.stderr.decode(errors="replace"))
        self.assertEqual(failures.snapshot(Path(report["aclApplication"])), self.before)


if __name__ == "__main__":
    unittest.main()
