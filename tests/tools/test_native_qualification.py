"""Value-level catalog selection/rejection and strict exporter input tests; no DLL or GUI execution."""
import copy
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import unittest


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("native_qualification_export", ROOT / "tools/export_native_qualification.py")
EXPORT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(EXPORT)


def bracket(value):
    if "]==]" in str(value):
        raise ValueError("fixture bracket terminator")
    return "[==[" + str(value) + "]==]"


class NativeCatalogTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.catalog = json.loads((ROOT / "qualification/native-business.json").read_text(encoding="utf-8"))
        cls.cmake = shutil.which("cmake")
        if cls.cmake is None:
            known = Path("E:/Qt/Tools/CMake_64/bin/cmake.exe")
            if known.is_file():
                cls.cmake = str(known)
        if cls.cmake is None:
            raise RuntimeError("CMake 3.21+ is required for the catalog's real value-level tests")

    def invoke(self, profile, catalog=None, overrides=None, configurations=None, missing=False, raw=None):
        with tempfile.TemporaryDirectory(prefix="cordis-catalog-") as directory:
            root = Path(directory)
            (root / "qualification").mkdir()
            if not missing:
                (root / "qualification/native-business.json").write_text(
                    raw if raw is not None else json.dumps(catalog if catalog is not None else self.catalog), encoding="utf-8")
            values = {"PROJECT_SOURCE_DIR": root.as_posix(), "PROJECT_VERSION": profile["sdkVersion"],
                      "CMAKE_CURRENT_BINARY_DIR": (root / "output").as_posix(), "CMAKE_SYSTEM_NAME": "Windows",
                      "CMAKE_CXX_COMPILER_ID": profile["compiler"], "CMAKE_CXX_COMPILER_VERSION": profile["compilerVersion"],
                      "CMAKE_SIZEOF_VOID_P": "8", "CMAKE_CXX_STANDARD": "17", "CMAKE_BUILD_TYPE": profile["buildType"],
                      "CMAKE_MSVC_RUNTIME_LIBRARY": profile["runtimeDefinition"], "Qt5Core_VERSION_STRING": profile["qtVersion"],
                      "CMAKE_CXX_FLAGS": profile["cxxFlags"],
                      "CMAKE_CXX_FLAGS_" + profile["buildType"].upper(): profile["configurationFlags"]}
            if configurations is not None:
                values["CMAKE_CONFIGURATION_TYPES"] = ";".join(configurations)
                for other in self.catalog["profiles"]:
                    values["CMAKE_CXX_FLAGS_" + other["buildType"].upper()] = other["configurationFlags"]
            values.update(overrides or {})
            script = "cmake_minimum_required(VERSION 3.21)\n"
            script += "\n".join("set(" + name + " " + bracket(value) + ")" for name, value in values.items())
            script += "\ninclude(" + bracket((ROOT / "cmake/NativeProductCatalog.cmake").as_posix()) + ")\n"
            script += "cordis_native_product_catalog(source)\nfile(WRITE " + bracket((root / "returned.txt").as_posix()) + " \"${source}\")\n"
            path = root / "fixture.cmake"
            path.write_text(script, encoding="utf-8")
            process = subprocess.run([self.cmake, "-P", str(path)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
            files = {p.name: p.read_text(encoding="utf-8") for p in (root / "output/native-product-catalog").glob("*.cpp")}
            returned = (root / "returned.txt").read_text() if (root / "returned.txt").exists() else ""
            return process.returncode, process.stdout.decode("utf-8", errors="replace"), files, returned

    def test_each_real_profile_has_only_its_four_rows(self):
        for profile in self.catalog["profiles"]:
            with self.subTest(profile=profile["id"]):
                code, log, files, _ = self.invoke(profile)
                self.assertEqual(code, 0, log)
                source = files["native_catalog_" + profile["buildType"] + ".cpp"]
                self.assertEqual(source.count('{"org.cordis.example.'), 4)
                for other in self.catalog["profiles"]:
                    for product in other["products"]:
                        self.assertEqual(product["sha256"] in source, other["id"] == profile["id"])
                self.assertNotIn(self.catalog["provenance"]["exporterSha256"], source)

    def test_unmatched_toolchain_crt_and_flags_produce_empty_values(self):
        profile = self.catalog["profiles"][0]
        for change in ({"CMAKE_CXX_COMPILER_VERSION": "19.44.99999.0"},
                       {"CMAKE_MSVC_RUNTIME_LIBRARY": "UnknownRuntime"},
                       {"CMAKE_CXX_FLAGS": profile["cxxFlags"] + " /DUNQUALIFIED=1"},
                       {"CMAKE_SYSTEM_NAME": "Linux"}):
            with self.subTest(change=change):
                code, log, files, _ = self.invoke(profile, overrides=change)
                self.assertEqual(code, 0, log)
                self.assertFalse(any('{"org.cordis.example.' in source for source in files.values()))

    def test_standard_crt_expression_and_multiconfig_do_not_union(self):
        profile = next(row for row in self.catalog["profiles"] if row["buildType"] == "Release")
        code, log, files, returned = self.invoke(profile, configurations=["Release", "Debug", "RelWithDebInfo"],
                                                overrides={"CMAKE_MSVC_RUNTIME_LIBRARY": "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"})
        self.assertEqual(code, 0, log)
        self.assertIn("$<CONFIG>", returned)
        for name in ("Release", "Debug"):
            source = files["native_catalog_" + name + ".cpp"]
            self.assertEqual(source.count('{"org.cordis.example.'), 4)
            expected = next(row for row in self.catalog["profiles"] if row["buildType"] == name)
            self.assertTrue(all(product["sha256"] in source for product in expected["products"]))
        self.assertNotIn('{"org.cordis.example.', files["native_catalog_RelWithDebInfo.cpp"])

    def test_missing_malformed_and_illegal_identity_fail(self):
        profile = self.catalog["profiles"][0]
        for options in ({"missing": True}, {"raw": "{broken"}):
            self.assertNotEqual(self.invoke(profile, **options)[0], 0)
        for mutation in ("module", "version", "loader", "resident", "sha", "duplicate", "profile", "unknown", "missing-field"):
            changed = copy.deepcopy(self.catalog)
            product = changed["profiles"][0]["products"][0]
            if mutation == "module":
                product["moduleId"] = "org.other.plugin"
            elif mutation == "version":
                product["moduleVersion"] = "1.3.0"
            elif mutation == "loader":
                product["loader"] = "qt-ui"
            elif mutation == "resident":
                product["unloadPolicy"] = "resident"
            elif mutation == "sha":
                product["sha256"] = "invalid"
            elif mutation == "duplicate":
                changed["profiles"][0]["products"][1] = copy.deepcopy(product)
            elif mutation == "profile":
                changed["profiles"].pop()
            elif mutation == "unknown":
                changed["unknown"] = True
            else:
                product.pop("pluginId")
            with self.subTest(mutation=mutation):
                self.assertNotEqual(self.invoke(profile, catalog=changed)[0], 0)


class ExportInputTests(unittest.TestCase):
    def test_json_and_actual_names_are_not_silently_folded(self):
        with self.assertRaises(ValueError):
            EXPORT.parse_json('{"schema":1,"schema":1}')
        with self.assertRaises(ValueError):
            EXPORT.parse_json('{"value":NaN}')
        with self.assertRaises(ValueError):
            EXPORT.passed_names(b"1/1 Test #1: example Passed\n1/1 Test #1: example Passed\n")

    def test_export_cannot_modify_reviewed_source_or_accept_unknown_capture(self):
        with self.assertRaises(ValueError):
            EXPORT.export_candidate(ROOT, Path("does-not-exist"), ROOT / "qualification/native-business.json")
        with tempfile.TemporaryDirectory(prefix="cordis-capture-") as directory:
            root = Path(directory)
            evidence = root / "qualification.json"
            evidence.write_text('{"schema":"unknown","status":"Completed"}', encoding="utf-8")
            with self.assertRaises(ValueError):
                EXPORT.export_candidate(ROOT, evidence, root / "candidate.json")

    def test_nested_evidence_reference_must_be_an_exact_unique_archived_record(self):
        with tempfile.TemporaryDirectory(prefix="cordis-reference-") as directory:
            root = Path(directory)
            data = root / "source.json"
            data.write_text("{}", encoding="utf-8")
            record = {"role": "source", "sourcePath": data.as_posix(), "path": "source.json", "sha256": EXPORT.sha(data)}
            base = {"schema": EXPORT.CAPTURE, "status": "Completed", "checkoutCommit": "0" * 40,
                    "sourceInputs": copy.deepcopy(record), "files": [record], "profiles": []}
            for variant in ("reference-path", "reference-sha", "duplicate"):
                changed = copy.deepcopy(base)
                if variant == "reference-path":
                    changed["sourceInputs"]["path"] = "../outside.json"
                elif variant == "reference-sha":
                    changed["sourceInputs"]["sha256"] = "0" * 64
                else:
                    changed["files"].append(copy.deepcopy(record))
                evidence = root / "qualification.json"
                evidence.write_text(json.dumps(changed), encoding="utf-8")
                with self.subTest(variant=variant), self.assertRaisesRegex(ValueError, "reference|duplicate"):
                    EXPORT.export_candidate(ROOT, evidence, root / "candidate.json")

    def test_failed_attempt_raw_outputs_are_saved_before_interpretation(self):
        with tempfile.TemporaryDirectory(prefix="cordis-failed-output-") as directory:
            root = Path(directory)
            tree = root / "tree"
            (tree / "Testing/Temporary").mkdir(parents=True)
            proof = root / "partial-proof"
            proof.mkdir()
            raw = b'{"event":"run_exception","message":"original failure"}\n'
            (proof / "child.jsonl").write_bytes(raw)
            last = tree / "Testing/Temporary/LastTest.log"
            last.write_text("Proof evidence: " + proof.as_posix() + "\n", encoding="utf-8")
            prefix = root / "endurance"
            prefix.with_suffix(".json").write_text('{"passed":false}', encoding="utf-8")
            prefix.with_suffix(".jsonl").write_bytes(raw)
            bundle = root / "bundle"
            bundle.mkdir()
            archive = EXPORT.Archive(bundle)
            result = EXPORT.archive_attempt_outputs(archive, tree, [
                {"name": "p5.workspace_endurance_native", "command": ["unused", str(prefix)]}])
            self.assertEqual(result["captureErrors"], [])
            self.assertEqual(len(archive.records), 4)
            self.assertEqual((bundle / result["lastTest"]["path"]).read_bytes(), last.read_bytes())
            self.assertTrue(any((bundle / record["path"]).read_bytes() == raw for record in archive.records))


class NinjaFreshnessTests(unittest.TestCase):
    def test_original_graph_clean_and_stale(self):
        cache = EXPORT.cache_values(ROOT / "build/ela-msvc-release")
        ninja, cmake = cache["CMAKE_MAKE_PROGRAM"], cache["CMAKE_COMMAND"]
        with tempfile.TemporaryDirectory(prefix="cordis-ninja-") as directory:
            tree = Path(directory)
            (tree / "input.txt").write_text("source", encoding="utf-8")
            (tree / "first.hpp").write_text("header", encoding="utf-8")
            (tree / "config.txt").write_text("copy", encoding="utf-8")
            (tree / "stamp").write_text("", encoding="utf-8")
            generator = '''from pathlib import Path
import json
r=Path(__file__).resolve().parent
names=sorted(p.name for p in r.glob('*.hpp'))
mode=(r/'config.txt').read_text()
graph='rule verify\\n  command = "{python}" verify.py\\n  restat = 1\\nrule regenerate\\n  command = "{python}" generate.py\\n  generator = 1\\n  restat = 1\\nrule copy\\n  command = "{cmake}" -E '+mode+' input.txt output.txt\\nbuild FORCE: phony\\nbuild stamp: verify | FORCE\\nbuild build.ninja: regenerate | stamp config.txt generate.py\\nbuild output.txt: copy input.txt | '+' '.join(names)+'\\n'
(r/'build.ninja').write_text(graph,encoding='utf-8')
(r/'glob-list.json').write_text(json.dumps(names))
'''.format(python=Path(sys.executable).as_posix(), cmake=Path(cmake).as_posix())
            (tree / "generate.py").write_text(generator, encoding="utf-8")
            (tree / "verify.py").write_text(
                "from pathlib import Path\nimport json,os\nr=Path(__file__).resolve().parent\n"
                "if sorted(p.name for p in r.glob('*.hpp'))!=json.loads((r/'glob-list.json').read_text()):\n"
                " os.utime(r/'stamp',None)\n", encoding="utf-8")
            subprocess.run([sys.executable, str(tree / "generate.py")], check=True)
            def build():
                subprocess.run([ninja, "-C", str(tree), "output.txt"], check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            counter = 0
            def refresh():
                nonlocal counter
                counter += 1
                before = ((tree / "output.txt").read_bytes(), (tree / "output.txt").stat().st_mtime_ns)
                EXPORT.refresh_graph(tree, cache, tree / ("metadata-" + str(counter) + ".log"))
                self.assertEqual(before, ((tree / "output.txt").read_bytes(), (tree / "output.txt").stat().st_mtime_ns))
            def query():
                nonlocal counter
                counter += 1
                return EXPORT.dry_run(tree, {"output.txt"}, cache, tree / ("query-" + str(counter) + ".log"))
            build()
            refresh()
            clean = query()
            self.assertEqual(Path(clean["queryEntry"]).read_bytes(), b"subninja build.ninja\n")
            for kind, path in (("source", "input.txt"), ("header", "first.hpp")):
                with self.subTest(kind=kind):
                    time.sleep(.03)
                    (tree / path).write_text(kind + " changed", encoding="utf-8")
                    with self.assertRaisesRegex(ValueError, "up-to-date"):
                        query()
                    build()
            with self.subTest(kind="command"):
                graph = tree / "build.ninja"
                graph.write_text(graph.read_text().replace("-E copy input", "-E copy_if_different input"), encoding="utf-8")
                with self.assertRaisesRegex(ValueError, "up-to-date"):
                    query()
                build()
            for kind in ("config", "glob"):
                with self.subTest(kind=kind):
                    time.sleep(.03)
                    if kind == "config":
                        (tree / "config.txt").write_text("copy", encoding="utf-8")
                    else:
                        (tree / "second.hpp").write_text("new glob member", encoding="utf-8")
                    refresh()
                    with self.assertRaisesRegex(ValueError, "up-to-date"):
                        query()
                    build()


if __name__ == "__main__":
    unittest.main()
