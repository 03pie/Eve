"""Fresh-baseline fixtures are data only; no fixture image or qualification is executed."""
import copy
import hashlib
import json
import os
from pathlib import Path
import unittest
from unittest import mock

import test_preview_packaging as legacy

ROOT, digest, pack, put, put_json = legacy.ROOT, legacy.digest, legacy.pack, legacy.put, legacy.put_json


@unittest.skipUnless(os.name == "nt", "the shared packager checks Windows VERSIONINFO")
class RevalidatedPackagingTests(unittest.TestCase):
    write_cache = legacy.PackagingTests.write_cache
    write_host = legacy.PackagingTests.write_host
    payload = legacy.PackagingTests.payload

    def setUp(self):
        legacy.PackagingTests.setUp(self)
        self.revision = self.args.delivery_version = "0.1.5-preview.3"
        self.args.revalidated = True
        self.args.p5_root = None
        self.args.evidence_root = str(self.p5)
        self.old_catalog = self.root / "qualification/native-business.json"
        put(self.old_catalog, (ROOT / "qualification/native-business.json").read_bytes())
        self.new_catalog = self.root / pack.REVALIDATED_CATALOG
        self.exporter = put(self.root / "tools/export_native_qualification.py",
                            (ROOT / "tools/export_native_qualification.py").read_bytes())
        put(self.root / "cmake/NativeProductCatalog.cmake", b"# original fixed catalog selection\n")
        for name in ("counter", "reporter"):
            put(self.root / ("examples/native_services/" + name + ".cpp"), b"// measured fixture producer\n")
        def git(arguments, **options):
            if arguments[1] == "rev-parse":
                return "1" * 40 + "\n"
            if arguments[1] == "ls-files":
                return ("\0".join(p.relative_to(self.root).as_posix() for p in self.root.rglob("*") if p.is_file()) + "\0").encode()
            return b""
        pack.subprocess.check_output.side_effect = git
        self.captured_sources = pack.current_source_inputs(self.root, self.p5)
        self.bundle = self.p5 / "capture"
        self.bundle.mkdir()
        self.capture_records = []
        def save(source, role):
            source = Path(source)
            relative = "files/" + str(len(self.capture_records) + 1).zfill(5) + "-" + source.name
            copied = put(self.bundle / relative, source.read_bytes())
            record = {"role": role, "sourcePath": source.resolve().as_posix(), "path": relative, "sha256": digest(copied)}
            self.capture_records.append(record)
            return record
        source_record = save(put_json(self.bundle / "source-inputs.json", self.captured_sources), "qualification checkout source snapshot")
        profiles, rows = [], []
        reader = pack.source_reader(self.root)
        for identity, (mode, runtime, asan) in reader.PROFILES.items():
            profile = copy.deepcopy(self.profile)
            profile.update(id=identity, buildType=mode, runtime=runtime, asan=asan, cppStandard=201703,
                           runtimeDefinition="MultiThreadedDebugDLL" if runtime == "MDd" else "MultiThreadedDLL")
            for item in profile["dependencies"]:
                item["role"] = "measured-environment"
            tree = self.build if identity == "ela-msvc-release" else self.work / identity
            profile["products"] = []
            for name in ("counter", "reporter"):
                for variant in ("a", "b"):
                    relative = "native-upgrade/" + name + "-" + variant + "/"
                    library = relative + "cordis_example_" + name + "_" + variant + ".dll"
                    manifest = relative + "module.json"
                    dll = self.build / "bin" / library
                    module = self.build / "bin" / manifest
                    if tree != self.build:
                        dll = put(tree / "bin" / library, dll.read_bytes())
                        module = put(tree / "bin" / manifest, module.read_bytes())
                    producer_source = "examples/native_services/" + name + ".cpp"
                    profile["products"].append({"moduleId": "org.cordis.example." + name,
                        "pluginId": "example." + name, "moduleVersion": "1.1.0" if variant == "a" else "1.2.0",
                        "loader": "c-abi", "unloadPolicy": "explicit", "sha256": digest(dll),
                        "manifestSha256": digest(module), "library": library, "manifest": manifest,
                        "producerSource": producer_source, "producerSha256": digest(self.root / producer_source),
                        "producerCommand": "cl /MD fixture.cpp", "compileCommandSha256": hashlib.sha256(b"cl /MD fixture.cpp").hexdigest()})
                    save(dll, "actual measured Native library")
                    save(module, "actual measured Native manifest")
            for dependency in profile["dependencies"]:
                save(Path(dependency["path"]), "actual measured dependency")
            selected = save(put_json(self.bundle / identity / "selected.json", {"tests": [{"name": name} for name in sorted(reader.TESTS)]}), "actual selected names")
            log = save(put(self.bundle / identity / "ctest.log", self.passed_log(reader.TESTS)), "actual CTest exit/output")
            rows.append({"profile": profile, "buildDirectory": str(tree), "selected": selected,
                         "execution": {"exit": 0, "arguments": ["ctest.exe", "--test-dir", str(tree), "-j", "1"]}, "log": log})
            profiles.append(profile)
        self.capture = {"schema": reader.CAPTURE, "status": "Completed", "checkoutCommit": "1" * 40,
                        "sourceInputs": source_record, "profiles": rows, "files": self.capture_records}
        self.capture_path = put_json(self.bundle / "qualification.json", self.capture)
        catalog = {"schema": reader.SCHEMA, "profiles": profiles, "provenance": {
            "method": "current-capture", "pendingStages": ["M5-e2", "M5-e3"], "exportCheckoutCommit": "1" * 40,
            "exporterSha256": digest(self.exporter), "qualificationSources": [{"stage": reader.CAPTURE,
                "commit": "1" * 40, "inputManifest": (self.bundle / source_record["path"]).resolve().as_posix(),
                "inputManifestSha256": source_record["sha256"]}],
            "evidence": [{"role": r["role"], "path": (self.bundle / r["path"]).resolve().as_posix(), "sha256": r["sha256"]} for r in self.capture_records]}}
        put_json(self.new_catalog, catalog)
        # A fixture models the root's later narrow source-rule adoption; the production exporter is untouched.
        put(self.exporter, self.exporter.read_bytes() + b'\n_original_source_inputs = source_inputs\ndef source_inputs(root, exclude=None):\n    value = _original_source_inputs(root, exclude)\n    name = "qualification/native-business-p6-3-20261123.json"\n    if (root / name).is_file(): value[name] = sha(root / name)\n    return value\n')
        put(self.root / "cmake/NativeProductCatalog.cmake", b"# reviewed fixed versioned catalog selection\n")
        self.sources = pack.current_source_inputs(self.root, self.bundle)
        self.source_path = put_json(self.p5 / "current-sources.json", self.sources)
        self.release_files = {}
        for name in pack.release_paths():
            path = self.qt / "bin" / name if name.startswith("Qt5") else self.build / "bin" / name
            self.release_files[name] = {"bytes": path.stat().st_size, "sha256": digest(path)}
        self.product_path = put_json(self.p5 / "release21.json", self.release_files)
        self.consumer = {"schema": "cordis.preview.revalidated-consumer/1", "qualificationSourceSha256": digest(self.new_catalog),
            "captureSha256": digest(self.capture_path), "sourceInputsSha256": digest(self.source_path),
            "productFilesSha256": digest(self.product_path), "generatedCatalog": {
                "path": "examples/qt_workspace/native-product-catalog/native_catalog_Release.cpp",
                "sha256": digest(self.build / "examples/qt_workspace/native-product-catalog/native_catalog_Release.cpp"),
                "approvedRows": [[p["moduleId"], p["sha256"]] for p in profiles[0]["products"]]},
            "producerBefore": {name: value for name, value in self.release_files.items() if name not in pack.ENTRY_POINTS},
            "producerAfter": {name: value for name, value in self.release_files.items() if name not in pack.ENTRY_POINTS},
            "adoptionChanges": {name: {"before": self.captured_sources.get(name), "after": self.sources.get(name)}
                for name in self.captured_sources.keys() | self.sources.keys() if self.captured_sources.get(name) != self.sources.get(name)},
            "validations": []}
        for row in rows:
            identity = row["profile"]["id"]
            selected = put_json(self.p5 / identity / "product-selected.json", {"tests": [{"name": name,
                "command": [str(Path(row["buildDirectory"]) / "bin/cordis_platform_product_tests.exe"),
                    str(Path(row["buildDirectory"]) / "bin"), name.removeprefix("p5.platform_product_")]}
                for name in sorted(pack.PRODUCT_TESTS)]})
            log = put(self.p5 / identity / "product.log", self.passed_log(pack.PRODUCT_TESTS))
            self.consumer["validations"].append({"profile": identity,
                "command": ["ctest.exe", "--test-dir", row["buildDirectory"], "-j", "1"], "exitCode": 0,
                "selected": self.reference(selected), "log": self.reference(log)})
        self.consumer_path = put_json(self.p5 / "consumer.json", self.consumer)
        self.native_review_path = put_json(self.p5 / "native-review.json", {"schema": "cordis.native-business.review/1", "passed": True,
            "candidateSha256": digest(self.new_catalog), "captureSha256": digest(self.capture_path)})
        self.fresh_path = self.assets / pack.REVALIDATED_ASSET
        self.fresh = {"schema": pack.REVALIDATED_SCHEMA, "implementationCommit": "1" * 40,
            "qualificationSource": {"path": pack.REVALIDATED_CATALOG, "sha256": digest(self.new_catalog)},
            "evidence": {"capture": self.reference(self.capture_path), "qualificationReview": self.reference(self.native_review_path),
                "sourceInputs": self.reference(self.source_path), "productFiles": self.reference(self.product_path),
                "consumer": self.reference(self.consumer_path)}}
        self.fresh_review_path = self.p5 / "fresh-review.json"
        self.write_fresh()

    @staticmethod
    def passed_log(names):
        return "".join(f"Test #{i}: {name} Passed\n" for i, name in enumerate(sorted(names), 1)).encode()

    def reference(self, path):
        return {"path": path.relative_to(self.p5).as_posix(), "sha256": digest(path)}

    def write_fresh(self):
        self.fresh["evidence"]["consumer"] = self.reference(put_json(self.consumer_path, self.consumer))
        candidate = {k: v for k, v in self.fresh.items() if k != "review"}
        review = {"schema": "cordis.preview.revalidated-review/1", "passed": True,
                  "candidateSha256": hashlib.sha256(pack.canonical(candidate)).hexdigest(),
                  **{role + "Sha256": self.fresh["evidence"][role]["sha256"] for role in ("capture", "sourceInputs", "productFiles", "consumer")}}
        self.fresh["review"] = self.reference(put_json(self.fresh_review_path, review))
        put_json(self.fresh_path, self.fresh)

    def load(self):
        return pack.load_revalidated_baseline(self.p5)

    def test_fresh_roundtrip_uses_one_copy_pipeline_and_no_legacy_claims(self):
        receipt = pack.package(self.args)
        manifest = pack.read_json(self.payload() / "build-manifest.json")
        self.assertTrue(receipt["sameRevalidatedBytes"])
        self.assertNotIn("sameP5ModuleAndRuntimeBytes", receipt)
        self.assertNotIn("p5BusinessProductUnchanged", receipt)
        self.assertNotIn("configurationHost", manifest)
        self.assertNotIn("p5CloseoutCommit", manifest["qualification"])
        self.assertTrue((self.payload() / pack.REVALIDATED_ASSET).is_file())
        self.assertFalse((self.payload() / "verification/p5-baseline.json").exists())
        self.assertFalse((self.payload() / "verification/configuration-host.json").exists())
        self.assertEqual(manifest["qualification"]["sourceSha256"], digest(self.new_catalog))

    def test_fixed_paths_and_no_automatic_fallback(self):
        self.fresh["qualificationSource"]["path"] = "qualification/native-business.json"
        self.write_fresh()
        with self.assertRaisesRegex(ValueError, "fixed reviewed source"):
            self.load()
        self.fresh_path.unlink()
        with self.assertRaisesRegex(ValueError, "missing regular input"):
            self.load()

    def test_omitted_complete_source_map_entry_is_rejected(self):
        sources = copy.deepcopy(self.sources)
        del sources["tools/preview_pe.py"]
        put_json(self.source_path, sources)
        self.fresh["evidence"]["sourceInputs"] = self.reference(self.source_path)
        self.consumer["sourceInputsSha256"] = digest(self.source_path)
        self.write_fresh()
        with self.assertRaisesRegex(ValueError, "complete source map"):
            self.load()

    def test_unreviewed_or_misdeclared_adoption_delta_is_rejected(self):
        self.consumer["adoptionChanges"]["src/module/manager.cpp"] = {"before": None, "after": "0" * 64}
        self.write_fresh()
        with self.assertRaisesRegex(ValueError, "adoption delta"):
            self.load()

    def test_real_cpp_change_cannot_be_declared_as_an_adoption_exception(self):
        put(self.root / "examples/native_services/counter.cpp", b"// unreviewed producer change\n")
        self.sources = pack.current_source_inputs(self.root, self.bundle)
        put_json(self.source_path, self.sources)
        self.fresh["evidence"]["sourceInputs"] = self.reference(self.source_path)
        self.consumer["sourceInputsSha256"] = digest(self.source_path)
        self.consumer["adoptionChanges"] = {name: {"before": self.captured_sources.get(name), "after": self.sources.get(name)}
            for name in self.captured_sources.keys() | self.sources.keys() if self.captured_sources.get(name) != self.sources.get(name)}
        self.write_fresh()
        with self.assertRaisesRegex(ValueError, "adoption delta"):
            self.load()

    def test_catalog_selection_must_be_an_actual_post_capture_adoption(self):
        put(self.root / "cmake/NativeProductCatalog.cmake", b"# original fixed catalog selection\n")
        self.sources = pack.current_source_inputs(self.root, self.bundle)
        put_json(self.source_path, self.sources)
        self.fresh["evidence"]["sourceInputs"] = self.reference(self.source_path)
        self.consumer["sourceInputsSha256"] = digest(self.source_path)
        self.consumer["adoptionChanges"] = {name: {"before": self.captured_sources.get(name), "after": self.sources.get(name)}
            for name in self.captured_sources.keys() | self.sources.keys() if self.captured_sources.get(name) != self.sources.get(name)}
        self.write_fresh()
        with self.assertRaisesRegex(ValueError, "adoption delta"):
            self.load()

    def test_changed_producer_bytes_are_not_an_adoption_override(self):
        key = next(iter(self.consumer["producerBefore"]))
        self.consumer["producerBefore"][key] = {"bytes": 1, "sha256": "0" * 64}
        self.write_fresh()
        with self.assertRaisesRegex(ValueError, "producer/dependency bytes changed"):
            self.load()

    def test_raw_capture_archive_and_native_review_are_both_required(self):
        record = self.capture_records[0]
        saved = self.bundle / record["path"]
        saved.write_bytes(saved.read_bytes() + b" ")
        with self.assertRaisesRegex(ValueError, "Native archive bytes"):
            self.load()

    def test_native_review_must_bind_the_exact_candidate_and_capture(self):
        original = pack.read_json(self.native_review_path)
        for field, value in (("passed", False), ("candidateSha256", "0" * 64), ("captureSha256", "0" * 64)):
            put_json(self.native_review_path, {**original, field: value})
            self.fresh["evidence"]["qualificationReview"] = self.reference(self.native_review_path)
            self.write_fresh()
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, "Native review"):
                self.load()

    def test_fresh_review_cannot_be_replaced_by_a_true_boolean(self):
        review = pack.read_json(self.fresh_review_path)
        review["candidateSha256"] = "0" * 64
        put_json(self.fresh_review_path, review)
        self.fresh["review"] = self.reference(self.fresh_review_path)
        put_json(self.fresh_path, self.fresh)
        with self.assertRaisesRegex(ValueError, "review does not bind"):
            self.load()

    def test_release21_extra_inputs_are_rejected(self):
        self.release_files["not-a-release-input.dll"] = {"bytes": 0, "sha256": "0" * 64}
        put_json(self.product_path, self.release_files)
        self.fresh["evidence"]["productFiles"] = self.reference(self.product_path)
        self.consumer["productFilesSha256"] = digest(self.product_path)
        self.write_fresh()
        with self.assertRaisesRegex(ValueError, "Release file set"):
            self.load()

    def test_release_byte_length_cannot_be_a_boolean(self):
        self.release_files[next(iter(self.release_files))]["bytes"] = True
        put_json(self.product_path, self.release_files)
        self.fresh["evidence"]["productFiles"] = self.reference(self.product_path)
        self.consumer["productFilesSha256"] = digest(self.product_path)
        self.write_fresh()
        with self.assertRaisesRegex(ValueError, "file metadata"):
            self.load()

    def test_generated_rows_cannot_union_another_profile(self):
        self.consumer["generatedCatalog"]["approvedRows"].append(["org.cordis.example.counter", "0" * 64])
        self.write_fresh()
        baseline, _, documents = self.load()
        with self.assertRaisesRegex(ValueError, "four reviewed entries"):
            pack.profile_inputs(self.build, self.qt, baseline, documents)

    def test_product_actual_exit_and_passed_names_are_required(self):
        self.consumer["validations"][0]["exitCode"] = True
        self.write_fresh()
        with self.assertRaisesRegex(ValueError, "actual product consumer"):
            self.load()

    def test_product_selection_and_duplicate_command_flags_are_rejected(self):
        self.consumer["validations"][0]["command"] += ["--test-dir", "wrong-tree"]
        self.write_fresh()
        with self.assertRaisesRegex(ValueError, "command option"):
            self.load()

    def test_missing_or_duplicate_product_passed_line_is_not_success(self):
        validation = self.consumer["validations"][0]
        path = self.p5 / validation["log"]["path"]
        put(path, self.passed_log(pack.PRODUCT_TESTS) + self.passed_log({next(iter(pack.PRODUCT_TESTS))}))
        validation["log"] = self.reference(path)
        self.write_fresh()
        with self.assertRaisesRegex(ValueError, "duplicate actual Passed"):
            self.load()

    def test_fresh_evidence_root_traversal_is_rejected(self):
        self.fresh["evidence"]["consumer"]["path"] = "../consumer.json"
        candidate = {k: v for k, v in self.fresh.items() if k != "review"}
        review = pack.read_json(self.fresh_review_path)
        review["candidateSha256"] = hashlib.sha256(pack.canonical(candidate)).hexdigest()
        put_json(self.fresh_review_path, review)
        self.fresh["review"] = self.reference(self.fresh_review_path)
        put_json(self.fresh_path, self.fresh)
        with self.assertRaisesRegex(ValueError, "unsafe relative"):
            self.load()

    def test_unrecognized_fresh_asset_is_not_silently_shipped(self):
        put(self.assets / "verification/unreviewed-fresh.json", b"{}\n")
        with self.assertRaisesRegex(ValueError, "unreviewed preview asset"):
            pack.package(self.args)
        self.assertFalse(self.output.exists())

    def test_fresh_revision_and_ambiguous_cli_roots_are_rejected(self):
        self.args.delivery_version = "0.1.5-preview.2"
        with self.assertRaisesRegex(ValueError, "preview.3"):
            pack.package(self.args)
        self.args.delivery_version = self.revision
        self.args.p5_root = str(self.p5)
        with self.assertRaisesRegex(ValueError, "explicit --evidence-root"):
            pack.package(self.args)

    def test_new_asset_does_not_leak_into_legacy_payload(self):
        self.args.revalidated = False
        self.args.evidence_root = None
        self.args.p5_root = str(self.p5)
        # The legacy fixture's old catalog is distinct; restore its original exact receipt.
        put_json(self.old_catalog, {"profiles": [self.profile]})
        receipt = pack.package(self.args)
        self.assertTrue(receipt["sameP5ModuleAndRuntimeBytes"])
        self.assertFalse((self.payload() / pack.REVALIDATED_ASSET).exists())

    def test_late_capture_or_unlisted_source_change_is_not_published(self):
        original = pack.copy_inputs
        def mutate(inputs, payload):
            original(inputs, payload)
            put(self.root / "tools/unlisted_new_source.py", b"# late unreviewed source\n")
        with mock.patch.object(pack, "copy_inputs", side_effect=mutate):
            with self.assertRaisesRegex(ValueError, "complete source map changed"):
                pack.package(self.args)
        self.assertFalse(list(self.output.glob("*.zip")))
        self.assertTrue((self.output / "INCOMPLETE.txt").exists())


if __name__ == "__main__":
    unittest.main()
