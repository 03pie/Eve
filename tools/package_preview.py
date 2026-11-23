"""Package one explicitly selected, reviewed baseline through the same byte-copy closure."""
import argparse
import hashlib
import importlib.util
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
CONFIGURATION_SOURCE = "examples/qt/config_workbench.cpp"
HOST_EVIDENCE_ROLES = {"asanBuild", "asanTestLog", "asanTests", "debugBuild", "debugTestLog", "debugTests",
                       "originalComponents", "originalUnicodeFailure", "releaseArtifact", "releaseBuild",
                       "releaseTestLog", "releaseTests", "unchangedComponents", "unicodeCompletion"}
SUPPORT_ASSETS = {"README.txt", "qt.conf", "verification/verify-package.ps1", "verification/p5-baseline.json",
                  "verification/record-desktop.ps1", "verification/preview-windows.psm1",
                  "verification/preview-ui.psm1", "verification/configuration-host.json",
                  "verification/preview-acl.ps1"}
REVALIDATED_ASSET = "verification/revalidated-baseline.json"
REVALIDATED_SCHEMA = "cordis.preview.revalidated-baseline/1"
REVALIDATED_CATALOG = "qualification/native-business-p6-3-20261123.json"
PROFILE_IDS = {"ela-msvc-release", "ela-msvc-debug", "p3-q3-asan"}
PRODUCT_TESTS = {"p5.platform_product_" + name for name in
                 ("catalog", "roundtrip", "empty", "wrong_hash", "changed_candidate", "restart")}
ADOPTION_PATHS = {"cmake/NativeProductCatalog.cmake", "tools/export_native_qualification.py",
                  REVALIDATED_CATALOG, "tests/tools/test_native_qualification.py"}


def revalidated(baseline):
    return baseline.get("schema") == REVALIDATED_SCHEMA


def require_sha(value):
    require(isinstance(value, str) and re.fullmatch("[0-9a-f]{64}", value), "invalid reviewed SHA256")
    return value


def reviewed_record(root, record):
    require(isinstance(record, dict) and set(record) == {"path", "sha256"}, "invalid reviewed evidence record")
    name = relative_path(record["path"])
    path = regular(Path(root) / name)
    require(sha(path) == require_sha(record["sha256"]), "reviewed evidence changed: " + name)
    return path, {"path": str(path), "sha256": record["sha256"]}


def release_paths():
    paths = set(ENTRY_POINTS) | {"cordis_core.dll", "cordis_qt.dll", "cordis_ui.dll", "ElaWidgetTools.dll",
                               "Qt5Core.dll", "Qt5Gui.dll", "Qt5Widgets.dll"}
    for alias in ("counter", "reporter", "notebook"):
        for variant in ("a", "b"):
            directory = ("qt-upgrade/" if alias == "notebook" else "native-upgrade/") + alias + "-" + variant
            paths |= {directory + "/module.json", directory + "/cordis_example_" + alias + "_" + variant + ".dll"}
    return paths


def check_file_map(value, expected):
    require(isinstance(value, dict) and set(value) == set(expected), "reviewed Release file set differs")
    for name, facts in value.items():
        relative_path(name)
        require(isinstance(facts, dict) and set(facts) == {"bytes", "sha256"} and
                type(facts["bytes"]) is int and facts["bytes"] >= 0, "invalid reviewed file metadata")
        require_sha(facts["sha256"])


def command_option(command, option):
    require(command.count(option) == 1 and command.index(option) + 1 < len(command), "invalid recorded command option: " + option)
    return command[command.index(option) + 1]


def source_reader(root):
    """Reuse the original source rule and fixed test evidence interpretation."""
    exporter = regular(root / "tools/export_native_qualification.py")
    spec = importlib.util.spec_from_file_location("cordis_reviewed_packaging_source_reader", exporter)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def current_source_inputs(root, exclude):
    return source_reader(root).source_inputs(root, exclude)


def load_revalidated_baseline(evidence_root):
    path = regular(ASSETS / REVALIDATED_ASSET)
    baseline = read_json(path)
    require(isinstance(baseline, dict) and set(baseline) == {
        "schema", "implementationCommit", "qualificationSource", "evidence", "review"} and
        baseline["schema"] == REVALIDATED_SCHEMA, "invalid fixed revalidated baseline schema")
    require(isinstance(baseline["implementationCommit"], str) and
            re.fullmatch("[0-9a-f]{40}", baseline["implementationCommit"]), "invalid captured checkout commit")
    source = baseline["qualificationSource"]
    require(isinstance(source, dict) and set(source) == {"path", "sha256"} and
            source["path"] == REVALIDATED_CATALOG, "revalidated catalog path is not the fixed reviewed source")
    catalog_path = regular(ROOT / REVALIDATED_CATALOG)
    require(sha(catalog_path) == require_sha(source["sha256"]), "reviewed qualification source changed")
    require(isinstance(baseline["evidence"], dict) and set(baseline["evidence"]) == {
        "capture", "qualificationReview", "sourceInputs", "productFiles", "consumer"}, "incomplete revalidated evidence roles")
    records = {"baseline": {"path": str(path), "sha256": sha(path)}}
    documents, names = {}, set()
    for role, entry in [*baseline["evidence"].items(), ("review", baseline["review"])]:
        evidence, record = reviewed_record(evidence_root, entry)
        require(str(evidence).casefold() not in names, "duplicate revalidated evidence path")
        names.add(str(evidence).casefold())
        records[role], documents[role] = record, read_json(evidence)
    candidate = {key: value for key, value in baseline.items() if key != "review"}
    review = documents["review"]
    require(isinstance(review, dict) and review.get("schema") == "cordis.preview.revalidated-review/1" and review.get("passed") is True and
            review.get("candidateSha256") == hashlib.sha256(canonical(candidate)).hexdigest() and
            all(review.get(role + "Sha256") == records[role]["sha256"] for role in
                ("capture", "sourceInputs", "productFiles", "consumer")), "review does not bind this revalidated baseline")
    sources = documents["sourceInputs"]
    require(isinstance(sources, dict) and "tools/export_native_qualification.py" in sources and
            sha(regular(ROOT / "tools/export_native_qualification.py")) == require_sha(sources["tools/export_native_qualification.py"]),
            "reviewed source reader changed")
    reader = source_reader(ROOT)
    catalog, capture = read_json(catalog_path), documents["capture"]
    require(isinstance(catalog, dict) and isinstance(capture, dict) and catalog.get("schema") == "cordis.native-business.qualification/1" and
            capture.get("schema") == "cordis.native-business.capture/1" and capture.get("status") == "Completed" and
            capture.get("checkoutCommit") == baseline["implementationCommit"], "incomplete or different Native capture")
    qualification_review = documents["qualificationReview"]
    require(isinstance(qualification_review, dict) and qualification_review.get("schema") == "cordis.native-business.review/1" and
            qualification_review.get("passed") is True and qualification_review.get("candidateSha256") == source["sha256"] and
            qualification_review.get("captureSha256") == records["capture"]["sha256"], "Native review does not bind candidate/capture")
    bundle = Path(records["capture"]["path"]).parent.resolve()
    archived = {}
    for record in capture["files"]:
        require(isinstance(record, dict) and set(record) == {"role", "sourcePath", "path", "sha256"}, "invalid Native archive record")
        saved = regular(bundle / relative_path(record["path"]))
        require(record["path"] not in archived and sha(saved) == require_sha(record["sha256"]), "Native archive bytes changed/duplicated")
        archived[record["path"]] = (record, saved)
    def saved(record):
        require(isinstance(record, dict) and record.get("path") in archived and
                record == archived[record["path"]][0], "Native reference is not an exact archived record")
        return archived[record["path"]][1]
    captured_sources = read_json(saved(capture["sourceInputs"]))
    profiles = catalog["profiles"]
    require(len(profiles) == len(capture["profiles"]) == 3 and
            {p["id"] for p in profiles} == {row["profile"]["id"] for row in capture["profiles"]} == PROFILE_IDS,
            "revalidation requires exactly three measured profiles")
    for row in capture["profiles"]:
        matches = [p for p in profiles if p["id"] == row["profile"]["id"]]
        require(len(matches) == 1 and matches[0] == row["profile"] and len(matches[0]["products"]) == 4,
                "reviewed catalog differs from captured four-product profile")
        measured = []
        for product in matches[0]["products"]:
            require(product["producerSha256"] == captured_sources[product["producerSource"]], "captured Native producer source differs")
            for field, digest in (("library", product["sha256"]), ("manifest", product["manifestSha256"])):
                measured.append(("actual measured Native " + field, Path(row["buildDirectory"]) / "bin" / product[field], digest))
        measured += [("actual measured dependency", Path(item["path"]), item["sha256"]) for item in matches[0]["dependencies"]]
        require(len(matches[0]["dependencies"]) == 7, "captured dependency set differs")
        for role, original, digest in measured:
            found = [record for record in capture["files"] if record["role"] == role and
                     Path(record["sourcePath"]).resolve() == original.resolve()]
            require(found and all(record["sha256"] == digest for record in found), "captured producer/dependency file is absent or ambiguous")
        selected = read_json(saved(row["selected"]))["tests"]
        expected = reader.TESTS
        require(len(selected) == 27 and {t["name"] for t in selected} == expected and
                type(row["execution"]["exit"]) is int and row["execution"]["exit"] == 0 and
                reader.passed_names(saved(row["log"]).read_bytes()) == expected, "actual Native27 evidence differs")
    provenance = catalog["provenance"]
    require(provenance["method"] == "current-capture" and provenance["pendingStages"] == ["M5-e2", "M5-e3"] and
            provenance["exporterSha256"] == captured_sources["tools/export_native_qualification.py"] and
            provenance["qualificationSources"] == [{"stage": "cordis.native-business.capture/1", "commit": capture["checkoutCommit"],
                "inputManifest": saved(capture["sourceInputs"]).as_posix(), "inputManifestSha256": capture["sourceInputs"]["sha256"]}] and
            provenance["evidence"] == [{"role": record["role"], "path": saved(record).as_posix(), "sha256": record["sha256"]}
                                       for record in capture["files"]], "catalog does not retain the exact current capture archive")
    required_sources = {CONFIGURATION_SOURCE, "examples/qt_workspace/main.cpp", "examples/native_services/counter.cpp",
                        "examples/native_services/reporter.cpp", "cmake/NativeProductCatalog.cmake",
                        "tools/export_native_qualification.py", "qualification/native-business.json", REVALIDATED_CATALOG}
    require(required_sources <= set(sources), "current source map omits fixed compilation/qualification inputs")
    for name, digest in sources.items():
        require(sha(regular(ROOT / relative_path(name))) == require_sha(digest), "current compilation input changed: " + name)
    require(sources == current_source_inputs(ROOT, bundle), "current complete source map differs")
    require(sources[REVALIDATED_CATALOG] == source["sha256"] and
            sources["qualification/native-business.json"] == "6c0c5324e0e064667c79ae10ff9cf2d48754cbffd49f376d338d4e8cd91786ec",
            "new or historical trusted catalog identity differs")
    consumer = documents["consumer"]
    require(isinstance(consumer, dict) and consumer.get("schema") == "cordis.preview.revalidated-consumer/1" and
            consumer.get("qualificationSourceSha256") == source["sha256"] and
            all(consumer.get(role + "Sha256") == records[role]["sha256"] for role in
                ("capture", "sourceInputs", "productFiles")), "consumer does not bind the reviewed inputs")
    changes = {name: {"before": captured_sources.get(name), "after": sources.get(name)}
               for name in set(captured_sources) | set(sources) if captured_sources.get(name) != sources.get(name)}
    require({"cmake/NativeProductCatalog.cmake", REVALIDATED_CATALOG} <= set(changes) <= ADOPTION_PATHS and
            consumer.get("adoptionChanges") == changes and
            all(item["after"] is not None for item in changes.values()), "unreviewed source adoption delta")
    products = documents["productFiles"]
    check_file_map(products, release_paths())
    producers = release_paths() - set(ENTRY_POINTS)
    check_file_map(consumer["producerBefore"], producers)
    check_file_map(consumer["producerAfter"], producers)
    require(consumer["producerBefore"] == consumer["producerAfter"] == {name: products[name] for name in producers},
            "producer/dependency bytes changed during catalog adoption")
    generated = consumer["generatedCatalog"]
    require(isinstance(generated, dict) and set(generated) == {"path", "sha256", "approvedRows"} and
            generated["path"] == "examples/qt_workspace/native-product-catalog/native_catalog_Release.cpp",
            "invalid generated consumer catalog record")
    require_sha(generated["sha256"])
    validations = consumer["validations"]
    require(len(validations) == 3 and {v["profile"] for v in validations} == PROFILE_IDS, "incomplete product consumer validations")
    for validation in validations:
        selected_path, selected_record = reviewed_record(evidence_root, validation["selected"])
        log_path, log_record = reviewed_record(evidence_root, validation["log"])
        records["consumerSelected-" + validation["profile"]] = selected_record
        records["consumerLog-" + validation["profile"]] = log_record
        selected = read_json(selected_path)["tests"]
        command = validation["command"]
        captured = next(row for row in capture["profiles"] if row["profile"]["id"] == validation["profile"])
        tree = Path(captured["buildDirectory"])
        require(isinstance(command, list) and command and all(isinstance(arg, str) for arg in command) and
                Path(command[0]).name.lower() in {"ctest", "ctest.exe"} and
                Path(command[0]).resolve() == Path(captured["execution"]["arguments"][0]).resolve() and
                Path(command_option(command, "--test-dir")).resolve() == tree.resolve() and command_option(command, "-j") == "1" and
                type(validation["exitCode"]) is int and validation["exitCode"] == 0 and
                len(selected) == 6 and {t["name"] for t in selected} == PRODUCT_TESTS and
                reader.passed_names(log_path.read_bytes()) == PRODUCT_TESTS, "actual product consumer validation differs")
        require(all(len(test["command"]) == 3 and
                    Path(test["command"][0]).resolve() == (tree / "bin/cordis_platform_product_tests.exe").resolve() and
                    Path(test["command"][1]).resolve() == (tree / "bin").resolve() and
                    test["command"][2] == test["name"].removeprefix("p5.platform_product_") for test in selected),
                "selected product tests do not bind the actual captured build tree")
    documents["catalogPath"] = catalog_path
    documents["captureBundle"] = bundle
    documents["revalidated"] = True
    documents["generatedCatalog"] = generated
    documents["capturedEvidenceRecords"] = [{"path": str(value[1]), "sha256": value[0]["sha256"]} for value in archived.values()]
    documents["configurationHostRecords"] = []
    documents["compilationSourceRecords"] = [{"path": str(ROOT / name), "sha256": digest} for name, digest in sources.items()]
    return baseline, records, documents


def require(condition, message):
    if not condition:
        raise ValueError(message)


def qualification_sha(baseline):
    return baseline["qualificationSource"]["sha256"] if revalidated(baseline) else baseline["qualificationSourceSha256"]


def product_digest(documents, name):
    if documents.get("revalidated"):
        return documents["productFiles"][name]["sha256"]
    return evidence_hash(documents["productFiles"], "/bin/" + name)


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


def load_configuration_host(p5_root, documents):
    """One fixed source/EXE repair, never an alternate plugin/source approval mechanism."""
    path = regular(ASSETS / "verification/configuration-host.json")
    records = [{"path": str(path), "sha256": sha(path)}]
    host = read_json(path)
    require(isinstance(host, dict) and set(host) == {"schema", "profile", "source", "executable", "evidence", "review"}
            and host["schema"] == "cordis.preview.configuration-host/1" and host["profile"] == "ela-msvc-release",
            "invalid reviewed configuration host schema/profile")
    for field, fixed_path, previous in (
            ("source", CONFIGURATION_SOURCE, documents["sourceInputs"][CONFIGURATION_SOURCE]),
            ("executable", ENTRY_POINTS[0], evidence_hash(documents["configurationExecutables"], "/bin/" + ENTRY_POINTS[0]))):
        entry = host[field]
        require(isinstance(entry, dict) and set(entry) == {"path", "previousSha256", "sha256"}
                and entry["path"] == fixed_path, "configuration host cannot override another source/executable")
        require(entry["previousSha256"] == previous and isinstance(entry["sha256"], str)
                and re.fullmatch("[0-9a-f]{64}", entry["sha256"]) and entry["sha256"] != previous,
                "configuration host old/new digest does not match its P5 predecessor: " + field)
    require(isinstance(host["evidence"], dict) and set(host["evidence"]) == HOST_EVIDENCE_ROLES,
            "configuration host requires the exact reviewed evidence roles")
    seen = set()
    for role, entry in [*host["evidence"].items(), ("review", host["review"])]:
        require(isinstance(entry, dict) and set(entry) == {"path", "sha256"}
                and isinstance(entry["sha256"], str) and re.fullmatch("[0-9a-f]{64}", entry["sha256"]),
                "invalid configuration host evidence record: " + role)
        name = relative_path(entry["path"])
        require(name.casefold() not in seen, "configuration host evidence paths must be unique")
        seen.add(name.casefold())
        evidence = regular(Path(p5_root) / name)
        require(sha(evidence) == entry["sha256"], "configuration host evidence changed: " + role)
        records.append({"path": str(evidence), "sha256": entry["sha256"]})
    review = read_json(Path(p5_root) / host["review"]["path"])
    candidate = {key: value for key, value in host.items() if key != "review"}
    require(isinstance(review, dict) and review.get("schema") == "cordis.preview.host-review/1"
            and review.get("passed") is True
            and review.get("candidateSha256") == hashlib.sha256(canonical(candidate)).hexdigest()
            and review.get("configurationSourceSha256") == host["source"]["sha256"]
            and review.get("configurationExecutableSha256") == host["executable"]["sha256"],
            "configuration host review does not bind this candidate/source/executable")
    return host, records


def load_baseline(p5_root):
    baseline_path = regular(ASSETS / "verification/p5-baseline.json")
    baseline = read_json(baseline_path)
    require(baseline["schema"] == "cordis.preview.baseline/1", "unsupported delivery baseline")
    require(set(baseline["evidence"]) == {"artifacts", "sourceInputs", "productFiles", "configurationExecutables"},
            "expected the original four P5 evidence records")
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
    catalog = regular(ROOT / "qualification/native-business.json")
    require(sha(catalog) == baseline["qualificationSourceSha256"], "reviewed qualification source changed")
    require(len(documents["productFiles"]) == 21, "expected the exact 21-piece product receipt")
    host, host_records = load_configuration_host(p5_root, documents)
    source_records = []
    for name, expected in documents["sourceInputs"].items():
        if name == CONFIGURATION_SOURCE:
            expected = host["source"]["sha256"]
        source = regular(ROOT / relative_path(name))
        require(sha(source) == expected, "P5 compilation input changed: " + name)
        source_records.append({"path": str(source), "sha256": expected})
    documents["configurationHost"] = host
    documents["configurationHostRecords"] = host_records
    documents["compilationSourceRecords"] = source_records
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
    catalog_path = regular(documents.get("catalogPath", ROOT / "qualification/native-business.json"))
    require(sha(catalog_path) == qualification_sha(baseline), "reviewed qualification source changed")
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
    generated = documents["generatedCatalog"] if revalidated(baseline) else documents["artifacts"]["actual_generated_catalog_profiles"]["ela-msvc-release"]
    generated_path = regular(build / "examples/qt_workspace/native-product-catalog/native_catalog_Release.cpp")
    generated_sha = generated["sha256"] if revalidated(baseline) else generated["generated_source"]["sha256"]
    generated_rows = generated["approvedRows"] if revalidated(baseline) else generated["approved_rows"]
    require(sha(generated_path) == generated_sha, "generated product catalog changed")
    require(len(profile["products"]) == 4 and
            sorted(generated_rows) == sorted([[p["moduleId"], p["sha256"]] for p in profile["products"]]),
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
        if revalidated(baseline) and destination in release_paths():
            frozen = documents["productFiles"][destination]
            require(source.stat().st_size == frozen["bytes"] and digest == frozen["sha256"], "reviewed Release bytes changed: " + destination)
        inputs.append({"path": relative_path(destination), "role": role, "bytes": source.stat().st_size,
                       "sha256": digest, "source": {"path": str(source), "sha256": digest,
                                                     "identity": original_identity}})

    for name in ENTRY_POINTS:
        if name == ENTRY_POINTS[0] and not revalidated(baseline):
            add(build / "bin" / name, name, "reviewed-configuration-host", documents["configurationHost"]["executable"]["sha256"])
        else:
            add(build / "bin" / name, name, "application", product_digest(documents, name))
    profile, build_records = profile_inputs(build, qt, baseline, documents)
    for entry in profile["dependencies"]:
        name = Path(entry["path"]).name
        origin = qt / "bin" / name if name.startswith("Qt5") else build / "bin" / name
        require(product_digest(documents, name) == entry["sha256"], "dependency receipt mismatch")
        add(origin, name, "qualified-environment", entry["sha256"])
    normal = [] if revalidated(baseline) else documents["artifacts"]["normal_artifacts"]
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
            if revalidated(baseline):
                frozen = [{"manifest_sha256": product_digest(documents, manifest_name), "sha256": product_digest(documents, name)}]
            else:
                frozen = [entry for entry in normal if entry["configuration"] == "ela-msvc-release" and
                          entry["manifest"] == "build/ela-msvc-release/bin/" + manifest_name]
            require(len(frozen) == 1 and frozen[0]["manifest_sha256"] == product_digest(documents, manifest_name)
                    and frozen[0]["sha256"] == manifest["sha256"] == product_digest(documents, name),
                    "business artifact differs from reviewed receipts")
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
    selected_assets = (SUPPORT_ASSETS - {"verification/p5-baseline.json", "verification/configuration-host.json"}) | {REVALIDATED_ASSET} if revalidated(baseline) else SUPPORT_ASSETS
    for name in selected_assets:
        regular(ASSETS / name)
    for source in sorted(ASSETS.rglob("*")):
        reject_reparse(source)
        if source.is_dir():
            continue
        destination = source.relative_to(ASSETS).as_posix()
        require(destination in SUPPORT_ASSETS | {REVALIDATED_ASSET} or destination.startswith("licenses/"),
                "unreviewed preview asset: " + destination)
        if destination in (SUPPORT_ASSETS | {REVALIDATED_ASSET}) - selected_assets:
            continue
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
    fresh = getattr(args, "revalidated", False)
    require(type(fresh) is bool, "invalid baseline adapter selection")
    if fresh:
        require(int(args.delivery_version.rsplit(".", 1)[1]) >= 3, "revalidated baseline requires preview.3 or later")
        require(getattr(args, "p5_root", None) is None and getattr(args, "evidence_root", None),
                "revalidated adapter requires only an explicit --evidence-root")
        evidence_root = args.evidence_root
    else:
        require(getattr(args, "evidence_root", None) is None, "--evidence-root requires the explicit revalidated adapter")
        evidence_root = getattr(args, "p5_root", None) or ROOT / "build"
    build, qt, crt = (reject_reparse(value).resolve() for value in (args.build_dir, args.qt_root, args.crt_root))
    output = reject_reparse(args.output).resolve()
    require(not output.exists() and not output.is_symlink(), "output must be a new, absent directory")
    for source in (ROOT, build, qt, crt, reject_reparse(evidence_root).resolve()):
        require(not source.is_relative_to(output), "output cannot contain an input tree")
    for source in (build, qt, crt):
        require(not output.is_relative_to(source), "output cannot be nested inside a build/kit input")
    baseline, evidence_records, documents = load_revalidated_baseline(evidence_root) if fresh else load_baseline(evidence_root)
    inputs, profile, build_records, system = collect_inputs(build, qt, crt, baseline, documents)
    crt_versions = {entry["pe"]["fileVersion"] for entry in inputs if entry["role"] == "release-crt-deployment"}
    require(len(crt_versions) == 1, "mixed CRT deployment versions")
    tool_records = [{"path": str(path), "sha256": sha(regular(path))} for path in
                    (ROOT / "tools/package_preview.py", ROOT / "tools/preview_pe.py",
                     ASSETS / (REVALIDATED_ASSET if fresh else "verification/p5-baseline.json"))]
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
                "qualification": {"sourceSha256": qualification_sha(baseline),
                                  "implementationCommit": baseline["implementationCommit"],
                                  "nativeApprovalCount": 4, "qtPolicy": "resident", "evidence": evidence_records},
                "packaging": {"commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT,
                                                                 text=True).strip(),
                              "dirty": bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT)),
                              "toolInputs": tool_records, "buildInputs": build_records},
                "systemDependencies": system, "files": inputs}
    if fresh:
        manifest["qualification"]["baselineKind"] = "fresh-revalidated"
        manifest["qualification"]["sourcePath"] = REVALIDATED_CATALOG
        manifest["qualification"]["record"] = {"path": REVALIDATED_ASSET, "sha256": evidence_records["baseline"]["sha256"]}
    else:
        manifest["qualification"]["p5CloseoutCommit"] = baseline["p5CloseoutCommit"]
        manifest["configurationHost"] = {**documents["configurationHost"],
            "record": {"path": "verification/configuration-host.json", "sha256": documents["configurationHostRecords"][0]["sha256"]}}
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
    for record in [*evidence_records.values(), *build_records, *tool_records,
                   *documents["configurationHostRecords"], *documents["compilationSourceRecords"],
                   *documents.get("capturedEvidenceRecords", [])]:
        require(sha(regular(record["path"])) == record["sha256"], "source evidence changed after packaging")
    if fresh:
        require(documents["sourceInputs"] == current_source_inputs(ROOT, documents["captureBundle"]),
                "complete source map changed after packaging")
    archive = archive.rename(output / (name + ".zip"))
    receipt = {"schema": "cordis.preview.packaging/1", "passed": True, "package": name, "files": len(inputs) + 2,
               "archive": {"path": archive.name, "bytes": archive.stat().st_size, "sha256": sha(archive)},
               "manifestSha256": sums["build-manifest.json"],
               "packageRuntimeVerified": False, "cleanMachineVerified": False,
               "deploymentEnvironment": "app-local CRT and extra Qt plugins require P6-2 validation"}
    if fresh:
        receipt.update({"baselineKind": "fresh-revalidated", "sameRevalidatedBytes": True,
                        "qualificationSourceSha256": qualification_sha(baseline)})
    else:
        receipt.update({"sameP5ModuleAndRuntimeBytes": True, "p5BusinessProductUnchanged": True,
                       "reviewedConfigurationHostSha256": documents["configurationHost"]["executable"]["sha256"]})
    write_exclusive(output / "packaging.json", canonical(receipt))
    write_exclusive(output / "SHA256SUMS.txt", (receipt["archive"]["sha256"] + "  " + archive.name + "\n").encode("utf-8"))
    incomplete.unlink()
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", default=str(ROOT / "build/ela-msvc-release"))
    parser.add_argument("--qt-root", required=True)
    parser.add_argument("--crt-root", required=True)
    parser.add_argument("--p5-root", help="original historical evidence root (default: build)")
    parser.add_argument("--revalidated", action="store_true", help="use the one fixed reviewed fresh baseline")
    parser.add_argument("--evidence-root", help="external evidence location for the revalidated baseline")
    parser.add_argument("--delivery-version", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    try:
        print(json.dumps(package(args), ensure_ascii=False, indent=2))
    except (ValueError, OSError, KeyError, TypeError, json.JSONDecodeError, subprocess.CalledProcessError) as error:
        parser.exit(1, "preview packaging failed: " + str(error) + "\n")


if __name__ == "__main__":
    main()
