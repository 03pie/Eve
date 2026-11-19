"""Exercise packaging invariants with isolated receipts and PE files; never load fixture images."""
import argparse
import copy
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import warnings
import zipfile


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import package_preview as pack
from preview_pe import file_version, inspect_pe


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def put(path, data):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return path


def put_json(path, value):
    return put(path, (json.dumps(value, ensure_ascii=False, indent=2) + "\n").encode("utf-8"))


def pe_bytes(imported="KERNEL32.dll", delayed="USER32.dll", version=(14, 44, 35211, 0)):
    """A bounded PE32+ import/resource fixture, not an executable test program."""
    data = bytearray(0x1200)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3c, 0x80)
    data[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<HHIIIHH", data, 0x84, 0x8664, 1, 0, 0, 0, 240, 0x2022)
    optional = 0x98
    struct.pack_into("<H", data, optional, 0x20b)
    struct.pack_into("<Q", data, optional + 24, 0x180000000)
    struct.pack_into("<II", data, optional + 32, 0x1000, 0x200)
    struct.pack_into("<HH", data, optional + 40, 6, 0)
    struct.pack_into("<HH", data, optional + 48, 6, 0)
    struct.pack_into("<II", data, optional + 56, 0x2000, 0x200)
    struct.pack_into("<HH", data, optional + 68, 3, 0x8140)
    struct.pack_into("<QQQQ", data, optional + 72, 0x100000, 0x1000, 0x100000, 0x1000)
    struct.pack_into("<I", data, optional + 108, 16)
    section = optional + 240
    data[section:section + 8] = b".rdata\0\0"
    struct.pack_into("<IIII", data, section + 8, 0x1000, 0x1000, 0x1000, 0x200)
    struct.pack_into("<I", data, section + 36, 0x40000040)
    if imported:
        struct.pack_into("<II", data, optional + 112 + 8, 0x1000, 40)
        struct.pack_into("<IIIII", data, 0x200, 0x1100, 0, 0, 0x1180, 0x1120)
        encoded = imported.encode("ascii") + b"\0"
        data[0x380:0x380 + len(encoded)] = encoded
    if delayed:
        struct.pack_into("<II", data, optional + 112 + 13 * 8, 0x1080, 64)
        struct.pack_into("<IIIIIIII", data, 0x280, 1, 0x11a0, 0, 0, 0, 0, 0, 0)
        encoded = delayed.encode("ascii") + b"\0"
        data[0x3a0:0x3a0 + len(encoded)] = encoded
    # A genuine VERSIONINFO resource lets the Windows API read the fixture's version without loading it.
    version_ms, version_ls = (version[0] << 16) | version[1], (version[2] << 16) | version[3]
    fixed = struct.pack("<13I", 0xfeef04bd, 0x10000, version_ms, version_ls,
                        version_ms, version_ls, 0x3f, 0, 0x40004, 2, 0, 0, 0)
    version = bytearray(struct.pack("<HHH", 0, len(fixed), 0) + "VS_VERSION_INFO\0".encode("utf-16-le"))
    version.extend(b"\0" * (-len(version) % 4))
    version.extend(fixed)
    struct.pack_into("<H", version, 0, len(version))
    struct.pack_into("<II", data, optional + 112 + 2 * 8, 0x1200, 0x200)
    resource = 0x400
    for offset in (0, 0x20, 0x40):
        struct.pack_into("<HH", data, resource + offset + 12, 0, 1)
    struct.pack_into("<II", data, resource + 16, 16, 0x80000020)
    struct.pack_into("<II", data, resource + 0x30, 1, 0x80000040)
    struct.pack_into("<II", data, resource + 0x50, 1033, 0x60)
    struct.pack_into("<IIII", data, resource + 0x60, 0x1280, len(version), 0, 0)
    data[0x480:0x480 + len(version)] = version
    return bytes(data)


class PrimitiveTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="cordis-preview-primitives-")
        self.addCleanup(temporary.cleanup)
        self.work = Path(temporary.name)

    def test_sha_matches_bytes_on_supported_python(self):
        path = put(self.work / "payload", b"frozen\0payload" * 5000)
        self.assertEqual(pack.sha(path), digest(path))

    def test_windows_unsafe_paths_rejected(self):
        for value in ("../outside", "/root", "C:relative", "x\\y", "x//y", "x/../y", "x/.",
                      "end.", "end ", "nul.txt", "COM1.bin", "LPT9", "a:b", "a\nb", "a\x7fb",
                      "COM¹.txt", "LPT²", "x*", ""):
            with self.subTest(value=value), self.assertRaises(ValueError):
                pack.relative_path(value)
        self.assertEqual(pack.relative_path("licenses/中文 with spaces.txt"), "licenses/中文 with spaces.txt")

    def test_duplicate_escaped_json_key_and_nonfinite_rejected(self):
        for raw in ('{"name":1,"na\\u006de":2}', '{"x":NaN}', '{"x":Infinity}', '{"x":-Infinity}'):
            with self.subTest(raw=raw), self.assertRaises(ValueError):
                pack.read_json(put(self.work / "bad.json", raw.encode()))

    def test_evidence_suffix_must_be_unique(self):
        with self.assertRaisesRegex(ValueError, "ambiguous"):
            pack.evidence_hash({"D:/a/bin/app.exe": "1" * 64, "E:/b/bin/app.exe": "2" * 64}, "/bin/app.exe")

    def test_pe_normal_and_delay_imports(self):
        path = put(self.work / "fixture.dll", pe_bytes())
        self.assertEqual(inspect_pe(path), {"machine": "x64", "imports": ["kernel32.dll"],
                                            "delayImports": ["user32.dll"]})
        if os.name == "nt":
            self.assertEqual(file_version(path), "14.44.35211.0")

    def test_malformed_pe_is_rejected_without_loading(self):
        changes = {
            "signature": lambda b: b.__setitem__(slice(0, 2), b"NO"),
            "huge_dos_offset": lambda b: struct.pack_into("<I", b, 0x3c, 0xfffffff0),
            "x86": lambda b: struct.pack_into("<H", b, 0x84, 0x14c),
            "pe32": lambda b: struct.pack_into("<H", b, 0x98, 0x10b),
            "no_sections": lambda b: struct.pack_into("<H", b, 0x86, 0),
            "oversized_directories": lambda b: struct.pack_into("<I", b, 0x98 + 108, 99),
            "truncated_section": lambda b: struct.pack_into("<I", b, 0x98 + 240 + 16, len(b)),
            "unmapped_import": lambda b: struct.pack_into("<I", b, 0x98 + 120, 0x900000),
            "bad_library_path": lambda b: b.__setitem__(slice(0x380, 0x38b), b"../bad.dll\0"),
            "va_delay_import": lambda b: struct.pack_into("<I", b, 0x280, 0),
            "unterminated_descriptors": lambda b: b.__setitem__(slice(0x214, 0x228), b[0x200:0x214]),
        }
        for name, mutate in changes.items():
            malformed = bytearray(pe_bytes())
            mutate(malformed)
            with self.subTest(name=name), self.assertRaises(ValueError):
                inspect_pe(put(self.work / (name + ".dll"), malformed))

    def test_ambiguous_section_mapping_rejected(self):
        data = bytearray(pe_bytes())
        struct.pack_into("<H", data, 0x86, 2)
        section = 0x98 + 240
        data[section + 40:section + 80] = data[section:section + 40]
        with self.assertRaisesRegex(ValueError, "ambiguous"):
            inspect_pe(put(self.work / "overlap.dll", data))

    @unittest.skipUnless(os.name == "nt", "Windows directory junction semantics")
    def test_ancestor_junction_rejected_for_input_and_new_output(self):
        target, alias = self.work / "target", self.work / "alias"
        target.mkdir()
        put(target / "data.bin", b"ordinary data behind a junction")
        done = subprocess.run([os.environ.get("COMSPEC", "cmd.exe"), "/d", "/c", "mklink", "/J",
                               str(alias), str(target)], capture_output=True, check=False)
        if done.returncode:
            self.skipTest("the isolated test directory cannot create a junction")
        self.addCleanup(alias.rmdir)
        with self.assertRaisesRegex(ValueError, "reparse"):
            pack.regular(alias / "data.bin")
        with self.assertRaisesRegex(ValueError, "reparse"):
            pack.reject_reparse(alias / "new output")


@unittest.skipUnless(os.name == "nt", "the preview records Windows VERSIONINFO resources")
class PackagingTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="cordis-preview-packaging-")
        self.addCleanup(temporary.cleanup)
        self.work = Path(temporary.name)
        self.root, self.build = self.work / "checkout", self.work / "build"
        self.qt, self.crt, self.p5 = self.work / "Qt kit", self.work / "CRT", self.work / "P5"
        self.assets = self.root / "delivery/windows"
        self.output = self.work / "new output"
        self.revision = "0.1.5-preview.1"
        self.args = argparse.Namespace(build_dir=str(self.build), qt_root=str(self.qt), crt_root=str(self.crt),
                                       p5_root=str(self.p5), output=str(self.output), delivery_version=self.revision)
        for path in (self.root, self.build, self.qt, self.crt, self.p5):
            path.mkdir()
        self.patch = mock.patch.multiple(pack, ROOT=self.root, ASSETS=self.assets)
        self.patch.start()
        self.addCleanup(self.patch.stop)
        self.git = mock.patch.object(pack.subprocess, "check_output", side_effect=lambda arguments, **kw:
                                     "fixture-commit\n" if arguments[1] == "rev-parse" else "")
        self.git.start()
        self.addCleanup(self.git.stop)
        image = pe_bytes()
        self.product_files, self.dependencies, self.products, self.normal = {}, [], [], []
        for name in pack.ENTRY_POINTS:
            path = put(self.build / "bin" / name, image)
            if name == "cordis_platform_workbench_demo.exe":
                self.product_files[str(path)] = digest(path)
            else:
                self.configuration_files = {str(path): digest(path)}
        for name in ("cordis_core.dll", "cordis_qt.dll", "cordis_ui.dll", "ElaWidgetTools.dll",
                     "Qt5Core.dll", "Qt5Gui.dll", "Qt5Widgets.dll"):
            folder = self.qt if name.startswith("Qt5") else self.build
            path = put(folder / "bin" / name, pe_bytes(version=(5, 15, 2, 0)) if name.startswith("Qt5") else image)
            self.product_files[str(path)] = digest(path)
            self.dependencies.append({"path": str(path), "sha256": digest(path)})
        for alias in ("counter", "reporter", "notebook"):
            for variant in ("a", "b"):
                directory = ("qt-upgrade/" if alias == "notebook" else "native-upgrade/") + alias + "-" + variant
                name = "cordis_example_" + alias + "_" + variant + ".dll"
                library = put(self.build / "bin" / directory / name, image)
                manifest = {"moduleId": "org.cordis.example." + alias, "plugins": ["example." + alias],
                            "moduleVersion": "1.1.0" if variant == "a" else "1.2.0",
                            "loader": "qt-ui" if alias == "notebook" else "c-abi",
                            "unloadPolicy": "resident" if alias == "notebook" else "explicit",
                            "library": name, "sha256": digest(library)}
                path = put_json(library.parent / "module.json", manifest)
                self.product_files[str(path)] = digest(path)
                self.product_files[str(library)] = digest(library)
                relative = directory + "/module.json"
                self.normal.append({"configuration": "ela-msvc-release", "manifest": "build/ela-msvc-release/bin/" + relative,
                                    "manifest_sha256": digest(path), "sha256": digest(library)})
                if alias != "notebook":
                    self.products.append({"moduleId": manifest["moduleId"], "sha256": digest(library),
                                          "manifest": relative, "manifestSha256": digest(path)})
        self.profile = {"id": "ela-msvc-release", "compiler": "MSVC", "compilerVersion": "19.44.35222.0",
                        "runtime": "MD", "runtimeDefinition": "MultiThreadedDLL", "buildType": "Release",
                        "asan": False, "pointerBits": 64, "platform": "windows-x64", "qtVersion": "5.15.2",
                        "sdkVersion": "0.1.5", "cxxFlags": "/DWIN32 /D_WINDOWS /EHsc",
                        "configurationFlags": "/O2 /Ob2 /DNDEBUG", "dependencies": self.dependencies,
                        "products": self.products}
        self.catalog = put_json(self.root / "qualification/native-business.json", {"profiles": [self.profile]})
        self.product_files[str(self.catalog)] = digest(self.catalog)
        self.assertEqual(len(self.product_files), 21)
        for name in pack.QT_PLUGINS:
            put(self.qt / "plugins" / name, pe_bytes(version=(5, 15, 2, 0)))
        for name in ("libEGL.dll", "libGLESv2.dll"):
            put(self.qt / "bin" / name, image)
        for name in pack.CRT_FILES:
            put(self.crt / name, image)
        put(self.assets / "README.txt", "临时包 with spaces\n".encode())
        put(self.assets / "qt.conf", b"[Paths]\nPlugins=plugins\n")
        put(self.assets / "verification/verify-package.ps1", b"# value-only fixture verifier\n")
        for name in ("record-desktop.ps1", "preview-windows.psm1", "preview-ui.psm1", "preview-acl.ps1"):
            put(self.assets / "verification" / name, b"# fixture desktop support; never executed\n")
        material = put(self.assets / "licenses/NOTICE.txt", b"fixture material\n")
        put_json(self.assets / "licenses/sources.json", {"schema": "cordis.preview.license-sources/1",
                 "files": [{"path": material.name, "bytes": material.stat().st_size, "sha256": digest(material)}]})
        put(self.root / "tools/package_preview.py", b"# fixture provenance\n")
        put(self.root / "tools/preview_pe.py", b"# fixture PE provenance\n")
        generated = put(self.build / "examples/qt_workspace/native-product-catalog/native_catalog_Release.cpp", b"// frozen fixture\n")
        self.source_inputs = {}
        commands = []
        for source in ("examples/qt/config_workbench.cpp", "examples/qt_workspace/main.cpp"):
            path = put(self.root / source, b"// frozen fixture source\n")
            self.source_inputs[source] = digest(path)
            commands.append({"file": str(path), "command": "cl /O2 /MD /DNDEBUG -c " + str(path)})
        self.commands = put_json(self.build / "compile_commands.json", commands)
        self.cache_values = {"CMAKE_BUILD_TYPE": "Release", "CMAKE_MSVC_RUNTIME_LIBRARY": "MultiThreadedDLL",
                             "CORDIS_SHARED_CORE": "ON", "CORDIS_REQUIRE_QT5152": "ON", "CORDIS_BUILD_ELA_UI": "ON",
                             "CORDIS_BUILD_EXAMPLES": "ON", "CMAKE_CXX_FLAGS": self.profile["cxxFlags"],
                             "CMAKE_CXX_FLAGS_RELEASE": self.profile["configurationFlags"],
                             "Qt5Core_DIR": str(self.qt / "lib/cmake/Qt5Core")}
        self.write_cache()
        put(self.build / "CMakeFiles/3.29.7/CMakeCXXCompiler.cmake",
            b'set(CMAKE_CXX_COMPILER_ID "MSVC")\nset(CMAKE_CXX_COMPILER_VERSION "19.44.35222.0")\n')
        put(self.build / "build.ninja", "".join("build bin\\" + name + ": CXX_EXECUTABLE_LINKER\n"
                                                for name in pack.ENTRY_POINTS).encode())
        self.documents = {"productFiles": self.product_files, "configurationExecutables": self.configuration_files,
                          "sourceInputs": self.source_inputs,
                          "artifacts": {"passed": True, "base_commit": "implementation-fixture", "frozen_source_inputs": self.source_inputs,
                                        "normal_artifacts": self.normal, "actual_generated_catalog_profiles": {
                                            "ela-msvc-release": {"generated_source": {"sha256": digest(generated)},
                                                "approved_rows": [[x["moduleId"], x["sha256"]] for x in self.products]}}}}
        evidence = {}
        for role, document in self.documents.items():
            path = put_json(self.p5 / (role + ".json"), document)
            evidence[role] = {"path": path.name, "sha256": digest(path)}
        self.baseline = {"schema": "cordis.preview.baseline/1", "implementationCommit": "implementation-fixture",
                         "p5CloseoutCommit": "closeout-fixture", "qualificationSourceSha256": digest(self.catalog),
                         "evidence": evidence}
        put_json(self.assets / "verification/p5-baseline.json", self.baseline)
        # Preserve the original receipts, then model the one separately reviewed repair.
        source = put(self.root / pack.CONFIGURATION_SOURCE, b"// fixture Qt Unicode arguments repair\n")
        executable = put(self.build / "bin" / pack.ENTRY_POINTS[0], image + b"\nreviewed Unicode host fixture\n")
        evidence = {}
        for role in sorted(pack.HOST_EVIDENCE_ROLES):
            path = put_json(self.p5 / "host" / (role + ".json"), {"fixtureRole": role})
            evidence[role] = {"path": path.relative_to(self.p5).as_posix(), "sha256": digest(path)}
        self.host = {"schema": "cordis.preview.configuration-host/1", "profile": "ela-msvc-release",
                     "source": {"path": pack.CONFIGURATION_SOURCE,
                                "previousSha256": self.source_inputs[pack.CONFIGURATION_SOURCE], "sha256": digest(source)},
                     "executable": {"path": pack.ENTRY_POINTS[0],
                                    "previousSha256": pack.evidence_hash(self.configuration_files, "/bin/" + pack.ENTRY_POINTS[0]),
                                    "sha256": digest(executable)}, "evidence": evidence}
        self.host_path = self.assets / "verification/configuration-host.json"
        self.review_path = self.p5 / "host/review.json"
        self.write_host()

    def write_host(self, host=None, review=None):
        host = self.host if host is None else host
        candidate = {key: value for key, value in host.items() if key != "review"}
        if review is None:
            review = {"schema": "cordis.preview.host-review/1", "passed": True,
                      "candidateSha256": hashlib.sha256(pack.canonical(candidate)).hexdigest(),
                      "configurationSourceSha256": host["source"]["sha256"],
                      "configurationExecutableSha256": host["executable"]["sha256"]}
        put_json(self.review_path, review)
        host["review"] = {"path": self.review_path.relative_to(self.p5).as_posix(), "sha256": digest(self.review_path)}
        put_json(self.host_path, host)

    def write_cache(self):
        put(self.build / "CMakeCache.txt", "".join(key + ":STRING=" + value + "\n"
                                                   for key, value in self.cache_values.items()).encode())

    def collect(self):
        baseline, _, documents = pack.load_baseline(self.p5)
        return pack.collect_inputs(self.build, self.qt, self.crt, baseline, documents)

    def payload(self):
        return self.output / ("Cordis-" + self.revision + "-windows-x64")

    def test_complete_package_roundtrip_and_existing_output_unchanged(self):
        receipt = pack.package(self.args)
        payload = self.payload()
        manifest = json.loads((payload / "build-manifest.json").read_text(encoding="utf-8"))
        expected = {entry["path"]: entry["sha256"] for entry in manifest["files"]}
        self.assertEqual(len(expected), len(manifest["files"]))
        self.assertIn("verification/preview-acl.ps1", expected)
        self.assertEqual({p.relative_to(payload).as_posix() for p in payload.rglob("*") if p.is_file()},
                         set(expected) | {"build-manifest.json", "SHA256SUMS.txt"})
        for entry in manifest["files"]:
            self.assertEqual(digest(payload / entry["path"]), entry["sha256"])
            self.assertEqual(digest(entry["source"]["path"]), entry["sha256"])
        self.assertFalse(receipt["packageRuntimeVerified"])
        self.assertFalse(receipt["cleanMachineVerified"])
        self.assertNotIn("sameQualifiedBytes", receipt)
        self.assertIs(receipt["sameP5ModuleAndRuntimeBytes"], True)
        self.assertIs(receipt["p5BusinessProductUnchanged"], True)
        self.assertEqual(receipt["reviewedConfigurationHostSha256"], self.host["executable"]["sha256"])
        host = manifest["configurationHost"]
        self.assertEqual(host["source"], self.host["source"])
        self.assertEqual(host["executable"], self.host["executable"])
        self.assertEqual(host["record"], {"path": "verification/configuration-host.json", "sha256": digest(self.host_path)})
        self.assertEqual(set(manifest["qualification"]["evidence"]), set(self.baseline["evidence"]))
        config = [entry for entry in manifest["files"] if entry["path"] == pack.ENTRY_POINTS[0]][0]
        self.assertEqual(config["role"], "reviewed-configuration-host")
        self.assertNotEqual(config["sha256"], self.host["executable"]["previousSha256"])
        archive = self.output / receipt["archive"]["path"]
        self.assertEqual(digest(archive), receipt["archive"]["sha256"])
        with zipfile.ZipFile(archive) as package:
            for name in package.namelist():
                relative = name.split("/", 1)[1]
                self.assertEqual(package.read(name), (payload / relative).read_bytes())
        before = {p.relative_to(self.output).as_posix(): digest(p) for p in self.output.rglob("*") if p.is_file()}
        with self.assertRaisesRegex(ValueError, "absent"):
            pack.package(self.args)
        self.assertEqual(before, {p.relative_to(self.output).as_posix(): digest(p)
                                  for p in self.output.rglob("*") if p.is_file()})

    def test_debug_static_and_sanitized_cache_profiles_rejected(self):
        original = copy.deepcopy(self.cache_values)
        for key, value in (("CMAKE_BUILD_TYPE", "Debug"), ("CORDIS_SHARED_CORE", "OFF"),
                           ("CMAKE_MSVC_RUNTIME_LIBRARY", "MultiThreadedDebugDLL"),
                           ("CMAKE_CXX_FLAGS", "/EHsc /fsanitize=address")):
            self.cache_values = {**original, key: value}
            self.write_cache()
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.collect()

    def test_windows_ninja_output_separator_and_wrong_qt_root(self):
        profile, _ = pack.profile_inputs(self.build, self.qt, self.baseline, self.documents)
        self.assertEqual(profile["id"], "ela-msvc-release")
        with self.assertRaisesRegex(ValueError, "Qt root"):
            pack.profile_inputs(self.build, self.work / "different kit", self.baseline, self.documents)

    def test_actual_compile_flags_cannot_contradict_release_cache(self):
        commands = json.loads(self.commands.read_text(encoding="utf-8"))
        commands[0]["command"] = commands[0]["command"].replace("/MD ", "/MDd ")
        put_json(self.commands, commands)
        with self.assertRaises(ValueError):
            self.collect()

    def test_changed_p5_evidence_or_frozen_source_rejected(self):
        original = (self.p5 / "productFiles.json").read_bytes()
        put(self.p5 / "productFiles.json", original + b" ")
        with self.assertRaisesRegex(ValueError, "evidence changed"):
            pack.load_baseline(self.p5)
        put(self.p5 / "productFiles.json", original)
        put(self.root / "examples/qt_workspace/main.cpp", b"changed after receipt\n")
        with self.assertRaisesRegex(ValueError, "compilation input"):
            pack.load_baseline(self.p5)

    def test_configuration_host_is_required_even_with_original_source_and_exe(self):
        self.host_path.unlink()
        put(self.root / pack.CONFIGURATION_SOURCE, b"// frozen fixture source\n")
        put(self.build / "bin" / pack.ENTRY_POINTS[0], pe_bytes())
        with self.assertRaisesRegex(ValueError, "missing regular input"):
            self.collect()

    def test_host_schema_cannot_authorize_another_source_or_a_dll(self):
        original = copy.deepcopy(self.host)
        attempts = [("source", "examples/qt_workspace/main.cpp"),
                    ("executable", "native-upgrade/counter-a/cordis_example_counter_a.dll")]
        for field, path in attempts:
            host = copy.deepcopy(original); host[field]["path"] = path
            self.write_host(host)
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, "cannot override"):
                self.collect()
        host = copy.deepcopy(original); host["source"]["overrides"] = {"src/module/manager.cpp": "0" * 64}
        self.write_host(host)
        with self.assertRaisesRegex(ValueError, "cannot override"):
            self.collect()
        host = copy.deepcopy(original); host["dllOverrides"] = {"cordis_core.dll": "0" * 64}
        self.write_host(host)
        with self.assertRaisesRegex(ValueError, "schema/profile"):
            self.collect()

    def test_host_predecessors_must_match_the_original_two_p5_records(self):
        original = copy.deepcopy(self.host)
        for field in ("source", "executable"):
            host = copy.deepcopy(original); host[field]["previousSha256"] = "0" * 64
            self.write_host(host)
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, "P5 predecessor"):
                self.collect()
            host = copy.deepcopy(original); host[field]["sha256"] = host[field]["previousSha256"]
            self.write_host(host)
            with self.subTest(unchanged=field), self.assertRaisesRegex(ValueError, "P5 predecessor"):
                self.collect()

    def test_old_configuration_executable_and_unreviewed_source_are_rejected(self):
        path = self.build / "bin" / pack.ENTRY_POINTS[0]
        current = path.read_bytes(); put(path, pe_bytes())
        with self.assertRaisesRegex(ValueError, "frozen artifact changed"):
            self.collect()
        put(path, current)
        put(self.root / pack.CONFIGURATION_SOURCE, b"// unreviewed further edit\n")
        with self.assertRaisesRegex(ValueError, "compilation input"):
            self.collect()

    def test_host_evidence_and_review_tampering_are_rejected(self):
        evidence = self.p5 / self.host["evidence"]["unicodeCompletion"]["path"]
        before = evidence.read_bytes(); put(evidence, before + b" ")
        with self.assertRaisesRegex(ValueError, "host evidence changed: unicodeCompletion"):
            self.collect()
        put(evidence, before)
        with self.review_path.open("ab") as stream: stream.write(b" ")
        with self.assertRaisesRegex(ValueError, "host evidence changed: review"):
            self.collect()

    def test_review_must_bind_this_exact_candidate_and_two_new_digests(self):
        original_review = pack.read_json(self.review_path)
        for key, value in (("candidateSha256", "0" * 64), ("configurationSourceSha256", "0" * 64),
                           ("configurationExecutableSha256", "0" * 64), ("passed", False)):
            review = {**original_review, key: value}
            self.write_host(review=review)
            with self.subTest(key=key), self.assertRaisesRegex(ValueError, "review does not bind"):
                self.collect()

    def test_host_evidence_roles_and_paths_cannot_be_omitted_or_escape(self):
        original = copy.deepcopy(self.host)
        host = copy.deepcopy(original); del host["evidence"]["unicodeCompletion"]
        self.write_host(host)
        with self.assertRaisesRegex(ValueError, "exact reviewed evidence roles"):
            self.collect()
        host = copy.deepcopy(original); host["evidence"]["unicodeCompletion"]["path"] = "../outside.json"
        self.write_host(host)
        with self.assertRaisesRegex(ValueError, "unsafe relative"):
            self.collect()

    def test_review_binds_canonical_candidate_not_raw_line_endings(self):
        raw = self.host_path.read_bytes()
        put(self.host_path, raw.replace(b"\n", b"\r\n"))
        self.collect()

    def test_exact_desktop_helpers_are_required_and_other_assets_still_rejected(self):
        for name in ("preview-ui.psm1", "preview-acl.ps1"):
            helper = self.assets / "verification" / name
            before = helper.read_bytes(); helper.unlink()
            with self.subTest(helper=name), self.assertRaisesRegex(ValueError, "missing regular input"):
                self.collect()
            put(helper, before)
        put(self.assets / "verification/unreviewed-helper.psm1", b"not in the fixed delivery inputs")
        with self.assertRaisesRegex(ValueError, "unreviewed preview asset"):
            self.collect()

    def test_baseline_cannot_escape_its_evidence_root(self):
        baseline = copy.deepcopy(self.baseline)
        baseline["evidence"]["productFiles"]["path"] = "../outside.json"
        put_json(self.assets / "verification/p5-baseline.json", baseline)
        outside = put(self.work / "outside.json", b"must not become an evidence source")
        original = outside.read_bytes()
        with self.assertRaisesRegex(ValueError, "unsafe relative"):
            pack.load_baseline(self.p5)
        self.assertEqual(outside.read_bytes(), original)

    def test_changed_approved_executable_rejected(self):
        with (self.build / "bin/cordis_platform_workbench_demo.exe").open("ab") as stream:
            stream.write(b"different bytes")
        with self.assertRaisesRegex(ValueError, "frozen artifact changed"):
            self.collect()

    def test_changed_qualification_and_manifest_identity_rejected(self):
        original = self.catalog.read_bytes()
        put(self.catalog, original + b" ")
        with self.assertRaisesRegex(ValueError, "qualification source changed"):
            pack.profile_inputs(self.build, self.qt, self.baseline, self.documents)
        put(self.catalog, original)
        path = self.build / "bin/native-upgrade/counter-b/module.json"
        manifest = json.loads(path.read_text(encoding="utf-8"))
        manifest["moduleId"] = "org.cordis.example.reporter"
        put_json(path, manifest)
        with self.assertRaisesRegex(ValueError, "identity mismatch"):
            self.collect()

    def test_output_in_normalized_input_tree_rejected_before_evidence_read(self):
        (self.build / "alias").mkdir()
        self.args.build_dir = str(self.build / "alias/..")
        self.args.output = str(self.build / "new output")
        with mock.patch.object(pack, "load_baseline", side_effect=AssertionError("overlap guard was too late")):
            with self.assertRaisesRegex(ValueError, "nested"):
                pack.package(self.args)
        self.assertFalse(Path(self.args.output).exists())

    def test_source_change_after_pre_copy_hash_rejected(self):
        source = put(self.work / "source.bin", b"original payload")
        target = self.work / "copy"
        target.mkdir()
        entry = {"path": "payload.bin", "sha256": digest(source), "bytes": source.stat().st_size,
                 "source": {"path": str(source), "identity": pack.identity(source)}}
        original_sha = pack.sha
        changed = False

        def race(path):
            nonlocal changed
            value = original_sha(path)
            if Path(path) == source and not changed:
                changed = True
                source.write_bytes(b"mutation after the pre-copy check")
            return value

        with mock.patch.object(pack, "sha", side_effect=race):
            with self.assertRaisesRegex(ValueError, "during copy"):
                pack.copy_inputs([entry], target)

    def test_copied_payload_mutation_is_not_published(self):
        original_copy = pack.copy_inputs

        def change_after_copy(inputs, payload):
            original_copy(inputs, payload)
            with (payload / "cordis_config_workbench.exe").open("ab") as stream:
                stream.write(b"changed copied payload")

        with mock.patch.object(pack, "copy_inputs", side_effect=change_after_copy):
            with self.assertRaises(ValueError):
                pack.package(self.args)
        self.assertTrue((self.output / "INCOMPLETE.txt").exists())
        self.assertFalse(list(self.output.glob("*.zip")))

    def test_host_evidence_and_compilation_sources_are_rechecked_after_copy(self):
        original_copy = pack.copy_inputs
        for index, changed in enumerate((self.review_path, self.root / pack.CONFIGURATION_SOURCE)):
            self.args.output = str(self.work / ("raced-output-" + str(index)))
            original = changed.read_bytes()
            def change_after_copy(inputs, payload):
                original_copy(inputs, payload)
                put(changed, original + b" ")
            with self.subTest(path=str(changed)), mock.patch.object(pack, "copy_inputs", side_effect=change_after_copy):
                with self.assertRaisesRegex(ValueError, "source evidence changed after"):
                    pack.package(self.args)
            self.assertFalse(list(Path(self.args.output).glob("*.zip")))
            self.assertTrue((Path(self.args.output) / "INCOMPLETE.txt").exists())
            put(changed, original)

    def test_new_unlisted_payload_file_during_zip_is_rejected(self):
        original_write = zipfile.ZipFile.write
        injected = False

        def write(archive, filename, *arguments, **keywords):
            nonlocal injected
            if not injected:
                injected = True
                put(self.payload() / "unlisted.bin", b"new file after ZIP file enumeration")
            return original_write(archive, filename, *arguments, **keywords)

        with mock.patch.object(zipfile.ZipFile, "write", new=write):
            with self.assertRaises(ValueError):
                pack.package(self.args)
        self.assertFalse(list(self.output.glob("*.zip")))

    def test_sums_changed_during_zip_cannot_redefine_its_expected_hash(self):
        original_write = zipfile.ZipFile.write

        def write(archive, filename, *arguments, **keywords):
            if Path(filename).name == "SHA256SUMS.txt":
                with Path(filename).open("ab") as stream:
                    stream.write(b"invalid additional checksum line\n")
            return original_write(archive, filename, *arguments, **keywords)

        with mock.patch.object(zipfile.ZipFile, "write", new=write):
            with self.assertRaises(ValueError):
                pack.package(self.args)
        self.assertFalse(list(self.output.glob("*.zip")))

    def test_duplicate_zip_entry_is_rejected(self):
        original_write = zipfile.ZipFile.write
        injected = False

        def write(archive, filename, arcname, *arguments, **keywords):
            nonlocal injected
            result = original_write(archive, filename, arcname, *arguments, **keywords)
            if not injected:
                injected = True
                archive.writestr(arcname, b"duplicate archive entry")
            return result

        with warnings.catch_warnings(), mock.patch.object(zipfile.ZipFile, "write", new=write):
            warnings.filterwarnings("ignore", message="Duplicate name:.*", category=UserWarning)
            with self.assertRaisesRegex(ValueError, "ZIP file set"):
                pack.package(self.args)
        self.assertFalse(list(self.output.glob("*.zip")))

    def test_nested_qt_plugin_is_not_a_root_import_resolution(self):
        put(self.qt / "plugins/platforms/qwindows.dll", pe_bytes(imported="qjpeg.dll", version=(5, 15, 2, 0)))
        with self.assertRaisesRegex(ValueError, "unresolved"):
            self.collect()

    def test_license_inventory_extra_file_and_false_size_rejected(self):
        extra = put(self.assets / "licenses/extra.txt", b"not inventoried")
        with self.assertRaisesRegex(ValueError, "file set"):
            pack.check_license_materials()
        extra.unlink()
        path = self.assets / "licenses/sources.json"
        inventory = json.loads(path.read_text(encoding="utf-8"))
        inventory["files"][0]["bytes"] = True
        put_json(path, inventory)
        with self.assertRaisesRegex(ValueError, "license material"):
            pack.check_license_materials()


if __name__ == "__main__":
    unittest.main()
