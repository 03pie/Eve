"""Capture current fixed Native qualifications or export a review candidate from that one evidence format."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess


SCHEMA = "cordis.native-business.qualification/1"
CAPTURE = "cordis.native-business.capture/1"
REVIEWED_SOURCES = ("qualification/native-business.json", "qualification/native-business-p6-3-20261123.json")
PROFILES = {"ela-msvc-release": ("Release", "MD", False), "ela-msvc-debug": ("Debug", "MDd", False),
            "p3-q3-asan": ("RelWithDebInfo", "MD", True)}
FAULTS = {"p5.upgrade_fault_" + name for name in (
    "notebook_restore", "draft_limit", "stop_counter", "stop_notebook", "missing_query", "missing_business",
    "s09_restored", "s09_recovery_failed", "retirement")}
UPGRADES = {"p5.platform_upgrade_" + name for name in (
    "counter", "reporter", "native_roundtrip", "retained_factory", "notebook", "notebook_restore",
    "held_protection", "committed_discard", "admission", "stale_revision", "changed_source", "close_inflight")}
STRESS = {"p5.workspace_stress_" + name for name in (
    "seed_1", "seed_18446744073709551615", "replay_proof", "invalid_proof", "reduce_proof")}
TESTS = FAULTS | UPGRADES | STRESS | {"p5.workspace_endurance_native"}
PRODUCERS = ["cordis_example_" + name + "_" + variant for name in ("counter", "reporter") for variant in ("a", "b")]


def require(condition, message):
    if not condition:
        raise ValueError(message)


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, "duplicate JSON key: " + key)
        result[key] = value
    return result


def invalid_constant(value):
    raise ValueError("nonfinite JSON: " + value)


def parse_json(data):
    return json.loads(data, object_pairs_hook=unique_object, parse_constant=invalid_constant)


def read_json(path):
    return parse_json(Path(path).read_text(encoding="utf-8-sig"))


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def canonical(value):
    return (json.dumps(value, ensure_ascii=False, sort_keys=True, indent=2) + "\n").encode("utf-8")


def write_json(path, value):
    path = Path(path)
    temporary = path.with_suffix(path.suffix + ".tmp")
    with temporary.open("xb") as stream:
        stream.write(canonical(value))
    temporary.replace(path)


def passed_names(data):
    names = re.findall(rb"Test\s+#\d+:\s+(\S+)[^\n]*?\bPassed\b", data)
    require(len(names) == len(set(names)), "duplicate actual Passed name")
    return {name.decode("ascii") for name in names}


def capture_limit(value):
    if isinstance(value, dict):
        if (value.get("code"), value.get("phase"), value.get("message")) == (
                "0", "module.state", "state operation rejected, status -8"):
            return True
        return any(capture_limit(child) for child in value.values())
    return isinstance(value, list) and any(capture_limit(child) for child in value)


def target_failure(events):
    terminal = events[-1]
    signature = "notebook.capture.limit/InvalidArgument/module.state/unchanged-before-stop"
    require(terminal.get("failure_signature") == signature, "reducer retained a different product failure")
    begins = [event for event in events if event.get("event") == "action_begin" and event.get("id") == terminal["action"]]
    ends = [event for event in events if event.get("event") == "action_end" and event.get("id") == terminal["action"]]
    require(len(begins) == len(ends) == 1 and ends[0]["kind"] == "notebookUpgrade", "target has no original action pair")
    result = ends[0]["result"]
    replacement = result["report"]["replacement"]
    require(result["failure_signature"] == signature and capture_limit(replacement["errors"]) and
            replacement["committed"] is False and replacement["recovered"] is False and
            replacement["resources_retained"] is False and replacement["instances"] == [] and
            result["notebooks_before"] == result["notebooks_after"] and
            result["notebooks_before"]["notes"]["draft"] == "x" * 65537,
            "original capture failure/state retention facts differ")
    for key in ("desired_revision", "desired", "module_locators", "module_paths", "driver", "coordinator_running", "pages", "commands"):
        require(begins[0]["before"][key] == ends[0]["after"][key], "target stopped or changed original " + key)


def cache_values(tree):
    return dict(re.findall(r"^([^/#\n:]+):[^=\n]+=(.*)$",
                          (tree / "CMakeCache.txt").read_text(encoding="utf-8-sig"), re.M))


def source_inputs(root, exclude=None):
    names = subprocess.check_output(["git", "ls-files", "--cached", "--others", "--exclude-standard", "-z"], cwd=root).decode("utf-8").split("\0")
    return {name: sha(root / name) for name in names if name and
            (exclude is None or not (root / name).resolve().is_relative_to(exclude.resolve())) and (
        Path(name).suffix in {".cpp", ".hpp", ".h", ".c", ".cmake", ".py", ".ps1"} or
        Path(name).name == "CMakeLists.txt" or name in REVIEWED_SOURCES)}


def inspect_profile(root, tree, identity):
    build_type, runtime, asan = PROFILES[identity]
    cache = cache_values(tree)
    require(cache["CMAKE_BUILD_TYPE"] == build_type, "wrong current build type: " + identity)
    runtime_definition = "MultiThreadedDebugDLL" if runtime == "MDd" else "MultiThreadedDLL"
    actual_runtime = cache.get("CMAKE_MSVC_RUNTIME_LIBRARY", "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    require(actual_runtime in (runtime_definition, "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"), "wrong actual CRT: " + identity)
    flags = cache["CMAKE_CXX_FLAGS"] + " " + cache["CMAKE_CXX_FLAGS_" + build_type.upper()]
    address = lambda value: bool(re.search(r"(?:^|\s)[/-]fsanitize=address(?:\s|$)", value, re.I))
    require(address(flags) == asan, "wrong actual sanitizer flags: " + identity)
    compilers = list((tree / "CMakeFiles").glob("*/CMakeCXXCompiler.cmake"))
    require(len(compilers) == 1, "ambiguous configured compiler")
    compiler = compilers[0].read_text(encoding="utf-8")
    def compiler_value(name):
        found = re.search(r'set\(' + name + r' "([^"\n]+)"\)', compiler)
        require(found is not None, "missing compiler field " + name)
        return found.group(1)
    require(compiler_value("CMAKE_CXX_COMPILER_ID") == "MSVC" and
            compiler_value("CMAKE_CXX_SIZEOF_DATA_PTR") == "8", "unsupported actual Native compiler/platform")
    metadata = tree / "examples/qt_workspace/plugin/cordis_example_notebook_a" / build_type / "notebook_metadata.json"
    toolchain = read_json(metadata)["toolchain"]
    require(toolchain["compilerVersion"] == compiler_value("CMAKE_CXX_COMPILER_VERSION") and
            toolchain["compiler"] == "MSVC" and toolchain["runtime"] == runtime and
            toolchain["pointerBits"] == 64 and toolchain["cppStandard"] == 201703 and
            toolchain["qtVersion"] == "5.15.2", "actual dependency ABI differs")
    commands = read_json(tree / "compile_commands.json")
    products = []
    for name in ("counter", "reporter"):
        for variant, version in (("a", "1.1.0"), ("b", "1.2.0")):
            target = "cordis_example_" + name + "_" + variant
            relative = "native-upgrade/" + name + "-" + variant + "/"
            library = relative + target + ".dll"
            manifest = read_json(tree / "bin" / relative / "module.json")
            expected = {"manifestVersion": 1, "moduleId": "org.cordis.example." + name, "moduleVersion": version,
                        "loader": "c-abi", "library": target + ".dll", "sha256": sha(tree / "bin" / library),
                        "platform": "windows-x64", "abi": {"major": 1, "minor": 0},
                        "plugins": ["example." + name], "unloadPolicy": "explicit"}
            require(manifest == expected and type(manifest["manifestVersion"]) is int and
                    all(type(value) is int for value in manifest["abi"].values()), "invalid fixed Native manifest: " + library)
            producer = [entry for entry in commands if target + ".dir" in entry["output"]]
            require(len(producer) == 1, "actual Native producer entry is missing/ambiguous")
            command = producer[0]["command"]
            require("CORDIS_EXAMPLE_EXPLICIT_UNLOAD=1" in command and
                    address(command) == asan and
                    re.search(r"(?:^|\s)[/-]" + runtime + r"(?:\s|$)", command), "actual Native producer flags differ")
            source = "examples/native_services/" + name + ".cpp"
            require(Path(producer[0]["file"]).resolve() == (root / source).resolve(), "Native source identity differs")
            products.append({"moduleId": manifest["moduleId"], "pluginId": manifest["plugins"][0],
                             "moduleVersion": version, "loader": "c-abi", "unloadPolicy": "explicit",
                             "sha256": manifest["sha256"], "manifestSha256": sha(tree / "bin" / relative / "module.json"),
                             "library": library, "manifest": relative + "module.json", "producerSource": source,
                             "producerSha256": sha(root / source), "producerCommand": command,
                             "compileCommandSha256": hashlib.sha256(command.encode("utf-8")).hexdigest()})
    profile = {"id": identity, "platform": "windows-x64", "compiler": "MSVC",
               "compilerVersion": toolchain["compilerVersion"], "buildType": build_type, "runtime": runtime,
               "runtimeDefinition": actual_runtime, "asan": asan,
               "cxxFlags": cache["CMAKE_CXX_FLAGS"], "configurationFlags": cache["CMAKE_CXX_FLAGS_" + build_type.upper()],
               "sdkVersion": toolchain["sdkVersion"], "qtVersion": toolchain["qtVersion"], "pointerBits": 64,
               "cppStandard": 201703, "products": products}
    suffix = "d" if runtime == "MDd" else ""
    dependencies = [tree / "bin" / ("cordis_" + name + suffix + ".dll") for name in ("core", "qt", "ui")]
    dependencies.append(tree / "bin" / ("ElaWidgetTools" + suffix + ".dll"))
    qt_root = Path(cache["Qt5Core_DIR"]).parents[2]
    dependencies += [qt_root / "bin" / ("Qt5" + name + suffix + ".dll") for name in ("Core", "Gui", "Widgets")]
    profile["dependencies"] = [{"role": "measured-environment", "path": path.as_posix(), "sha256": sha(path)}
                               for path in dependencies]
    return profile, cache, [compilers[0], metadata, tree / "CMakeCache.txt", tree / "compile_commands.json"]


class Archive:
    def __init__(self, directory):
        self.directory = directory
        self.records = []
        self.current = {}
        (directory / "files").mkdir()

    def save(self, original, role, current=True):
        original = Path(original).resolve()
        data = original.read_bytes()
        saved = "files/" + str(len(self.records) + 1).zfill(5) + "-" + original.name
        (self.directory / saved).write_bytes(data)
        record = {"role": role, "sourcePath": original.as_posix(), "path": saved,
                  "sha256": hashlib.sha256(data).hexdigest()}
        self.records.append(record)
        if current:
            self.current[original.as_posix()] = record
        return record


def run_command(arguments, output, timeout):
    with output.open("xb") as stream:
        process = subprocess.run(arguments, stdout=stream, stderr=subprocess.STDOUT, timeout=timeout, check=False)
    return {"arguments": [str(value) for value in arguments], "exit": process.returncode}


def dry_run(tree, targets, cache, path):
    # The single entry references the original graph unchanged. Ninja still uses this tree's real log/deps.
    entry = path.with_suffix(".ninja")
    entry.write_bytes(b"subninja build.ninja\n")
    result = run_command([cache["CMAKE_MAKE_PROGRAM"], "-C", str(tree), "-f", str(entry.resolve()),
                          "-n", *sorted(targets)], path, 60)
    output = path.read_bytes()
    lines = [line.strip() for line in output.decode("utf-8", errors="replace").splitlines() if line.strip()]
    harmless = lambda line: line.startswith("ninja: Entering directory") or line == "ninja: no work to do."
    require(result["exit"] == 0 and "ninja: no work to do." in lines and all(harmless(line) for line in lines),
            "qualification requires up-to-date targets; configure/build first (see " + str(path) + ")")
    result["queryEntry"] = entry.as_posix()
    return result


def refresh_graph(tree, cache, path):
    # Only the real CMake/Ninja metadata target is requested; never a producer/consumer build target.
    result = run_command([cache["CMAKE_MAKE_PROGRAM"], "-C", str(tree), "-j", "1", "build.ninja"], path, None)
    require(result["exit"] == 0, "CMake/Ninja metadata refresh failed; configure/build first (see " + str(path) + ")")
    return result


def select_tests(tree, ctest, regex, path):
    selected = run_command([ctest, "--test-dir", str(tree), "--show-only=json-v1", "-R", regex], path, 60)
    require(selected["exit"] == 0, "CTest selection failed")
    tests = read_json(path)["tests"]
    names = [test["name"] for test in tests]
    require(len(names) == len(set(names)) == 27 and set(names) == TESTS, "actual fixed qualification set differs")
    require(not any(prop["value"] for test in tests for prop in test["properties"] if prop["name"] == "DISABLED"),
            "qualification contains a disabled case")
    require(all(any(prop["name"] == "TIMEOUT" and 0 < prop["value"] <= 900 for prop in test["properties"])
                for test in tests), "qualification needs the original bounded per-test process timeout")
    return selected, tests


def archive_attempt_outputs(archive, tree, tests):
    result = {"proofDirectories": [], "captureErrors": []}
    def copy(path, role):
        try:
            return archive.save(path, role)
        except (OSError, ValueError) as failure:
            result["captureErrors"].append(str(failure))
            return None
    last = copy(tree / "Testing/Temporary/LastTest.log", "post-attempt raw proof stdout")
    result["lastTest"] = last
    if last:
        try:
            data = (archive.directory / last["path"]).read_bytes()
            directories = [Path(value.decode("utf-8").strip()) for value in
                           re.findall(rb"^Proof evidence: (.+)$", data, re.M)]
            result["proofDirectories"] = [path.as_posix() for path in directories]
            for directory in directories:
                require(directory.is_dir(), "printed proof directory is missing: " + str(directory))
                for path in sorted(directory.rglob("*")):
                    if path.is_file():
                        copy(path, "post-attempt independent proof output")
        except (OSError, ValueError) as failure:
            result["captureErrors"].append(str(failure))
    for test in tests:
        if test["name"] == "p5.workspace_endurance_native" or test["name"].startswith("p5.workspace_stress_seed_"):
            for extension in ((".json", ".jsonl") if test["name"] == "p5.workspace_endurance_native" else
                              (".actions.json", ".jsonl")):
                copy(test["command"][-1] + extension, "post-attempt fixed qualification output")
    return result


def qualify(root, build_root, run_directory, ctest):
    require(not run_directory.exists(), "qualification run directory already exists")
    run_directory.mkdir(parents=True)
    archive = Archive(run_directory)
    inputs = source_inputs(root, run_directory)
    write_json(run_directory / "source-inputs.json", inputs)
    document = {"schema": CAPTURE, "status": "Incomplete", "checkoutCommit": subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(), "sourceInputs": archive.save(
            run_directory / "source-inputs.json", "qualification checkout source snapshot"), "profiles": [], "files": []}
    try:
        for identity in PROFILES:
            tree = (build_root / identity).resolve()
            profile, cache, metadata = inspect_profile(root, tree, identity)
            prefix = run_directory / identity
            prefix.mkdir()
            regex = "^(" + "|".join(re.escape(name) for name in sorted(TESTS)) + ")$"
            initial_selection = prefix / "selected-before-refresh.json"
            initial_selected, initial_tests = select_tests(tree, ctest, regex, initial_selection)
            executables = {Path(test["command"][0]).resolve() for test in initial_tests}
            selection = prefix / "selected.json"
            for path in metadata + sorted(executables):
                archive.save(path, "before-refresh selected compiler/configuration/executable")
            stable = {str(path): sha(path) for path in executables}
            for product in profile["products"]:
                for field in ("library", "manifest"):
                    path = tree / "bin" / product[field]
                    stable[str(path)] = sha(path)
                    archive.save(path, "actual measured Native " + field)
            for dependency in profile["dependencies"]:
                stable[dependency["path"]] = dependency["sha256"]
                archive.save(dependency["path"], "actual measured dependency")
            refreshed = refresh_graph(tree, cache, prefix / "metadata-refresh.log")
            require(all(sha(Path(path)) == expected for path, expected in stable.items()) and
                    source_inputs(root, run_directory) == inputs,
                    "metadata refresh changed sources or producer/consumer/dependency bytes")
            current_profile, cache, metadata = inspect_profile(root, tree, identity)
            require(current_profile == profile, "metadata refresh changed actual profile/producer; configure/build first")
            selected_run, tests = select_tests(tree, ctest, regex, selection)
            require({Path(test["command"][0]).resolve() for test in tests} == executables,
                    "metadata refresh changed selected consumers; configure/build first")
            targets = {path.stem for path in executables} | set(PRODUCERS)
            before = dry_run(tree, targets, cache, prefix / "before-dryrun.log")
            for path in metadata:
                archive.save(path, "actual refreshed compiler/configuration")
            graph_files = [tree / "build.ninja", tree / "CMakeFiles/rules.ninja", tree / ".ninja_log", tree / ".ninja_deps"]
            graph_hashes = {str(path): sha(path) for path in graph_files}
            for path in graph_files:
                archive.save(path, "actual unchanged Ninja graph/log/deps used for freshness")
            for test in tests:
                if test["name"] == "p5.workspace_endurance_native" or test["name"].startswith("p5.workspace_stress_seed_"):
                    for extension in (".json", ".jsonl", ".actions.json"):
                        path = Path(test["command"][-1] + extension)
                        if path.exists():
                            archive.save(path, "preserved prior fixed output", current=False)
            previous_last = tree / "Testing/Temporary/LastTest.log"
            if previous_last.exists():
                archive.save(previous_last, "preserved prior raw CTest output", current=False)
            # CTest owns its test process tree and per-test timeout; do not kill only the parent CTest process.
            execution = run_command([ctest, "--test-dir", str(tree), "-j", "1", "--output-on-failure", "--stop-on-failure",
                                     "--timeout", "900", "-R", regex], prefix / "ctest.log", None)
            row = {"profile": profile, "buildDirectory": tree.as_posix(), "selected": archive.save(
                selection, "actual selected names"), "selectionCommand": selected_run, "execution": execution,
                "log": archive.save(prefix / "ctest.log", "actual CTest exit/output"),
                "metadataRefresh": {**refreshed, "log": archive.save(prefix / "metadata-refresh.log", "explicit metadata-only refresh")},
                "selectionBeforeRefresh": {**initial_selected, "log": archive.save(initial_selection, "actual selection before refresh")},
                "beforeDryRun": {**before, "log": archive.save(prefix / "before-dryrun.log", "actual freshness check"),
                                 "entry": archive.save(before["queryEntry"], "single-entry reference to original Ninja graph")}}
            document["profiles"].append(row)
            row.update(archive_attempt_outputs(archive, tree, tests))
            require(execution["exit"] == 0 and passed_names((prefix / "ctest.log").read_bytes()) == TESTS,
                    "actual qualification tests failed")
            require(not row["captureErrors"] and len(row["proofDirectories"]) == len(set(row["proofDirectories"])) == 3,
                    "actual qualification outputs were not completely archived")
            after = dry_run(tree, targets, cache, prefix / "after-dryrun.log")
            require(all(sha(Path(path)) == expected for path, expected in stable.items()) and source_inputs(root, run_directory) == inputs,
                    "sources, executable or measured Native/dependency bytes changed during qualification")
            require(inspect_profile(root, tree, identity)[0] == profile, "actual profile/producer changed during qualification")
            require(all(sha(Path(path)) == expected for path, expected in graph_hashes.items()),
                    "actual Ninja graph/log/deps changed during runtime qualification")
            row["afterDryRun"] = {**after, "log": archive.save(prefix / "after-dryrun.log", "actual freshness check"),
                                  "entry": archive.save(after["queryEntry"], "single-entry reference to original Ninja graph")}
            document["files"] = archive.records
            write_json(run_directory / "qualification.json", document)
        document["status"] = "Completed"
    except Exception as failure:
        document["status"] = "Failed"
        document["failure"] = str(failure)
        raise
    finally:
        document["files"] = archive.records
        write_json(run_directory / "qualification.json", document)


def export_candidate(root, evidence, output):
    require(not output.exists() and output not in {root / name for name in REVIEWED_SOURCES},
            "candidate exporter cannot overwrite an existing output or the reviewed approval source")
    capture = read_json(evidence)
    require(capture["schema"] == CAPTURE and capture["status"] == "Completed", "incomplete/unknown current capture")
    require(set(capture) == {"schema", "status", "checkoutCommit", "sourceInputs", "profiles", "files"},
            "unknown current capture fields")
    bundle = evidence.parent
    records = capture["files"]
    current, verified = {}, {}
    for record in records:
        require(set(record) == {"role", "sourcePath", "path", "sha256"}, "unknown captured file fields")
        require(record["path"] not in verified, "duplicate captured archive path")
        path = (bundle / record["path"]).resolve()
        require(path.is_relative_to(bundle) and sha(path) == record["sha256"], "captured evidence bytes changed")
        verified[record["path"]] = (record, path)
        if record["role"] not in {"preserved prior fixed output", "preserved prior raw CTest output"}:
            current[record["sourcePath"]] = record
    def saved(record):
        require(isinstance(record, dict) and record.get("path") in verified and
                record == verified[record["path"]][0], "capture reference is not an exact verified archived record")
        return verified[record["path"]][1]
    require(read_json(saved(capture["sourceInputs"])) == source_inputs(root, bundle), "qualification source checkout changed")
    profiles = []
    require(len(capture["profiles"]) == 3, "incomplete current profile capture")
    for row in capture["profiles"]:
        profile = row["profile"]
        require(profile["id"] in PROFILES and profile["id"] not in {p["id"] for p in profiles}, "duplicate/unknown profile")
        tree = Path(row["buildDirectory"])
        require(inspect_profile(root, tree, profile["id"])[0] == profile, "current packaging bytes/profile changed")
        tests = read_json(saved(row["selected"]))["tests"]
        require({test["name"] for test in tests} == TESTS and len(tests) == 27 and row["execution"]["exit"] == 0 and
                passed_names(saved(row["log"]).read_bytes()) == TESTS, "actual selected/exit/Passed evidence differs")
        refresh = row["metadataRefresh"]
        require(refresh["exit"] == 0 and refresh["arguments"][3:] == ["-j", "1", "build.ninja"],
                "capture did not explicitly refresh only CMake/Ninja metadata")
        saved(refresh["log"])
        require("-j" in row["execution"]["arguments"] and
                row["execution"]["arguments"][row["execution"]["arguments"].index("-j") + 1] == "1",
                "qualification was not explicitly serial")
        targets = {Path(test["command"][0]).stem for test in tests} | set(PRODUCERS)
        for key in ("beforeDryRun", "afterDryRun"):
            check = row[key]
            require(check["exit"] == 0 and b"ninja: no work to do." in saved(check["log"]).read_bytes() and
                    saved(check["entry"]).read_bytes() == b"subninja build.ninja\n" and
                    check["arguments"][1] == "-C" and Path(check["arguments"][2]).resolve() == tree.resolve() and
                    check["arguments"][3] == "-f" and Path(check["arguments"][4]).resolve() == Path(check["entry"]["sourcePath"]).resolve() and
                    check["arguments"][5:] == ["-n", *sorted(targets)], "capture did not query the actual full target graph")
        endurance = next(test for test in tests if test["name"] == "p5.workspace_endurance_native")
        jsonl = current[Path(endurance["command"][-1] + ".jsonl").resolve().as_posix()]
        events = [parse_json(line) for line in saved(jsonl).read_text(encoding="utf-8").splitlines()]
        completed = [event for event in events if event["event"] == "round_complete"]
        require(len(events) == 219 and len(completed) == 108, "incomplete original H02 trace")
        operations, observed = set(), set()
        for event in completed:
            report = event["report"]
            replacement = report["replacement"]
            require(replacement["outcome"] == "0" and replacement["committed"] is True and
                    replacement["recovered"] is False and replacement["resources_retained"] is False and
                    replacement["errors"] == [] and report["retained"] is False and report["delivery_blocked"] is False and
                    event["old_image_absent_file_exclusive"] is True and
                    event["explicit_save_confirmed"] is True and event["reporter_value_verified"] is True,
                    "H02 actual business/physical report failed")
            operations.add(replacement["operation"])
            for generation in event["catalog_detail"]["generations"]:
                observed.add((generation["module"], generation["version"], generation["sha256"]))
        require(len(operations) == 108 and sum(event["context"]["warmup"] is True for event in completed) == 8 and
                all((p["moduleId"], p["moduleVersion"], p["sha256"]) in observed for p in profile["products"]),
                "H02 did not observe all four measured Native identities")
        summary = events[-1]["summary"]
        require(events[-1]["event"] == "complete" and summary["warmup_applied"] == "8" and
                summary["measured_applied"] == "100" and summary["total_original_applied"] == "108" and
                summary["final_counter_result"] == "163", "H02 original final business result differs")
        proof_counts = {}
        for original in row["proofDirectories"]:
            proof = read_json(saved(current[(Path(original) / "proof.json").resolve().as_posix()]))
            kind = "reduce" if proof.get("schema") == "cordis.workspace.reduction-proof/1" else proof.get("proof")
            require(kind in {"normal-replay", "invalid-trace", "reduce"} and kind not in proof_counts,
                    "unknown or duplicate proof evidence")
            children = proof["children"] if kind == "normal-replay" else proof["cases"] if kind == "invalid-trace" else [r["child"] for r in proof["candidates"]]
            statuses, pids = Counter(), set()
            for child in children:
                require(child["status"] != "HarnessError" and child["actual_pid"] not in pids and int(child["actual_pid"]) > 0,
                        "actual proof child process failed")
                pids.add(child["actual_pid"])
                statuses[child["status"]] += 1
                child_events = [parse_json(line) for line in saved(current[Path(child["evidence_path"]).resolve().as_posix()]).read_text(encoding="utf-8").splitlines()]
                require(child_events[-1]["status"] == child["status"] and child["exit_code"] == {"Passed": 0, "InvalidTrace": 21, "Failed": 20}[child["status"]] and
                        [e["sequence"] for e in child_events] == [str(i) for i in range(1, len(child_events) + 1)], "original child exit/sequence differs")
                if child["status"] == "Failed":
                    target_failure(child_events)
                if kind == "normal-replay":
                    trace = read_json(saved(current[Path(child["input_path"]).resolve().as_posix()]))
                    begins = [event for event in child_events if event["event"] == "action_begin"]
                    require(len(begins) == 34 and [{key: event[key] for key in ("id", "kind", "args")} for event in begins] == trace["actions"],
                            "replay did not interpret its original saved actions")
                if kind == "invalid-trace" and child["case"] == "building-window-before-capture":
                    require(any(event["event"] == "run_exception" and capture_limit(event.get("after", {}).get("upgrade")) for event in child_events),
                            "missed Building window lacks the actual original capture rejection")
                for name in ("input", "evidence"):
                    require(current[Path(child[name + "_path"]).resolve().as_posix()]["sha256"] == child[name + "_sha256"], "original child bytes differ")
            expected = {"Passed": 6} if kind == "normal-replay" else {"InvalidTrace": 20} if kind == "invalid-trace" else {"Passed": 6, "Failed": 4}
            require(statuses == expected, "incomplete actual replay/invalid/reduction qualification")
            if kind == "reduce":
                require(proof["minimality"] == "deletion-minimal" and len(proof["original_actions"]) == 7 and
                        [(a["id"], a["kind"]) for a in proof["minimal_actions"]] == [("5", "draft"), ("6", "notebookUpgrade")] and
                        proof["minimal_actions"][0]["args"]["text"] == "x" * 65537,
                        "actual reduction does not retain the two original target actions")
                controls = {candidate["purpose"]: candidate for candidate in proof["candidates"] if candidate["purpose"] in {
                    "final-single-deletion-0", "final-single-deletion-1", "exact-65536-byte-success-control"}}
                require(len(controls) == 3 and all(candidate["child"]["status"] == "Passed" for candidate in controls.values()),
                        "actual single-deletion/boundary controls are incomplete")
            proof_counts[kind] = len(children)
        require(proof_counts == {"normal-replay": 6, "invalid-trace": 20, "reduce": 10}, "qualification proof set differs")
        profiles.append(profile)
    candidate = {"schema": SCHEMA, "profiles": profiles, "provenance": {
        "method": "current-capture", "exportCheckoutCommit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
        "exporterSha256": sha(__file__), "pendingStages": ["M5-e2", "M5-e3"],
        "qualificationSources": [{"stage": CAPTURE, "commit": capture["checkoutCommit"],
                                   "inputManifest": saved(capture["sourceInputs"]).as_posix(),
                                   "inputManifestSha256": capture["sourceInputs"]["sha256"]}],
        "evidence": [{"role": record["role"], "path": saved(record).as_posix(), "sha256": record["sha256"]} for record in records]}}
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("xb") as stream:
        stream.write(canonical(candidate))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--qualify", action="store_true")
    parser.add_argument("--build-root", type=Path)
    parser.add_argument("--run-dir", type=Path)
    parser.add_argument("--ctest", default="ctest")
    parser.add_argument("--evidence", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    root = args.repo_root.resolve()
    if args.qualify:
        require(args.run_dir is not None and args.evidence is None and args.output is None, "--qualify requires a new --run-dir; export is separate")
        qualify(root, (args.build_root or root / "build").resolve(), args.run_dir.resolve(), shutil.which(args.ctest) or args.ctest)
        print(json.dumps({"capture": str(args.run_dir.resolve() / "qualification.json"), "reviewedSourceUpdated": False}))
    else:
        require(args.evidence is not None and args.output is not None and args.run_dir is None and args.build_root is None,
                "export requires --evidence and --output")
        export_candidate(root, args.evidence.resolve(), args.output.resolve())
        print(json.dumps({"candidate": str(args.output.resolve()), "profiles": 3, "nativeDlls": 12,
                          "reviewedSourceUpdated": False, "releaseAndDesktopAcceptancePending": True}))


if __name__ == "__main__":
    try:
        main()
    except (ValueError, TypeError, KeyError, StopIteration, OSError, subprocess.SubprocessError) as failure:
        raise SystemExit("qualification rejected: " + str(failure)) from failure
