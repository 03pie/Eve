"""Stock PowerShell and controlled non-GUI children; never start a Cordis application."""
import base64
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SUPPORT = ROOT / "delivery/windows/verification/preview-windows.psm1"
UI = ROOT / "delivery/windows/verification/preview-ui.psm1"
RECORDER = ROOT / "delivery/windows/verification/record-desktop.ps1"
PS = Path(os.environ.get("SystemRoot", "C:/Windows")) / "System32/WindowsPowerShell/v1.0/powershell.exe"


def quote(value):
    return "'" + str(value).replace("'", "''") + "'"


@unittest.skipUnless(os.name == "nt" and PS.is_file(), "Windows PowerShell 5.1 is required")
class PreviewDesktopTests(unittest.TestCase):
    def run_ps(self, code, timeout=30):
        source = "[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false);$ErrorActionPreference='Stop';"
        source += "Import-Module " + quote(SUPPORT) + " -Force;" + code
        command = [str(PS), "-NoProfile", "-NonInteractive", "-EncodedCommand",
                   base64.b64encode(source.encode("utf-16le")).decode()]
        result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout, check=False)
        self.assertEqual(result.returncode, 0, result.stderr.decode("utf-8", errors="replace"))
        return json.loads(result.stdout.decode("utf-8-sig"))

    def test_scripts_are_valid_ps51_bom_crlf_and_only_snapshot_ui_is_exported(self):
        for path in (SUPPORT, UI, RECORDER):
            data = path.read_bytes()
            self.assertTrue(data.startswith(b"\xef\xbb\xbf"))
            self.assertNotIn(b"\n", data.replace(b"\r\n", b""))
        value = self.run_ps("$errors=@(); foreach($p in @(" + ",".join(quote(p) for p in (SUPPORT, UI, RECORDER)) + ")){"
                            "$t=$null;$e=$null;[void][Management.Automation.Language.Parser]::ParseFile($p,[ref]$t,[ref]$e);$errors+=@($e)};"
                            "Import-Module " + quote(UI) + " -Force; @{errors=@($errors);exports=@((Get-Command -Module preview-ui).Name)}|ConvertTo-Json -Depth 4")
        self.assertEqual(value["errors"], [])
        self.assertEqual(value["exports"], ["Get-PreviewUiSnapshot"])

    def test_real_host_token_and_same_pid_modules_are_observed_without_gui(self):
        value = self.run_ps("@{facts=(Get-PreviewHostFacts);observation=(Get-PreviewObservation -ProcessId $PID);pid=$PID}|ConvertTo-Json -Depth 10")
        self.assertEqual(value["observation"]["pid"], value["pid"])
        self.assertFalse(value["observation"]["liveDllFilesRead"])
        self.assertTrue(any(Path(row["path"]).name.lower() == "powershell.exe" for row in value["observation"]["modules"]))
        self.assertFalse(value["facts"]["installationAbsenceProven"])
        self.assertIsInstance(value["facts"]["token"]["elevated"], bool)
        self.assertGreater(value["facts"]["acp"], 0)
        self.assertTrue(value["facts"]["preferencesFile"].endswith("CordisConfigWorkbench\\ui-preferences.json"))

    def test_module_observation_keeps_supplementary_unicode_from_the_actual_process(self):
        with tempfile.TemporaryDirectory(prefix="cordis-preview-wide-module-") as temporary:
            directory = Path(temporary) / "观测𠮷 with spaces"
            directory.mkdir()
            executable = directory / "cmd.exe"
            shutil.copyfile(Path(os.environ["SystemRoot"]) / "System32/cmd.exe", executable)
            code = "$i=New-PreviewProcessStartInfo -Executable " + quote(executable) + " -WorkingDirectory " + quote(directory)
            code += " -Arguments @('/d','/q','/k');$i.RedirectStandardInput=$true;$p=[Diagnostics.Process]::new();$p.StartInfo=$i;"
            code += "$r=@{};try{[void]$p.Start();$r.pid=$p.Id;$o=$p.StandardOutput.ReadToEndAsync();$e=$p.StandardError.ReadToEndAsync();"
            code += "Start-Sleep -Milliseconds 100;$r.observation=Get-PreviewObservation -ProcessId $p.Id;$r.main=$p.MainModule.FileName}"
            code += "finally{$p.StandardInput.WriteLine('exit');$r.exited=$p.WaitForExit(5000);if($r.exited){$r.exit=$p.ExitCode};$p.Dispose()};$r|ConvertTo-Json -Depth 8"
            value = self.run_ps(code)
            self.assertTrue(value["exited"])
            self.assertEqual(value["exit"], 0)
            self.assertEqual(Path(value["main"]), executable)
            observation = value["observation"]
            self.assertEqual(observation["pid"], value["pid"])
            self.assertEqual(observation["errors"], [])
            self.assertFalse(observation["liveDllFilesRead"])
            rows = [row for row in observation["modules"] if Path(row["path"]).name.lower() == "cmd.exe"]
            self.assertEqual(len(rows), 1)
            self.assertEqual(Path(rows[0]["path"]), executable)
            self.assertIn("𠮷", rows[0]["path"])
            self.assertNotIn("?", rows[0]["path"])

    def test_wide_module_path_boundary_requires_room_and_exact_reported_length(self):
        cases = [(510, 512, 510, True), (511, 512, 511, False), (512, 512, 511, False),
                 (510, 512, 509, False), (0, 512, 0, False), (1, 2, 1, False),
                 (511, 1024, 511, True), (32766, 32768, 32766, True),
                 (32767, 32768, 32767, False), (32768, 32768, 32767, False)]
        rows = ",".join(f"@({length},{capacity},{actual})" for length, capacity, actual, _ in cases)
        code = "$method=[CordisPreview.Windows].GetMethod('IsCompleteModulePath',[Reflection.BindingFlags]'NonPublic,Static');"
        code += "if($null -eq $method){throw 'Actual private completeness predicate is missing'};$results=@();"
        code += "foreach($case in @(" + rows + ")){$results+=@{length=$case[0];capacity=$case[1];actual=$case[2];"
        code += "complete=$method.Invoke($null,[object[]]@([uint32]$case[0],[int]$case[1],[int]$case[2]))}};"
        code += "ConvertTo-Json -InputObject @($results) -Depth 4"
        values = self.run_ps(code)
        self.assertEqual(len(values), len(cases))
        for value, (length, capacity, actual, expected) in zip(values, cases):
            with self.subTest(length=length, capacity=capacity, actual=actual):
                self.assertEqual(value, {"length": length, "capacity": capacity, "actual": actual,
                                         "complete": expected})

    def load_recorder_functions(self, names):
        code = "$tokens=$null;$errors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile(" + quote(RECORDER)
        code += ",[ref]$tokens,[ref]$errors);if($errors.Count){throw 'Recorder AST invalid'};"
        for name in names:
            code += "$definition=$ast.Find({param($n)$n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq " + quote(name) + "},$true);"
            code += ". ([ScriptBlock]::Create($definition.Extent.Text));"
        return code

    def test_status_atomic_updates_and_recovery_keep_primary_error_on_unicode_paths(self):
        with tempfile.TemporaryDirectory(prefix="cordis-preview-status-") as temporary:
            root = Path(temporary) / "状态𠮷 with spaces"
            root.mkdir()
            status = root / "status.json"
            code = self.load_recorder_functions(["Write-DesktopJson", "Write-DesktopCompletion"])
            code += "$path=" + quote(status) + ";Write-DesktopJson $path @{value=1} -Replace;"
            code += "Write-DesktopJson $path @{value=2} -Replace;Write-DesktopJson $path @{value=3} -Replace;"
            code += "$locked=[IO.File]::Open($path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::None);"
            code += "$failed=$false;try{Write-DesktopJson $path @{value=4} -Replace}catch{$failed=$true}finally{$locked.Dispose()};"
            code += "$after=ConvertFrom-Json ([IO.File]::ReadAllText($path));$clean=-not [IO.File]::Exists($path+'.next');"
            code += "$report=[ordered]@{recording_complete=$false;liveProcess=$null;error='primary failure'};"
            code += "$completed=Write-DesktopCompletion $report " + quote(root) + ";"
            code += "@{failed=$failed;valueAfterFailure=$after.value;ownedTempCleaned=$clean;completionWritten=$completed;"
            code += "status=(ConvertFrom-Json ([IO.File]::ReadAllText($path)));report=(ConvertFrom-Json ([IO.File]::ReadAllText(" + quote(root / "recording.json") + ")))}|ConvertTo-Json -Depth 8"
            value = self.run_ps(code)
            self.assertTrue(value["failed"] and value["ownedTempCleaned"] and value["completionWritten"])
            self.assertEqual(value["valueAfterFailure"], 3)
            self.assertEqual(value["status"]["phase"], "incomplete")
            self.assertEqual(value["status"]["error"], "primary failure")
            self.assertEqual(value["report"]["error"], "primary failure")

    def test_status_preexisting_temp_is_preserved_and_secondary_failure_does_not_mask_primary(self):
        with tempfile.TemporaryDirectory(prefix="cordis-preview-status-owner-") as temporary:
            root = Path(temporary)
            (root / "status.json.next").write_bytes(b"pre-existing unrelated evidence")
            code = self.load_recorder_functions(["Write-DesktopJson", "Write-DesktopCompletion"])
            code += "$report=[ordered]@{recording_complete=$false;liveProcess=$null;error='original failure'};"
            code += "$written=Write-DesktopCompletion $report " + quote(root) + ";"
            code += "@{written=$written;report=(ConvertFrom-Json ([IO.File]::ReadAllText(" + quote(root / "recording.json") + ")))}|ConvertTo-Json -Depth 8"
            value = self.run_ps(code)
            self.assertFalse(value["written"])
            self.assertEqual(value["report"]["error"], "original failure")
            self.assertFalse(value["report"]["recording_complete"])
            self.assertTrue(value["report"]["statusWriteError"])
            self.assertEqual((root / "status.json.next").read_bytes(), b"pre-existing unrelated evidence")

    def test_finished_status_does_not_claim_completion_when_final_report_cannot_be_written(self):
        with tempfile.TemporaryDirectory(prefix="cordis-preview-final-report-") as temporary:
            root = Path(temporary) / "结束𠮷"
            root.mkdir()
            evidence = root / "recording.json"
            evidence.write_bytes(b"pre-existing unrelated evidence")
            code = self.load_recorder_functions(["Write-DesktopJson", "Write-DesktopCompletion"])
            code += "$report=[ordered]@{recording_complete=$true;liveProcess=$null;error=$null};"
            code += "$written=Write-DesktopCompletion $report " + quote(root) + ";"
            code += "@{written=$written;report=$report;status=(ConvertFrom-Json ([IO.File]::ReadAllText(" + quote(root / "status.json") + ")))}|ConvertTo-Json -Depth 8"
            value = self.run_ps(code)
            self.assertFalse(value["written"])
            self.assertFalse(value["report"]["recording_complete"])
            self.assertTrue(value["report"]["reportWriteError"])
            self.assertEqual(value["status"]["phase"], "processes-finished")
            self.assertEqual(value["status"]["completionEvidence"], "recording.json and recorder exit code 0")
            self.assertEqual(evidence.read_bytes(), b"pre-existing unrelated evidence")

    def test_actual_run_finally_disposes_owned_process_and_preserves_primary_error_on_report_failure(self):
        with tempfile.TemporaryDirectory(prefix="cordis-preview-run-finally-") as temporary:
            root = Path(temporary)
            code = self.load_recorder_functions(["Write-DesktopJson"])
            code += "$body=@($ast.FindAll({param($n)$n -is [Management.Automation.Language.TryStatementAst] -and "
            code += "$n.Body.Extent.Text.Contains('if (-not $active.Start())')},$true))[-1];"
            code += "if($null -eq $body.Finally){throw 'Actual run finalizer missing'};"
            code += "$finish=[ScriptBlock]::Create('. '+$body.Finally.Extent.Text);$results=@();"
            for index, primary in enumerate((None, "original body failure")):
                prefix = root / f"run-{index}"
                prefix.with_suffix(".json").write_bytes(b"original run evidence")
                child = base64.b64encode("[Threading.Thread]::Sleep(1500)".encode("utf-16le")).decode()
                code += "$active=[Diagnostics.Process]::new();$active.StartInfo=New-PreviewProcessStartInfo -Executable " + quote(PS)
                code += " -WorkingDirectory " + quote(root) + " -Arguments @('-NoProfile','-NonInteractive','-EncodedCommand'," + quote(child) + ");"
                code += "[void]$active.Start();$savedHandle=$active.SafeHandle;$childPid=$active.Id;"
                code += "$remaining=[Diagnostics.Process]::GetProcessById($childPid);[void]$remaining.Handle;"
                code += "$run=[ordered]@{pid=$childPid;processHandle=('0x{0:x}' -f $active.Handle.ToInt64());error=$null;logs=@()};"
                code += "$report=[ordered]@{liveProcess=$null};$readers=@();$runFinished=$false;$prefix=" + quote(prefix) + ";$caught=$null;"
                code += "try{try{"
                if primary:
                    code += "throw " + quote(primary)
                code += "}catch{$run.error=$_.Exception.Message;throw}finally{. $finish}}catch{$caught=$_.Exception.Message};"
                code += "$disposed=$savedHandle.IsClosed;"
                code += "try{$live=-not $remaining.HasExited;if(-not $remaining.WaitForExit(10000)){$remaining.Kill();throw 'Owned test child timed out'};"
                code += "$results+=@{caught=$caught;run=$run;activeCleared=($null -eq $active);disposed=$disposed;"
                code += "liveAfterFinalization=$live;exit=$remaining.ExitCode;liveEvidence=$report.liveProcess}}finally{$remaining.Dispose()};"
            code += "ConvertTo-Json -InputObject @($results) -Depth 8"
            values = self.run_ps(code)
            self.assertEqual(len(values), 2)
            for index, value in enumerate(values):
                self.assertTrue(value["disposed"] and value["activeCleared"] and value["liveAfterFinalization"])
                self.assertEqual(value["exit"], 0)
                self.assertTrue(value["liveEvidence"]["noTerminationAttempted"])
                self.assertEqual(len(value["run"]["finalizationErrors"]), 1)
                self.assertIn("run report:", value["run"]["finalizationErrors"][0])
                self.assertEqual((root / f"run-{index}.json").read_bytes(), b"original run evidence")
            self.assertTrue(values[0]["caught"].startswith("Run finalization failed: run report:"))
            self.assertEqual(values[1]["caught"], "original body failure")
            self.assertEqual(values[1]["run"]["error"], "original body failure")

    def test_declared_optional_dll_paths_are_checked_after_a_complete_core_observation(self):
        with tempfile.TemporaryDirectory(prefix="cordis-preview-path-values-") as temporary:
            root = Path(temporary)
            package = root / "package"
            package.mkdir()
            environment = ["cordis_core.dll", "cordis_qt.dll", "cordis_ui.dll", "ElaWidgetTools.dll",
                           "Qt5Core.dll", "Qt5Gui.dll", "Qt5Widgets.dll"]
            crt = ["msvcp140.dll", "msvcp140_1.dll", "vcruntime140.dll", "vcruntime140_1.dll"]
            extra = ["plugins/imageformats/qjpeg.dll", "plugins/imageformats/qgif.dll", "plugins/imageformats/qico.dll",
                     "plugins/styles/qwindowsvistastyle.dll", "libEGL.dll", "libGLESv2.dll"]
            module = "native-upgrade/counter-b/cordis_example_counter_b.dll"
            rows = [{"path": name, "role": "qualified-environment"} for name in environment]
            rows += [{"path": name, "role": "release-crt-deployment"} for name in crt]
            rows += [{"path": "plugins/platforms/qwindows.dll", "role": "qt-deployment-plugin"}]
            rows += [{"path": name, "role": "qt-deployment-plugin"} for name in extra]
            rows += [{"path": module, "role": "qualified-module"}]
            # Paths intentionally do not exist: this policy reads observation values, never DLL files.
            core = [str(package / row["path"]) for row in rows[:12]]
            allowed_store = str(root / "store" / ("a" * 64) / Path(module).name)
            cases = [{"name": "core-complete", "paths": core, "verified": True, "wrong": False},
                     {"name": "core-missing", "paths": core[:-1], "verified": False, "wrong": False},
                     {"name": "noncritical-only", "paths": [str(package / name) for name in extra],
                      "verified": False, "wrong": False},
                     {"name": "nonempty-registered-store", "paths": core + [allowed_store], "verified": True, "wrong": False},
                     {"name": "unregistered-store", "paths": core + [str(root / "other-store" / Path(module).name)],
                      "verified": False, "wrong": True},
                     {"name": "unknown-system-and-gpu", "paths": core + [str(root / "driver" / "gpu-other.dll")],
                      "verified": True, "wrong": False}]
            for name in extra:
                cases += [{"name": "package-" + name, "paths": core + [str(package / name)], "verified": True, "wrong": False},
                          {"name": "outside-" + name, "paths": core + [str(root / "external" / Path(name).name)],
                           "verified": False, "wrong": True}]
            mixed_crt = core[:7] + [str(package / name.upper()) for name in crt] + core[11:]
            external_crt = mixed_crt.copy()
            external_crt[7] = str(root / "external" / crt[0].upper())
            cases += [{"name": "mixed-case-package-crt", "paths": mixed_crt, "verified": True, "wrong": False},
                      {"name": "mixed-case-external-crt", "paths": external_crt, "verified": False, "wrong": True},
                      {"name": "unlisted-package-path", "paths": mixed_crt + [str(package / "plugins/unlisted.dll")],
                       "verified": True, "wrong": False}]
            fixture = root / "values.json"
            fixture.write_text(json.dumps({"manifest": {"files": rows}, "cases": cases}), encoding="utf-8")
            code = "$tokens=$null;$errors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile(" + quote(RECORDER)
            code += ",[ref]$tokens,[ref]$errors);if($errors.Count){throw 'Recorder AST invalid'};"
            code += "$definition=$ast.Find({param($n)$n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq 'Get-DesktopPathEvidence'},$true);"
            code += ". ([ScriptBlock]::Create($definition.Extent.Text));$PackageRoot=" + quote(package) + ";"
            code += "$values=ConvertFrom-Json -InputObject ([IO.File]::ReadAllText(" + quote(fixture) + "));"
            code += "$allowed=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase);[void]$allowed.Add(" + quote(allowed_store) + ");"
            code += "$results=@();foreach($case in $values.cases){$observation=[pscustomobject]@{modules=@($case.paths|ForEach-Object{[pscustomobject]@{path=$_}})};"
            code += "$result=Get-DesktopPathEvidence $observation $values.manifest $allowed;"
            code += "$results+=@{name=$case.name;verified=$result.verified;wrong=@($result.wrong);locations=@($observation.modules.location)}};"
            code += "ConvertTo-Json -InputObject @($results) -Depth 6"
            actual = self.run_ps(code)
            self.assertEqual(len(actual), len(cases))
            for expected, observed in zip(cases, actual):
                with self.subTest(case=expected["name"]):
                    self.assertEqual(observed["name"], expected["name"])
                    self.assertEqual(observed["verified"], expected["verified"])
                    self.assertEqual(bool(observed["wrong"]), expected["wrong"])
            self.assertIn("registered-module-store", actual[3]["locations"])
            self.assertIn("unclassified", actual[5]["locations"])
            indexed = {row["name"]: row for row in actual}
            self.assertEqual(indexed["mixed-case-package-crt"]["locations"], ["package"] * 12)
            self.assertEqual(indexed["mixed-case-external-crt"]["locations"][7], "unclassified")
            self.assertEqual(indexed["unlisted-package-path"]["locations"][-1], "unlisted-package-path")

    def test_child_argument_roundtrip_and_clean_environment_use_real_process(self):
        with tempfile.TemporaryDirectory(prefix="cordis-preview-child-") as temporary:
            path = Path(temporary)
            worker = path / "读取 args.ps1"
            worker.write_bytes(b"\xef\xbb\xbf" + (
                "param([Parameter(ValueFromRemainingArguments=$true)][string[]]$Values)\r\n"
                "[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false);"
                "@{values=@($Values);cwd=[Environment]::CurrentDirectory;path=$env:PATH;plugin=$env:QT_PLUGIN_PATH;"
                "qml=$env:QML2_IMPORT_PATH;platform=$env:QT_QPA_PLATFORM}|ConvertTo-Json -Compress").encode())
            arguments = ["中文 space", 'quote"inside', "trailing\\", "\\\\end\\"]
            code = "$env:QT_PLUGIN_PATH='untrusted-kit';$env:QML2_IMPORT_PATH='untrusted-qml';"
            code += "$i=New-PreviewProcessStartInfo -Executable " + quote(PS) + " -WorkingDirectory " + quote(path)
            code += " -Arguments @('-NoProfile','-NonInteractive','-File'," + quote(worker) + "," + ",".join(quote(a) for a in arguments) + ");"
            code += "$p=[Diagnostics.Process]::new();$p.StartInfo=$i;[void]$p.Start();$o=$p.StandardOutput.ReadToEndAsync();$e=$p.StandardError.ReadToEndAsync();"
            code += "if(-not $p.WaitForExit(10000)){$p.Kill();throw 'Owned test child timed out'};if($p.ExitCode){throw $e.Result};Write-Output $o.Result;$p.Dispose()"
            value = self.run_ps(code)
            self.assertEqual(value["values"], arguments)
            self.assertEqual(Path(value["cwd"]), path)
            self.assertIsNone(value["plugin"])
            self.assertIsNone(value["qml"])
            self.assertEqual(value["platform"], "windows")
            self.assertNotIn("Qt", value["path"])

    def test_bounded_capture_drains_an_actual_non_gui_child(self):
        with tempfile.TemporaryDirectory(prefix="cordis-preview-log-") as temporary:
            path = Path(temporary)
            script = path / "emit.ps1"
            script.write_text("$s=[Console]::OpenStandardOutput();$b=[byte[]]::new(65536);for($n=0;$n -lt 80;$n++){$s.Write($b,0,$b.Length)}", encoding="utf-8")
            output = path / "bounded.log"
            code = "$i=New-PreviewProcessStartInfo -Executable " + quote(PS) + " -WorkingDirectory " + quote(path)
            code += " -Arguments @('-NoProfile','-NonInteractive','-File'," + quote(script) + ");$p=[Diagnostics.Process]::new();$p.StartInfo=$i;[void]$p.Start();"
            code += "$r=New-PreviewLogReader -InputStream $p.StandardOutput.BaseStream -Path " + quote(output) + ";$clock=[Diagnostics.Stopwatch]::StartNew();"
            code += "try{while(-not $r.eof -and $clock.Elapsed.TotalSeconds -lt 15){Receive-PreviewLog $r;Start-Sleep -Milliseconds 5};"
            code += "if(-not $r.eof){$p.Kill();throw 'Owned log test child timed out'};$p.WaitForExit();"
            code += "@{seen=$r.seen;saved=$r.saved;truncated=$r.truncated;eof=$r.eof;exit=$p.ExitCode}|ConvertTo-Json}finally{$r.File.Dispose();$p.Dispose()}"
            value = self.run_ps(code)
            self.assertEqual(value, {"seen": 5 * 1024 * 1024, "saved": 4 * 1024 * 1024,
                                     "truncated": True, "eof": True, "exit": 0})
            self.assertEqual(output.stat().st_size, 4 * 1024 * 1024)

    def test_tree_hash_and_reparse_rejection_only_touch_temporary_files(self):
        with tempfile.TemporaryDirectory(prefix="cordis-preview-tree-") as temporary:
            path = Path(temporary)
            tree = path / "tree"
            tree.mkdir()
            (tree / "file.txt").write_bytes(b"one")
            value = self.run_ps("Get-PreviewTreeSnapshot -Root " + quote(tree) + "|ConvertTo-Json -Depth 5")
            self.assertEqual(value["file.txt"]["sha256"], hashlib.sha256(b"one").hexdigest())
            target = path / "external"
            target.mkdir()
            junction = tree / "junction"
            code = "New-Item -ItemType Junction -Path " + quote(junction) + " -Target " + quote(target) + "|Out-Null;"
            code += "$rejected=$false;try{[void](Get-PreviewTreeSnapshot -Root " + quote(tree) + ")}catch{$rejected=$true};@{rejected=$rejected}|ConvertTo-Json"
            try:
                self.assertTrue(self.run_ps(code)["rejected"])
            finally:
                if junction.exists():
                    os.rmdir(junction)

    def test_error_mode_is_restored_and_ui_rejects_non_product_pid(self):
        value = self.run_ps("$old=Set-PreviewErrorMode -Mode 32771;try{$actual=Set-PreviewErrorMode -Mode $old}finally{[void](Set-PreviewErrorMode -Mode $old)};"
                            "Import-Module " + quote(UI) + " -Force;@{mode=$actual;parent=$PID;ui=(Get-PreviewUiSnapshot -ProcessId $PID -Entry business)}|ConvertTo-Json -Depth 8")
        self.assertEqual(value["mode"], 32771)
        self.assertEqual(value["ui"]["pid"], value["parent"])
        self.assertTrue(value["ui"]["readOnly"])
        self.assertEqual(value["ui"]["elements"], [])
        self.assertTrue(value["ui"]["errors"])
        self.assertNotEqual(value["ui"]["observerPid"], value["parent"])

    def test_recorder_preflight_does_not_launch_or_overwrite_for_invalid_roots_or_anchor(self):
        with tempfile.TemporaryDirectory(prefix="cordis-preview-preflight-") as temporary:
            path = Path(temporary)
            package = path / "package"
            package.mkdir()
            manifest = package / "build-manifest.json"
            manifest.write_bytes(b"{}")
            existing = path / "existing"
            existing.mkdir()
            sentinel = existing / "original"
            sentinel.write_bytes(b"preserve")
            for data, anchor, cwd in ((existing, "0" * 64, None),
                                      (package / "data", "0" * 64, None),
                                      (path / "new", "0" * 64, None),
                                      (path / "bad-cwd", hashlib.sha256(b"{}").hexdigest(), package)):
                with self.subTest(data=data, cwd=cwd):
                    args = [str(PS), "-NoProfile", "-NonInteractive", "-File", str(RECORDER),
                            "-PackageRoot", str(package), "-DataRoot", str(data), "-ExpectedManifestSha256", anchor]
                    if cwd is not None:
                        args += ["-WorkingDirectory", str(cwd)]
                    result = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=20, check=False)
                    self.assertNotEqual(result.returncode, 0)
                    if (data / "recording.json").is_file():
                        self.assertEqual(json.loads((data / "recording.json").read_text(encoding="utf-8"))["runs"], [])
            self.assertEqual(sentinel.read_bytes(), b"preserve")


if __name__ == "__main__":
    unittest.main()
