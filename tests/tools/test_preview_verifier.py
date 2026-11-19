"""Exercise the independent Windows PowerShell verifier using isolated value-only packages."""
import copy
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
VERIFIER = ROOT / "delivery/windows/verification/verify-package.ps1"
POWERSHELL = Path(os.environ.get("SystemRoot", "C:/Windows")) / "System32/WindowsPowerShell/v1.0/powershell.exe"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


@unittest.skipUnless(os.name == "nt" and POWERSHELL.is_file(), "Windows PowerShell 5.1 is required")
class PreviewVerifierTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="cordis-preview-verifier-")
        self.addCleanup(self.temporary.cleanup)
        self.work = Path(self.temporary.name)
        self.package = self.work / "包 with spaces"
        self.package.mkdir()
        (self.package / "data").mkdir()
        (self.package / "app.bin").write_bytes(b"fixture payload; never loaded")
        (self.package / "data/说明.txt").write_text("独立完整性校验\n", encoding="utf-8")
        self.document = {
            "schema": "cordis.preview.package/1", "deliveryVersion": "0.1.5-preview.1",
            "applicationVersion": "0.1.5", "scope": "internal-technical-preview", "platform": "windows-x64",
            "profile": {"buildType": "Release"}, "qualification": {"source": "value-only fixture"},
            "files": [{"path": p.relative_to(self.package).as_posix(), "role": "test-payload",
                       "bytes": p.stat().st_size, "sha256": digest(p),
                       "source": {"path": str(self.work / "absent-sources" / p.name), "sha256": digest(p)}}
                      for p in sorted(self.package.rglob("*")) if p.is_file()]
        }
        self.write_manifest()

    def write_manifest(self, document=None, raw=None):
        document = self.document if document is None else document
        manifest = self.package / "build-manifest.json"
        manifest.write_bytes(raw if raw is not None else (json.dumps(document, ensure_ascii=False, indent=2) + "\n").encode())
        self.write_sums(document)

    def write_sums(self, document=None):
        document = self.document if document is None else document
        rows = {row["path"]: row["sha256"] for row in document["files"]}
        rows["build-manifest.json"] = digest(self.package / "build-manifest.json")
        (self.package / "SHA256SUMS.txt").write_bytes("".join(f"{value}  {name}\n" for name, value in sorted(rows.items())).encode())

    def invoke(self, *arguments):
        return subprocess.run([str(POWERSHELL), "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
                               "-File", str(VERIFIER), "-PackageRoot", str(self.package), *map(str, arguments)],
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30, check=False)

    def rejected(self, *arguments):
        result = self.invoke(*arguments)
        self.assertNotEqual(result.returncode, 0, result.stdout.decode(errors="replace"))
        self.assertIn(b"Package verification failed:", result.stderr)

    def test_valid_package_external_anchor_and_report_without_package_writes(self):
        self.assertTrue(all(not Path(row["source"]["path"]).exists() for row in self.document["files"]))
        before = {p.relative_to(self.package).as_posix(): digest(p) for p in self.package.rglob("*") if p.is_file()}
        report = self.work / "verification.json"
        result = self.invoke("-ExpectedManifestSha256", digest(self.package / "build-manifest.json"), "-ReportPath", report)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace"))
        value = json.loads(report.read_text(encoding="utf-8"))
        self.assertTrue(value["verified"] and value["integrityOnly"] and value["externalManifestAnchorProvided"])
        self.assertEqual(value["payloadFiles"], 2)
        self.assertEqual(before, {p.relative_to(self.package).as_posix(): digest(p) for p in self.package.rglob("*") if p.is_file()})
        self.assertIn(b"no publisher trust or plugin approval", result.stdout)

    def test_source_provenance_requires_a_typed_object_with_the_same_digest(self):
        source = self.document["files"][0]["source"]
        for value in ("not-an-object", 123, True, None, {"path": source["path"]},
                      {"path": "", "sha256": source["sha256"]},
                      {"path": 123, "sha256": source["sha256"]},
                      {"path": source["path"], "sha256": "invalid"},
                      {"path": source["path"], "sha256": "0" * 64}):
            with self.subTest(source=value):
                bad = copy.deepcopy(self.document)
                bad["files"][0]["source"] = value
                self.write_manifest(bad)
                self.rejected()

    def test_tampered_missing_and_unlisted_payloads_are_rejected(self):
        payload = self.package / "app.bin"
        original = payload.read_bytes()
        payload.write_bytes(original[:-1] + b"!")
        self.rejected()
        payload.unlink()
        self.rejected()
        payload.write_bytes(original)
        extra = self.package / "unlisted.txt"
        extra.write_bytes(b"not declared")
        self.rejected()

    def test_unsafe_relative_paths_and_case_collisions_are_rejected(self):
        for path in ("../escape", "/absolute", "C:/drive", "data\\backslash", "app.bin:stream", "data/./part",
                     "data//part", "data/trailing.", "data/NUL.txt"):
            with self.subTest(path=path):
                bad = copy.deepcopy(self.document)
                bad["files"][0]["path"] = path
                self.write_manifest(bad)
                self.rejected()
        bad = copy.deepcopy(self.document)
        bad["files"].append({**bad["files"][0], "path": bad["files"][0]["path"].upper()})
        self.write_manifest(bad)
        self.rejected()

    def test_duplicate_json_keys_including_escaped_and_case_variants_are_rejected(self):
        valid = json.dumps(self.document, ensure_ascii=False)
        for duplicate in ('"schema":"cordis.preview.package/1",',
                          '"sch\\u0065ma":"cordis.preview.package/1",',
                          '"Schema":"cordis.preview.package/1",'):
            with self.subTest(duplicate=duplicate):
                self.write_manifest(raw=("{" + duplicate + valid[1:]).encode())
                self.rejected()
        raw = valid.replace('"role": "test-payload"', '"role":"old","role":"test-payload"', 1)
        self.write_manifest(raw=raw.encode())
        self.rejected()

    def test_sum_inventory_hash_and_canonical_line_format_are_checked(self):
        path = self.package / "SHA256SUMS.txt"
        original = path.read_bytes()
        variants = [original.split(b"\n", 1)[1], original + original.split(b"\n", 1)[0] + b"\n",
                    b"0" * 64 + b"  extra.bin\n" + original, original.replace(b"\n", b"\r\n"),
                    original[:-1], b"0" * 64 + original[64:]]
        for data in variants:
            with self.subTest(data=data[:75]):
                path.write_bytes(data)
                self.rejected()

    def test_anchor_schema_scope_platform_and_size_type_are_checked(self):
        self.rejected("-ExpectedManifestSha256", "0" * 64)
        self.rejected("-ExpectedManifestSha256", "not-a-hash")
        for field, value in (("schema", "cordis.preview.package/2"), ("scope", "public"), ("platform", "linux-x64")):
            with self.subTest(field=field):
                bad = copy.deepcopy(self.document)
                bad[field] = value
                self.write_manifest(bad)
                self.rejected()
        bad = copy.deepcopy(self.document)
        bad["files"][0]["bytes"] = float(bad["files"][0]["bytes"])
        self.write_manifest(bad)
        self.rejected()

    def test_report_cannot_write_package_or_overwrite_existing_file(self):
        self.rejected("-ReportPath", self.package / "result.json")
        self.assertFalse((self.package / "result.json").exists())
        report = self.work / "existing.json"
        report.write_bytes(b"original")
        self.rejected("-ReportPath", report)
        self.assertEqual(report.read_bytes(), b"original")

    def test_directory_junctions_cannot_hide_payload_or_report_escapes(self):
        target = self.work / "external"
        target.mkdir()
        junction = self.package / "junction"
        quoted = lambda p: "'" + str(p).replace("'", "''") + "'"
        command = "New-Item -ItemType Junction -Path " + quoted(junction) + " -Target " + quoted(target) + " | Out-Null"
        result = subprocess.run([str(POWERSHELL), "-NoProfile", "-NonInteractive", "-Command", command],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=15, check=False)
        if result.returncode:
            self.skipTest("This Windows environment does not permit creating a test directory junction")
        self.rejected()
        # Remove the junction itself, never the external target directory.
        os.rmdir(junction)
        report_link = self.work / "report-link"
        command = "New-Item -ItemType Junction -Path " + quoted(report_link) + " -Target " + quoted(self.package) + " | Out-Null"
        subprocess.run([str(POWERSHELL), "-NoProfile", "-NonInteractive", "-Command", command],
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=15, check=True)
        try:
            self.rejected("-ReportPath", report_link / "result.json")
            self.assertFalse((self.package / "result.json").exists())
        finally:
            os.rmdir(report_link)


if __name__ == "__main__":
    unittest.main()
