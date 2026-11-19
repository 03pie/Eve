"""Observe startup failures in fresh preview copies; never alter or terminate the source package."""
import argparse
import contextlib
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import subprocess
import sys
import threading
import time
from datetime import datetime, timezone


TOOLS = Path(__file__).resolve().parent
SHARED_WINDOWS = TOOLS.parent / "delivery/windows/verification/preview-windows.psm1"
ACL_SCRIPT = TOOLS.parent / "delivery/windows/verification/preview-acl.ps1"
PACKAGE_NAME = "cordis_platform_workbench_demo.exe"
COUNTER_B = "native-upgrade/counter-b/cordis_example_counter_b.dll"
MANIFEST_B = "native-upgrade/counter-b/module.json"
CASES = ("missing-source-counter-b", "tampered-source-counter-b", "mismatched-manifest-sha",
         "incompatible-module-abi", "missing-core-library")
MAX_LOG_BYTES = 4 * 1024 * 1024


def require(condition, message):
    if not condition:
        raise ValueError(message)


def utc():
    return datetime.now(timezone.utc).isoformat()


def sha(path):
    result = hashlib.sha256()
    with Path(path).open("rb") as source:
        while block := source.read(1024 * 1024):
            result.update(block)
    return result.hexdigest()


def safe_path(path):
    absolute = Path(os.path.abspath(path))
    for entry in (absolute, *absolute.parents):
        if entry.exists() or entry.is_symlink():
            info = entry.lstat()
            require(not stat.S_ISLNK(info.st_mode) and not
                    getattr(info, "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT,
                    "reparse/symlink path refused: " + str(entry))
    return absolute.resolve()


def file_identity(path):
    info = safe_path(path).stat()
    require(info.st_nlink == 1 or stat.S_ISDIR(info.st_mode), "hard-linked file refused: " + str(path))
    return {"volume": str(info.st_dev), "index": str(info.st_ino)}


def snapshot(root):
    root = safe_path(root)
    require(root.is_dir(), "snapshot requires an existing directory")
    result = {}
    for path in sorted(root.rglob("*")):
        safe_path(path)
        if path.is_file():
            file_identity(path)
            result[path.relative_to(root).as_posix()] = {"bytes": path.stat().st_size, "sha256": sha(path)}
        else:
            require(path.is_dir(), "non-regular snapshot entry")
    return result


def data_snapshot(root):
    """The baseline root may also contain evidence; only the actual locator/store are data inputs."""
    root = safe_path(root)
    session = safe_path(root / "session.yaml")
    require(session.is_file(), "saved baseline session.yaml is missing")
    file_identity(session)
    return {"session.yaml": {"bytes": session.stat().st_size, "sha256": sha(session)},
            **{"store/" + name: row for name, row in snapshot(root / "store").items()}}


def write_json(path, value):
    with Path(path).open("x", encoding="utf-8", newline="\n") as target:
        json.dump(value, target, ensure_ascii=False, indent=2)
        target.write("\n")


def copy_tree(source, destination, expected, data_only=False):
    """Exclusive byte copies, without inherited source ACLs, links, or changed inputs."""
    source, destination = safe_path(source), safe_path(destination)
    source_snapshot = data_snapshot if data_only else snapshot
    require(source_snapshot(source) == expected, "copy source differs from its recorded baseline")
    destination.mkdir()
    directories = ([source / "store", *[path for path in (source / "store").rglob("*") if path.is_dir()]]
                   if data_only else [path for path in source.rglob("*") if path.is_dir()])
    for directory in sorted(directories):
        safe_path(directory)
        (destination / directory.relative_to(source)).mkdir(parents=True, exist_ok=True)
    for relative, record in expected.items():
        origin, target = source / relative, destination / relative
        original = (file_identity(origin), origin.stat().st_mtime_ns)
        target.parent.mkdir(parents=True, exist_ok=True)
        with origin.open("rb") as reader, target.open("xb") as writer:
            while block := reader.read(1024 * 1024):
                writer.write(block)
        require(original == (file_identity(origin), origin.stat().st_mtime_ns) and
                sha(origin) == sha(target) == record["sha256"] and target.stat().st_size == record["bytes"],
                "source changed during exclusive copy")
    require(source_snapshot(source) == expected and snapshot(destination) == expected, "copy file set changed")


def powershell():
    system = Path(os.environ.get("SystemRoot", r"C:\Windows"))
    return str(system / "System32/WindowsPowerShell/v1.0/powershell.exe")


def shared_observation(function, process_id=None):
    require(function in ("Get-PreviewHostFacts", "Get-PreviewObservation"), "unsupported observation")
    module = safe_path(SHARED_WINDOWS)
    require(module.is_file(), "shared Windows observer is missing")
    command = "[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false); Import-Module '" + str(module).replace("'", "''") + "' -Force -ErrorAction Stop; " + function
    if process_id is not None:
        command += " -ProcessId " + str(int(process_id))
    command += " | ConvertTo-Json -Depth 16 -Compress"
    import base64
    encoded = base64.b64encode(command.encode("utf-16le")).decode("ascii")
    result = subprocess.run([powershell(), "-NoProfile", "-NonInteractive", "-EncodedCommand", encoded],
                            capture_output=True, timeout=20, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    require(result.returncode == 0, "shared observer failed: " + result.stderr.decode("utf-8", errors="replace"))
    return json.loads(result.stdout.decode("utf-8-sig"))


def child_environment(environment=None):
    environment = dict(os.environ if environment is None else environment)
    for key in list(environment):
        upper = key.upper()
        if (upper.startswith(("QT", "QML", "VSCMD", "VSINSTALL", "VCINSTALL", "CMAKE", "CONDA")) or
                upper in {"INCLUDE", "LIB", "LIBPATH", "CL", "_CL_", "LINK", "_LINK_", "PYTHONPATH"}):
            del environment[key]
    system = environment.get("SystemRoot", environment.get("SYSTEMROOT", r"C:\Windows"))
    for key in list(environment):
        if key.upper() == "PATH":
            del environment[key]
    environment["PATH"] = os.pathsep.join((str(Path(system) / "System32"), system,
                                         str(Path(system) / "System32/Wbem")))
    environment["QT_QPA_PLATFORM"] = "windows"
    return environment


@contextlib.contextmanager
def inherited_error_mode():
    require(os.name == "nt", "startup observations require Windows")
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.GetErrorMode.restype = ctypes.c_uint32
    kernel.SetErrorMode.argtypes = [ctypes.c_uint32]
    kernel.SetErrorMode.restype = ctypes.c_uint32
    old = kernel.GetErrorMode()
    selected = old | 0x0001 | 0x0002 | 0x8000
    kernel.SetErrorMode(selected)
    try:
        yield {"previous": old, "inherited": selected, "restoredAfterCreateProcess": True}
    finally:
        kernel.SetErrorMode(old)


def observe_child(arguments, cwd, directory, timeout, observe=shared_observation):
    """One attempt, bounded capture; unknown/live is returned without retry or termination."""
    require(timeout > 0, "timeout must be positive")
    directory = safe_path(directory)
    streams = {}
    threads = []
    started, started_utc = time.monotonic(), utc()
    with inherited_error_mode() as mode:
        process = subprocess.Popen([str(value) for value in arguments], cwd=str(cwd), env=child_environment(),
                                   stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0), shell=False)

    def drain(name, stream):
        info = streams[name]
        with (directory / (name + ".raw")).open("xb") as output:
            try:
                while block := stream.read(65536):
                    room = max(0, MAX_LOG_BYTES - info["storedBytes"])
                    output.write(block[:room])
                    output.flush()
                    info["storedBytes"] += min(room, len(block))
                    info["receivedBytes"] += len(block)
            except (OSError, ValueError) as error:
                info["readError"] = str(error)
            finally:
                stream.close()

    for name, stream in (("stdout", process.stdout), ("stderr", process.stderr)):
        streams[name] = {"storedBytes": 0, "receivedBytes": 0, "readError": None}
        thread = threading.Thread(target=drain, args=(name, stream), daemon=True)
        threads.append(thread)
        thread.start()
    observations, errors = [], []
    while process.poll() is None and time.monotonic() - started < timeout:
        try:
            observations.append(observe("Get-PreviewObservation", process.pid))
        except (OSError, ValueError, subprocess.SubprocessError) as error:
            errors.append(str(error))
        if process.poll() is None:
            time.sleep(min(0.1, max(0, timeout - (time.monotonic() - started))))
    exited = process.poll() is not None
    if exited:
        for thread in threads:
            thread.join(timeout=2)
    result = {"arguments": [str(value) for value in arguments], "workingDirectory": str(cwd), "pid": process.pid,
              "startedUtc": started_utc, "elapsedSeconds": time.monotonic() - started, "exitObserved": exited,
              "exitCode": process.returncode if exited else None,
              "windowsExitUnsigned": process.returncode & 0xffffffff if exited else None,
              "livePid": None if exited else process.pid, "status": "exited" if exited else "unknown-live",
              "errorMode": mode, "observations": observations, "observationErrors": errors,
              "streams": streams, "captureComplete": exited and not any(t.is_alive() for t in threads) and
              all(info["readError"] is None for info in streams.values()),
              "captureTruncated": any(info["receivedBytes"] > info["storedBytes"] for info in streams.values()),
              "processTerminatedByTool": False, "retryCount": 0}
    if result["captureComplete"]:
        result["logs"] = {name: {"path": str(directory / (name + ".raw")),
                                  "sha256": sha(directory / (name + ".raw"))} for name in streams}
    return result


def verify_source(package, output, expected_manifest=None):
    verifier = package / "verification/verify-package.ps1"
    require(verifier.is_file(), "package lacks its independent verifier")
    digest = sha(package / "build-manifest.json")
    require(expected_manifest is None or digest == expected_manifest, "external manifest anchor differs")
    command = [powershell(), "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File", str(verifier),
               "-PackageRoot", str(package), "-ExpectedManifestSha256", digest, "-ReportPath", str(output / "verification.json")]
    result = subprocess.run(command, capture_output=True, timeout=60,
                            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    for name, data in (("stdout", result.stdout), ("stderr", result.stderr)):
        with (output / ("verification-" + name + ".raw")).open("xb") as target:
            target.write(data)
    write_json(output / "verification-command.json", {"arguments": command, "exitCode": result.returncode,
                                                       "externalAnchorSuppliedByCaller": expected_manifest is not None})
    require(result.returncode == 0, "original package failed independent integrity verification")
    return digest


def mutate(package, case):
    if case == "missing-source-counter-b":
        (package / COUNTER_B).unlink()
        return {"path": COUNTER_B, "change": "removed", "stderrContains": "cannot open stable module artifact"}
    if case == "tampered-source-counter-b":
        with (package / COUNTER_B).open("ab") as target:
            target.write(b"p6-negative-copy-only-overlay")
        return {"path": COUNTER_B, "change": "appended overlay without manifest update", "stderrContains": "SHA256 mismatch"}
    if case in ("mismatched-manifest-sha", "incompatible-module-abi"):
        path = package / MANIFEST_B
        value = json.loads(path.read_text(encoding="utf-8-sig"))
        if case == "mismatched-manifest-sha":
            value["sha256"] = "0" * 64
            message = "SHA256 mismatch"
        else:
            value["abi"]["major"] = 2
            message = "unsupported declared ABI (requires 1.0)"
        path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
        return {"path": MANIFEST_B, "change": case, "stderrContains": message}
    require(case == "missing-core-library", "unknown failure case")
    (package / "cordis_core.dll").unlink()
    return {"path": "cordis_core.dll", "change": "removed", "windowsExitUnsigned": 0xc0000135}


def prepare_data(baseline, destination, baseline_snapshot):
    if baseline is not None:
        copy_tree(baseline, destination, baseline_snapshot, data_only=True)
    else:
        destination.mkdir()
        (destination / "store").mkdir()
    return snapshot(destination)


def prepare_permissions(package, baseline, output, manifest_sha256):
    """The shipped PowerShell script is the only copy/registration producer for ACLs."""
    scope = output / "permissions"
    command = [powershell(), "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File", str(ACL_SCRIPT),
               "-Action", "Prepare", "-ScopeParent", str(scope), "-SourcePackage", str(package),
               "-ExpectedManifestSha256", manifest_sha256]
    if baseline is not None:
        command += ["-BaselineDataRoot", str(baseline)]
    result = subprocess.run(command, capture_output=True, timeout=180,
                            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    for name, data in (("stdout", result.stdout), ("stderr", result.stderr)):
        with (output / ("permissions-prepare-" + name + ".raw")).open("xb") as target:
            target.write(data)
    write_json(output / "permissions-prepare-command.json", {"arguments": command, "exitCode": result.returncode})
    require(result.returncode == 0, "stock PowerShell ACL Prepare failed; see preserved raw output")
    prepared = json.loads((scope / "prepare-report.json").read_text(encoding="utf-8-sig"))
    require(prepared["schema"] == "cordis.preview.acl-preparation/1" and prepared["passed"] is True and
            safe_path(prepared["aclScopeParent"]) == scope and prepared["protectionApplied"] is False,
            "ACL Prepare returned an incomplete or different scope")
    return prepared


def run(args):
    require(os.name == "nt", "this developer-machine tool requires Windows")
    package, output = safe_path(args.package_root), safe_path(args.output)
    require(package.is_dir() and not output.exists(), "package must exist and output must be new")
    require(not package.is_relative_to(output) and not output.is_relative_to(package), "output/package overlap refused")
    baseline = safe_path(args.baseline_data_root) if args.baseline_data_root else None
    if baseline is not None:
        require(baseline.is_dir() and (baseline / "session.yaml").is_file() and (baseline / "store").is_dir(),
                "baseline must contain the actual session.yaml and store/; locators are never rewritten")
        require(not baseline.is_relative_to(output) and not output.is_relative_to(baseline), "output/baseline overlap refused")
    require(args.timeout_seconds > 0 and args.timeout_seconds <= 120, "timeout must be in (0,120]")
    require(isinstance(args.expected_manifest_sha256, str) and
            re.fullmatch("[0-9a-f]{64}", args.expected_manifest_sha256), "an external manifest SHA256 anchor is required")
    output.parent.mkdir(parents=True, exist_ok=True)
    safe_path(output.parent)
    output.mkdir()
    write_json(output / "incomplete.json", {"schema": "cordis.preview.failure-run-incomplete/1", "startedUtc": utc()})
    original = snapshot(package)
    baseline_snapshot = data_snapshot(baseline) if baseline else None
    report = {"schema": "cordis.preview.failure-observations/1", "startedUtc": utc(), "passed": False,
              "preparationOnly": args.prepare_only or args.prepare_acl_only, "packageRoot": str(package),
              "baselineDataRoot": str(baseline) if baseline else None, "oldDataBaselineProvided": baseline is not None,
              "oldDataPreservationClaimed": False,
              "baselineBefore": baseline_snapshot, "sourceBefore": original, "cases": [],
              "sourceInputs": {str(path): sha(path) for path in (Path(__file__), SHARED_WINDOWS, ACL_SCRIPT)},
              "scope": "developer-machine startup negatives and ACL-copy preparation; no clean-machine/GUI/save qualification"}
    try:
        initial = output / "source-verification"
        initial.mkdir()
        report["manifestSha256"] = verify_source(package, initial, args.expected_manifest_sha256)
        declared = json.loads((package / "build-manifest.json").read_text(encoding="utf-8-sig"))
        expected_paths = {entry["path"] for entry in declared["files"]} | {"build-manifest.json", "SHA256SUMS.txt"}
        require(set(original) == expected_paths, "verified package inventory differs from its manifest")
        require(all(name in original for name in (PACKAGE_NAME, COUNTER_B, MANIFEST_B, "cordis_core.dll")),
                "package lacks the concrete startup-negative inputs")
        report["sourceFileCount"] = len(original)
        report["hostFacts"] = shared_observation("Get-PreviewHostFacts")
        report["aclPreparation"] = prepare_permissions(package, baseline, output, report["manifestSha256"])
        for key, prepared_key in (("aclScopeParent", "aclScopeParent"), ("aclRegistration", "registration"),
                                  ("aclApplication", "application"), ("aclData", "data")):
            report[key] = report["aclPreparation"][prepared_key]
        if not args.prepare_acl_only:
            for case in CASES:
                directory = output / case
                directory.mkdir()
                verified = directory / "source-verification"
                verified.mkdir()
                verify_source(package, verified, report["manifestSha256"])
                require(snapshot(package) == original, "original package changed between cases")
                app, data = directory / "application", directory / "data"
                copy_tree(package, app, original)
                before_data = prepare_data(baseline, data, baseline_snapshot)
                change = mutate(app, case)
                before_app = snapshot(app)
                row = {"case": case, "mutation": change, "applicationBefore": before_app, "dataBefore": before_data,
                       "passed": False, "preparedOnly": report["preparationOnly"]}
                report["cases"].append(row)
                if args.prepare_only:
                    continue
                work = directory / "working-directory"
                work.mkdir()
                observed = observe_child([app / PACKAGE_NAME, data / "session.yaml", data / "store"], work,
                                         directory, args.timeout_seconds)
                row["process"] = observed
                if not observed["exitObserved"]:
                    write_json(directory / "observation.json", row)
                    report["remainingCasesNotStarted"] = list(CASES[len(report["cases"]):])
                    break
                row["applicationAfter"], row["dataAfter"] = snapshot(app), snapshot(data)
                stderr = (directory / "stderr.raw").read_bytes().decode("utf-8", errors="replace")
                rejected = (observed["windowsExitUnsigned"] == change["windowsExitUnsigned"] if "windowsExitUnsigned" in change
                            else observed["exitCode"] == 1 and change["stderrContains"] in stderr)
                row["passed"] = (rejected and observed["captureComplete"] and not observed["captureTruncated"] and
                                 not observed["observationErrors"] and
                                 row["applicationAfter"] == before_app and row["dataAfter"] == before_data)
                write_json(directory / "observation.json", row)
                if not row["passed"]:
                    report["remainingCasesNotStarted"] = list(CASES[len(report["cases"]):])
                    break
        report["sourceAfter"] = snapshot(package)
        report["baselineAfter"] = data_snapshot(baseline) if baseline else None
        require(report["sourceAfter"] == original and report["baselineAfter"] == baseline_snapshot,
                "original package or baseline data changed")
        require(all(sha(path) == digest for path, digest in report["sourceInputs"].items()),
                "observation/ACL tool source changed during the run")
        report["prepared"] = True
        report["passed"] = (not report["preparationOnly"] and len(report["cases"]) == len(CASES) and
                            all(row["passed"] for row in report["cases"]))
        report["oldDataPreservationClaimed"] = report["passed"] and baseline is not None
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
        report["error"] = str(error)
    finally:
        report["finishedUtc"] = utc()
        write_json(output / "report.json", report)
    if report.get("prepared") and (report["passed"] or report["preparationOnly"]):
        (output / "incomplete.json").unlink()
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package-root", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--baseline-data-root")
    parser.add_argument("--expected-manifest-sha256", required=True)
    parser.add_argument("--timeout-seconds", type=float, default=30)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--prepare-only", action="store_true", help="copy/mutate only; never launch a product")
    mode.add_argument("--prepare-acl-only", action="store_true", help="only create registered app/data copies for ACL/UI validation")
    args = parser.parse_args()
    if args.expected_manifest_sha256 and not re.fullmatch("[0-9a-f]{64}", args.expected_manifest_sha256):
        parser.error("expected manifest SHA256 must be lowercase hexadecimal")
    try:
        result = run(args)
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.exit(1, str(error) + "\n")
    print(json.dumps({key: result.get(key) for key in ("passed", "prepared", "preparationOnly", "error")}, indent=2))
    return 0 if result["passed"] or result.get("prepared") and result["preparationOnly"] else 1


if __name__ == "__main__":
    sys.exit(main())
