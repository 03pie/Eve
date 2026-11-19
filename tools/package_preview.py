"""Copy the frozen P5 Release artifacts into an independently verifiable internal preview."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import subprocess
import zipfile

from preview_pe import file_version, inspect_pe


ROOT = Path(__file__).resolve().parent.parent
ASSETS = ROOT / "delivery/windows"
SCHEMA = "cordis.preview.package/1"
SYSTEM_DLLS = set(("advapi32 bcrypt comctl32 comdlg32 crypt32 d3d11 d3d9 d3dcompiler_47 dbghelp dwmapi dxgi "
                   "gdi32 imm32 iphlpapi kernel32 mpr netapi32 ntdll ole32 oleaut32 psapi rpcrt4 secur32 "
                   "setupapi shell32 shlwapi user32 userenv uxtheme version winmm winspool wintrust "
                   "ws2_32 wtsapi32 ucrtbase").split())
QT_PLUGINS = ("platforms/qwindows.dll", "styles/qwindowsvistastyle.dll",
              "imageformats/qjpeg.dll", "imageformats/qgif.dll", "imageformats/qico.dll")
CRT_FILES = ("msvcp140.dll", "msvcp140_1.dll", "vcruntime140.dll", "vcruntime140_1.dll")
ENTRY_POINTS = ("cordis_config_workbench.exe", "cordis_platform_workbench_demo.exe")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, "duplicate JSON key: " + key)
        result[key] = value
    return result


def read_json(path):
    def reject(value):
        raise ValueError("nonfinite JSON: " + value)
    return json.loads(Path(path).read_text(encoding="utf-8-sig"), object_pairs_hook=unique_object,
                      parse_constant=reject)


def sha(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as source:
        while chunk := source.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def canonical(value):
    return (json.dumps(value, ensure_ascii=False, sort_keys=True, indent=2) + "\n").encode("utf-8")


def relative_path(value):
    require(isinstance(value, str) and value and "\\" not in value and ":" not in value,
            "invalid relative package path")
    parts = value.split("/")
    require(not any(part in ("", ".", "..") or part.endswith((".", " ")) or
                    re.search(r'[\x00-\x1f\x7f<>"|?*]', part) or
                    re.fullmatch(r"(?i:con|prn|aux|nul|com[1-9¹²³]|lpt[1-9¹²³])(?:\..*)?", part) for part in parts),
            "unsafe relative package path: " + value)
    require(not PurePosixPath(value).is_absolute(), "absolute package path")
    return value


def reject_reparse(path):
    """Reject junctions/symlinks on the path, including ancestors, before reading or writing."""
    path = Path(os.path.abspath(path))
    for part in (path, *path.parents):
        if part.exists() or part.is_symlink():
            info = part.lstat()
            require(not stat.S_ISLNK(info.st_mode) and not
                    getattr(info, "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT,
                    "reparse/symlink input or output: " + str(part))
    return path


def regular(path):
    path = reject_reparse(path)
    require(path.is_file(), "missing regular input file: " + str(path))
    return path


def identity(path):
    info = Path(path).stat()
    return {"fileId": str(info.st_ino), "device": str(info.st_dev), "mtimeNs": str(info.st_mtime_ns),
            "bytes": info.st_size}


def evidence_hash(mapping, suffix):
    matches = [value for key, value in mapping.items() if key.replace("\\", "/").endswith(suffix)]
    require(len(matches) == 1, "missing/ambiguous frozen evidence entry: " + suffix)
    require(isinstance(matches[0], str) and re.fullmatch("[0-9a-f]{64}", matches[0]), "bad evidence digest")
    return matches[0]


def load_baseline(p5_root):
    baseline_path = regular(ASSETS / "verification/p5-baseline.json")
    baseline = read_json(baseline_path)
    require(baseline["schema"] == "cordis.preview.baseline/1", "unsupported delivery baseline")
    records, documents = {}, {}
    for role, entry in baseline["evidence"].items():
        path = regular(Path(p5_root) / relative_path(entry["path"]))
        require(sha(path) == entry["sha256"], "frozen P5 evidence changed: " + role)
        records[role] = {"path": str(path), "sha256": entry["sha256"]}
        documents[role] = read_json(path)
    artifacts = documents["artifacts"]
    require(artifacts["passed"] is True and artifacts["base_commit"] == baseline["implementationCommit"],
            "artifact evidence has a different implementation baseline")
    require(artifacts["frozen_source_inputs"] == documents["sourceInputs"], "frozen source maps differ")
    for name, expected in documents["sourceInputs"].items():
        require(sha(regular(ROOT / relative_path(name))) == expected, "P5 compilation input changed: " + name)
    require(len(documents["productFiles"]) == 21, "expected the exact 21-piece product receipt")
    return baseline, records, documents


def profile_inputs(build, qt, baseline, documents):
    cache_path = regular(build / "CMakeCache.txt")
    cache = dict(re.findall(r"^([^#/:\n][^:\n]*):[^=\n]*=(.*)$", cache_path.read_text("utf-8-sig"), re.M))
    expected = {"CMAKE_BUILD_TYPE": "Release", "CMAKE_MSVC_RUNTIME_LIBRARY": "MultiThreadedDLL",
                "CORDIS_SHARED_CORE": "ON", "CORDIS_REQUIRE_QT5152": "ON", "CORDIS_BUILD_ELA_UI": "ON",
                "CORDIS_BUILD_EXAMPLES": "ON"}
    require(all(cache.get(key) == value for key, value in expected.items()), "requires shared Qt/Ela /MD Release build")
    require(not any("sanitize" in value.lower() for key, value in cache.items() if "FLAGS" in key),
            "ASan/instrumented profile cannot be packaged as Release")
    compiler_files = list((build / "CMakeFiles").glob("*/CMakeCXXCompiler.cmake"))
    require(len(compiler_files) == 1, "ambiguous/missing selected CMake compiler")
    compiler_path = regular(compiler_files[0])
    compiler = dict(re.findall(r'^set\((\w+) "([^"\r\n]*)"\)', compiler_path.read_text("utf-8-sig"), re.M))
    catalog_path = regular(ROOT / "qualification/native-business.json")
    require(sha(catalog_path) == baseline["qualificationSourceSha256"], "reviewed qualification source changed")
    catalog = read_json(catalog_path)
    profiles = [item for item in catalog["profiles"] if item["id"] == "ela-msvc-release"]
    require(len(profiles) == 1, "ambiguous/missing reviewed Release profile")
    profile = profiles[0]
    require(profile["compiler"] == compiler.get("CMAKE_CXX_COMPILER_ID") == "MSVC" and
            profile["compilerVersion"] == compiler.get("CMAKE_CXX_COMPILER_VERSION") and
            profile["runtime"] == "MD" and profile["buildType"] == "Release" and profile["asan"] is False and
            profile["pointerBits"] == 64 and profile["platform"] == "windows-x64" and
            profile["qtVersion"] == "5.15.2" and profile["sdkVersion"] == "0.1.5", "toolchain/profile mismatch")
    require(profile["cxxFlags"] == cache.get("CMAKE_CXX_FLAGS") and
            profile["configurationFlags"] == cache.get("CMAKE_CXX_FLAGS_RELEASE"), "compiler flags differ")
    graph = regular(build / "build.ninja")
    commands_path = regular(build / "compile_commands.json")
    commands = read_json(commands_path)
    generated = documents["artifacts"]["actual_generated_catalog_profiles"]["ela-msvc-release"]
    generated_path = regular(build / "examples/qt_workspace/native-product-catalog/native_catalog_Release.cpp")
    require(sha(generated_path) == generated["generated_source"]["sha256"], "generated product catalog changed")
    require(len(profile["products"]) == 4 and
            sorted(generated["approved_rows"]) == sorted([[p["moduleId"], p["sha256"]] for p in profile["products"]]),
            "product approval rows differ from the four reviewed entries")
    graph_text = graph.read_text("utf-8-sig").replace("\\", "/")
    for name in ENTRY_POINTS:
        require("build bin/" + name + ":" in graph_text, "entry point is not a CMake target output: " + name)
    for source in ("examples/qt/config_workbench.cpp", "examples/qt_workspace/main.cpp"):
        selected = [entry for entry in commands if entry["file"].replace("\\", "/").endswith("/" + source)]
        require(selected, "missing actual compile entry: " + source)
        for entry in selected:
            command = entry["command"]
            require(all(re.search(r"(?:^|\s)[/-]" + flag + r"(?:\s|$)", command)
                        for flag in ("MD", "O2", "DNDEBUG")) and not
                    re.search(r"(?:^|\s)[/-](?:MDd|MTd?|Od|RTC\S*)\b|sanitize", command, re.I),
                    "actual entry point compile command is not /MD Release: " + source)
    actual_qt = Path(cache["Qt5Core_DIR"]).resolve().parents[2]
    require(actual_qt == qt.resolve(), "Qt root differs from the selected build kit")
    records = [{"path": str(path), "sha256": sha(path)} for path in
               (cache_path, compiler_path, graph, commands_path, generated_path, catalog_path)]
    return profile, records


def check_license_materials():
    folder = ASSETS / "licenses"
    inventory = read_json(regular(folder / "sources.json"))
    require(inventory["schema"] == "cordis.preview.license-sources/1", "unsupported license materials inventory")
    expected = {}
    for entry in inventory["files"]:
        name = relative_path(entry["path"])
        require(name.casefold() not in expected and type(entry["bytes"]) is int and entry["bytes"] >= 0 and
                re.fullmatch("[0-9a-f]{64}", entry["sha256"]), "invalid/duplicate license material")
        source = regular(folder / name)
        require(source.stat().st_size == entry["bytes"] and sha(source) == entry["sha256"], "license material changed: " + name)
        expected[name.casefold()] = name
    actual = {path.relative_to(folder).as_posix() for path in folder.rglob("*") if path.is_file()}
    require(actual == set(expected.values()) | {"sources.json"}, "license materials inventory/file set differs")


def collect_inputs(build, qt, crt, baseline, documents):
    inputs = []

    def add(source, destination, role, expected=None):
        source = regular(source)
        original_identity = identity(source)
        digest = sha(source)
        require(identity(source) == original_identity, "source changed while collecting: " + str(source))
        require(expected is None or digest == expected, "frozen artifact changed: " + str(source))
        inputs.append({"path": relative_path(destination), "role": role, "bytes": source.stat().st_size,
                       "sha256": digest, "source": {"path": str(source), "sha256": digest,
                                                     "identity": original_identity}})

    product = documents["productFiles"]
    for name in ENTRY_POINTS:
        receipt = documents["configurationExecutables"] if name == ENTRY_POINTS[0] else product
        add(build / "bin" / name, name, "application", evidence_hash(receipt, "/bin/" + name))
    profile, build_records = profile_inputs(build, qt, baseline, documents)
    for entry in profile["dependencies"]:
        name = Path(entry["path"]).name
        origin = qt / "bin" / name if name.startswith("Qt5") else build / "bin" / name
        require(evidence_hash(product, "/bin/" + name) == entry["sha256"], "dependency receipt mismatch")
        add(origin, name, "qualified-environment", entry["sha256"])
    normal = documents["artifacts"]["normal_artifacts"]
    for alias in ("counter", "reporter", "notebook"):
        for variant in ("a", "b"):
            directory = ("qt-upgrade/" if alias == "notebook" else "native-upgrade/") + alias + "-" + variant
            manifest_name = directory + "/module.json"
            manifest_path = regular(build / "bin" / manifest_name)
            manifest = read_json(manifest_path)
            require(manifest["moduleId"] == "org.cordis.example." + alias and
                    manifest["plugins"] == ["example." + alias] and
                    manifest["moduleVersion"] == ("1.1.0" if variant == "a" else "1.2.0") and
                    manifest["loader"] == ("qt-ui" if alias == "notebook" else "c-abi") and
                    manifest["unloadPolicy"] == ("resident" if alias == "notebook" else "explicit"),
                    "business module identity mismatch")
            name = directory + "/" + relative_path(manifest["library"])
            require("/" not in manifest["library"], "module library must retain its original basename")
            frozen = [entry for entry in normal if entry["configuration"] == "ela-msvc-release" and
                      entry["manifest"] == "build/ela-msvc-release/bin/" + manifest_name]
            require(len(frozen) == 1 and frozen[0]["manifest_sha256"] == evidence_hash(product, "/bin/" + manifest_name)
                    and frozen[0]["sha256"] == manifest["sha256"] == evidence_hash(product, "/bin/" + name),
                    "business artifact differs from original P5 receipts")
            if alias != "notebook":
                approved = [entry for entry in profile["products"] if entry["manifest"] == manifest_name]
                require(len(approved) == 1 and approved[0]["sha256"] == manifest["sha256"] and
                        approved[0]["manifestSha256"] == frozen[0]["manifest_sha256"] and
                        approved[0]["moduleId"] == manifest["moduleId"], "Native approval mismatch")
            add(manifest_path, manifest_name, "module-manifest", frozen[0]["manifest_sha256"])
            add(build / "bin" / name, name, "qualified-module", manifest["sha256"])
    for name in QT_PLUGINS:
        add(qt / "plugins" / name, "plugins/" + name, "qt-deployment-plugin")
    for name in ("libEGL.dll", "libGLESv2.dll"):
        add(qt / "bin" / name, name, "qt-rendering-backend")
    for name in CRT_FILES:
        add(crt / name, name, "release-crt-deployment")
    require((ASSETS / "licenses/sources.json").is_file(), "missing third-party materials inventory")
    check_license_materials()
    require((ASSETS / "verification/verify-package.ps1").is_file(), "missing independent package verifier")
    for source in sorted(ASSETS.rglob("*")):
        reject_reparse(source)
        if source.is_dir():
            continue
        destination = source.relative_to(ASSETS).as_posix()
        require(destination in ("README.txt", "qt.conf", "verification/verify-package.ps1",
                                "verification/p5-baseline.json") or destination.startswith("licenses/"),
                "unreviewed preview asset: " + destination)
        add(source, destination, "license-material" if destination.startswith("licenses/") else "delivery-support")
    folded = [entry["path"].casefold() for entry in inputs]
    require(len(folded) == len(set(folded)), "duplicate/case-colliding package destination")
    available = {entry["path"].lower() for entry in inputs if entry["path"].lower().endswith(".dll")}
    system = set()
    for entry in inputs:
        if not entry["path"].lower().endswith((".exe", ".dll")):
            continue
        entry["pe"] = inspect_pe(entry["source"]["path"])
        entry["pe"]["fileVersion"] = file_version(entry["source"]["path"])
        if entry["role"] == "release-crt-deployment":
            require(entry["pe"]["fileVersion"] is not None, "CRT file version resource missing")
        if Path(entry["path"]).name in ("Qt5Core.dll", "Qt5Gui.dll", "Qt5Widgets.dll"):
            require(entry["pe"]["fileVersion"] == "5.15.2.0", "Qt deployment file version differs")
        for name in entry["pe"]["imports"] + entry["pe"]["delayImports"]:
            local = (PurePosixPath(entry["path"]).parent / name).as_posix().lower()
            if name in available or local in available:
                continue
            require(name[:-4] in SYSTEM_DLLS or re.fullmatch(r"(?:api|ext)-ms-win-[a-z0-9-]+\.dll", name),
                    "unresolved non-system PE dependency: " + name)
            system.add(name)
    return sorted(inputs, key=lambda entry: entry["path"]), profile, build_records, sorted(system)


def copy_inputs(inputs, destination):
    for entry in inputs:
        source = regular(entry["source"]["path"])
        require(identity(source) == entry["source"]["identity"] and sha(source) == entry["sha256"],
                "source changed before copy: " + str(source))
        target = destination / entry["path"]
        target.parent.mkdir(parents=True, exist_ok=True)
        with source.open("rb") as reader, target.open("xb") as writer:
            while chunk := reader.read(1024 * 1024):
                writer.write(chunk)
        require(identity(source) == entry["source"]["identity"] and
                sha(source) == sha(target) == entry["sha256"] and target.stat().st_size == entry["bytes"],
                "source changed during copy: " + str(source))


def write_exclusive(path, data):
    with Path(path).open("xb") as target:
        target.write(data)


def verify_payload(folder, expected):
    files = []
    for path in folder.rglob("*"):
        reject_reparse(path)
        if path.is_file():
            files.append(path.relative_to(folder).as_posix())
    require(len(files) == len(expected) and set(files) == set(expected), "payload file set changed")
    for path, digest in expected.items():
        require(sha(regular(folder / path)) == digest, "payload changed during archive verification")


def package(args):
    require(re.fullmatch(r"0\.1\.5-preview\.[1-9][0-9]*", args.delivery_version), "invalid delivery revision")
    build, qt, crt = (reject_reparse(value).resolve() for value in (args.build_dir, args.qt_root, args.crt_root))
    output = reject_reparse(args.output).resolve()
    require(not output.exists() and not output.is_symlink(), "output must be a new, absent directory")
    for source in (ROOT, build, qt, crt, reject_reparse(args.p5_root).resolve()):
        require(not source.is_relative_to(output), "output cannot contain an input tree")
    for source in (build, qt, crt):
        require(not output.is_relative_to(source), "output cannot be nested inside a build/kit input")
    baseline, evidence_records, documents = load_baseline(args.p5_root)
    inputs, profile, build_records, system = collect_inputs(build, qt, crt, baseline, documents)
    crt_versions = {entry["pe"]["fileVersion"] for entry in inputs if entry["role"] == "release-crt-deployment"}
    require(len(crt_versions) == 1, "mixed CRT deployment versions")
    tool_records = [{"path": str(path), "sha256": sha(regular(path))} for path in
                    (ROOT / "tools/package_preview.py", ROOT / "tools/preview_pe.py",
                     ASSETS / "verification/p5-baseline.json")]
    output.parent.mkdir(parents=True, exist_ok=True)
    reject_reparse(output.parent)
    output.mkdir()
    incomplete = output / "INCOMPLETE.txt"
    write_exclusive(incomplete, b"Packaging incomplete; do not distribute.\n")
    name = "Cordis-" + args.delivery_version + "-windows-x64"
    payload = output / name
    payload.mkdir()
    copy_inputs(inputs, payload)
    for entry in inputs:
        if "pe" in entry:
            copied = payload / entry["path"]
            actual_pe = inspect_pe(copied)
            actual_pe["fileVersion"] = file_version(copied)
            require(actual_pe == entry["pe"] and sha(copied) == entry["sha256"], "copied PE metadata differs")
    manifest = {"schema": SCHEMA, "deliveryVersion": args.delivery_version, "applicationVersion": "0.1.5",
                "scope": "internal-technical-preview", "platform": "windows-x64",
                "profile": {key: profile[key] for key in ("id", "compiler", "compilerVersion", "runtime",
                            "runtimeDefinition", "buildType", "asan", "pointerBits", "qtVersion", "sdkVersion")},
                "versions": {"configurationFormat": 1, "moduleManifest": 1, "businessAbi": "1.0",
                             "queryAbi": "1.0 + explicit unload 1.1", "qtPluginInterface": "1.0",
                             "elaUpstream": "aae58f5d0d98e05eabe10c263aa7da5f2491d702", "libyaml": "0.2.5",
                             "crtFileVersion": next(iter(crt_versions)),
                             "moduleSchemas": {"example.counter": {"id": "org.cordis.example.counter", "version": 1},
                                               "example.notebook": {"id": "org.cordis.example.notebook.draft", "version": 1},
                                               "example.reporter": None}},
                "qualification": {"sourceSha256": baseline["qualificationSourceSha256"],
                                  "implementationCommit": baseline["implementationCommit"],
                                  "p5CloseoutCommit": baseline["p5CloseoutCommit"],
                                  "nativeApprovalCount": 4, "qtPolicy": "resident", "evidence": evidence_records},
                "packaging": {"commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT,
                                                                 text=True).strip(),
                              "dirty": bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT)),
                              "toolInputs": tool_records, "buildInputs": build_records},
                "systemDependencies": system, "files": inputs}
    manifest_data = canonical(manifest)
    write_exclusive(payload / "build-manifest.json", manifest_data)
    sums = {entry["path"]: entry["sha256"] for entry in inputs}
    sums["build-manifest.json"] = hashlib.sha256(manifest_data).hexdigest()
    sums_data = "".join(digest + "  " + path + "\n" for path, digest in sorted(sums.items())).encode("utf-8")
    write_exclusive(payload / "SHA256SUMS.txt", sums_data)
    expected_files = {**sums, "SHA256SUMS.txt": hashlib.sha256(sums_data).hexdigest()}
    verify_payload(payload, expected_files)
    archive = output / (name + ".zip.partial")
    with zipfile.ZipFile(archive, "x", compression=zipfile.ZIP_DEFLATED) as target:
        for path in sorted(payload.rglob("*")):
            if path.is_file():
                target.write(path, name + "/" + path.relative_to(payload).as_posix())
    with zipfile.ZipFile(archive) as packed:
        expected = {name + "/" + path: digest for path, digest in expected_files.items()}
        require(len(packed.namelist()) == len(expected) and set(packed.namelist()) == set(expected), "ZIP file set differs")
        for path, digest in expected.items():
            require(hashlib.sha256(packed.read(path)).hexdigest() == digest, "ZIP bytes changed: " + path)
    for entry in inputs:
        source = regular(entry["source"]["path"])
        require(identity(source) == entry["source"]["identity"] and sha(source) == entry["sha256"],
                "source changed after packaging")
    verify_payload(payload, expected_files)
    for record in [*evidence_records.values(), *build_records, *tool_records]:
        require(sha(regular(record["path"])) == record["sha256"], "source evidence changed after packaging")
    archive = archive.rename(output / (name + ".zip"))
    receipt = {"schema": "cordis.preview.packaging/1", "passed": True, "package": name, "files": len(inputs) + 2,
               "archive": {"path": archive.name, "bytes": archive.stat().st_size, "sha256": sha(archive)},
               "manifestSha256": sums["build-manifest.json"], "sameQualifiedBytes": True,
               "packageRuntimeVerified": False, "cleanMachineVerified": False,
               "deploymentEnvironment": "app-local CRT and extra Qt plugins require P6-2 validation"}
    write_exclusive(output / "packaging.json", canonical(receipt))
    write_exclusive(output / "SHA256SUMS.txt", (receipt["archive"]["sha256"] + "  " + archive.name + "\n").encode("utf-8"))
    incomplete.unlink()
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", default=str(ROOT / "build/ela-msvc-release"))
    parser.add_argument("--qt-root", required=True)
    parser.add_argument("--crt-root", required=True)
    parser.add_argument("--p5-root", default=str(ROOT / "build"))
    parser.add_argument("--delivery-version", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    try:
        print(json.dumps(package(args), ensure_ascii=False, indent=2))
    except (ValueError, OSError, KeyError, TypeError, json.JSONDecodeError, subprocess.CalledProcessError) as error:
        parser.exit(1, "preview packaging failed: " + str(error) + "\n")


if __name__ == "__main__":
    main()
