"""Exercise the QA owner's real file gate and owned pipes without Cordis or a UI."""
import base64
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
OWNER = ROOT / 'tools/record-readonly-save.ps1'
WINDOWS = ROOT / 'delivery/windows/verification/preview-windows.psm1'


@unittest.skipUnless(os.name == 'nt', 'Windows PowerShell and file ownership are required')
class ReadonlySaveOwner(unittest.TestCase):
    def run_script(self, body):
        script = r'''
$ErrorActionPreference='Stop';Set-StrictMode -Version 2.0
[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)
function Parse([string]$Path){
    $tokens=$null;$errors=$null
    $result=[Management.Automation.Language.Parser]::ParseFile($Path,[ref]$tokens,[ref]$errors)
    if($errors.Count){throw 'Production source did not parse.'};return $result
}
function Definition($Ast,[string]$Name){
    $nodes=@($Ast.FindAll({param($node)$node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq $Name},$true))
    if($nodes.Count -ne 1){throw 'Expected one actual production definition.'};return $nodes[0].Extent.Text
}
$owner=Parse '__OWNER__';$windows=Parse '__WINDOWS__'
'''.replace('__OWNER__', str(OWNER).replace("'", "''")).replace(
            '__WINDOWS__', str(WINDOWS).replace("'", "''")) + body
        encoded = base64.b64encode(script.encode('utf-16le')).decode('ascii')
        powershell = Path(os.environ.get('SystemRoot', r'C:\Windows')) / 'System32/WindowsPowerShell/v1.0/powershell.exe'
        result = subprocess.run([str(powershell), '-NoProfile', '-NonInteractive', '-EncodedCommand', encoded],
                                capture_output=True, timeout=90, check=False)
        self.assertEqual(result.returncode, 0, result.stderr.decode('utf-8-sig', errors='replace'))
        facts = json.loads(result.stdout.decode('utf-8-sig'))
        print(json.dumps({'scope': 'host-only-readonly-owner-regression', 'facts': facts}, sort_keys=True))
        return facts

    def test_manifest_set_accepts_both_real_inventory_sizes_and_rejects_drift(self):
        with tempfile.TemporaryDirectory(prefix='cordis-owner-files-') as directory:
            facts = self.run_script(r'''
foreach($name in @('Assert-D10Relative','Assert-D10SourceSnapshot')){. ([scriptblock]::Create((Definition $owner $name)))}
foreach($name in @('Resolve-PreviewDirectory','Get-PreviewFileSnapshot','Get-PreviewTreeSnapshot')){. ([scriptblock]::Create((Definition $windows $name)))}
$passed=@();$rejected=0
foreach($count in @(136,137)){
    $root=Join-Path '__ROOT__' ('中文 库存-'+$count);[void][IO.Directory]::CreateDirectory($root)
    $entries=@()
    for($index=0;$index -lt $count;$index++){
        $name=if($index -eq 0){'verification/preview-acl.ps1'}elseif($index -eq 1){'verification/verify-package.ps1'}else{'payload/{0:d3}.bin' -f $index}
        $path=Join-Path $root $name.Replace('/','\');[void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path))
        [IO.File]::WriteAllText($path,('Controlled file fixture '+$index),[Text.UTF8Encoding]::new($false))
        $entries+=,[pscustomobject]@{path=$name;bytes=(Get-Item -LiteralPath $path).Length;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()}
    }
    $manifest=[pscustomobject]@{schema='cordis.preview.package/1';files=$entries}
    $manifestPath=Join-Path $root 'build-manifest.json'
    [IO.File]::WriteAllText($manifestPath,($manifest|ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
    $manifestAnchor=(Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
    $sums=@($entries|ForEach-Object{$_.sha256+'  '+$_.path})+@($manifestAnchor+'  build-manifest.json')
    [IO.File]::WriteAllText((Join-Path $root 'SHA256SUMS.txt'),($sums -join "`n"),[Text.UTF8Encoding]::new($false))
    $snapshot=Get-PreviewTreeSnapshot $root
    Assert-D10SourceSnapshot $snapshot $manifest
    if($snapshot.Count -ne $count+2 -or (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLowerInvariant() -cne $manifestAnchor){throw 'Actual inventory/anchor differs.'}
    $passed+=,$snapshot.Count
    foreach($mutation in @('producer-sha','verifier-length','duplicate-case','traversal','wrong-schema','empty-files','extra-file','missing-checksum')){
        $bad=$manifest|ConvertTo-Json -Depth 8|ConvertFrom-Json;$actual=$snapshot
        switch($mutation){
            'producer-sha' {$path=Join-Path $root $entries[0].path.Replace('/','\');[IO.File]::WriteAllText($path,('X'*(Get-Item -LiteralPath $path).Length));$actual=Get-PreviewTreeSnapshot $root}
            'verifier-length' {$path=Join-Path $root $entries[1].path.Replace('/','\');[IO.File]::AppendAllText($path,'!');$actual=Get-PreviewTreeSnapshot $root}
            'duplicate-case' {$bad.files[1].path=$bad.files[0].path.ToUpperInvariant()}
            'traversal' {$bad.files[0].path='../escape.ps1'}
            'wrong-schema' {$bad.schema='not-a-package'}
            'empty-files' {$bad.files=@()}
            'extra-file' {[IO.File]::WriteAllText((Join-Path $root 'extra.bin'),'extra');$actual=Get-PreviewTreeSnapshot $root}
            'missing-checksum' {[IO.File]::Delete((Join-Path $root 'SHA256SUMS.txt'));$actual=Get-PreviewTreeSnapshot $root}
        }
        $denied=$false;try{Assert-D10SourceSnapshot $actual $bad}catch{$denied=$true}
        if(-not $denied){throw ('Manifest mutation accepted: '+$mutation)};$rejected++
        if($mutation -in @('producer-sha','verifier-length')){$index=if($mutation -ceq 'producer-sha'){0}else{1};[IO.File]::WriteAllText((Join-Path $root $entries[$index].path.Replace('/','\')),('Controlled file fixture '+$index),[Text.UTF8Encoding]::new($false))}
        if($mutation -ceq 'extra-file'){[IO.File]::Delete((Join-Path $root 'extra.bin'))}
        if($mutation -ceq 'missing-checksum'){[IO.File]::WriteAllText((Join-Path $root 'SHA256SUMS.txt'),($sums -join "`n"),[Text.UTF8Encoding]::new($false))}
    }
}
@{actualFileCounts=$passed;mutationsRejected=$rejected;productOrUiStarted=$false}|ConvertTo-Json -Compress
'''.replace('__ROOT__', directory.replace("'", "''")))
        self.assertEqual(facts['actualFileCounts'], [138, 139])
        self.assertEqual(facts['mutationsRejected'], 16)
        self.assertFalse(facts['productOrUiStarted'])

    def test_second_reader_real_create_new_failure_drains_owned_child_and_preserves_error(self):
        with tempfile.TemporaryDirectory(prefix='cordis-owner-pipes-') as directory:
            facts = self.run_script(r'''
foreach($name in @('Assert-D10Path','Get-D10Sha','Write-D10Json','Drain-D10Children','Test-D10Live','Start-D10Child','Finish-D10Child')){. ([scriptblock]::Create((Definition $owner $name)))}
foreach($name in @('Resolve-PreviewDirectory','Get-PreviewFileSnapshot','New-PreviewProcessStartInfo','New-PreviewLogReader','Receive-PreviewLog')){. ([scriptblock]::Create((Definition $windows $name)))}
$script:cwd='__ROOT__';$script:evidence='__ROOT__'
$script:children=[Collections.Generic.List[object]]::new();$script:writer=$null;$script:product=$null;$script:driver=$null
$script:created=$true;$script:responsible=$false;$script:restoreAttempted=$false
$occupied=Join-Path $script:evidence 'fixture.stderr.log';[IO.File]::WriteAllText($occupied,'Existing file must remain untouched.')
$oldHash=Get-D10Sha $occupied
# This owned fixture has no UI, ACL, Cordis, credentials or external process.
# A self-contained write timeout also lets a broken drain fail naturally.
$program='$data=[byte[]]::new(1048576);$stream=[Console]::OpenStandardError();$write=$stream.WriteAsync($data,0,$data.Length);if(-not $write.Wait(15000)){exit 23};[Console]::Write("fixture-finished");exit 0'
$encoded=[Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($program));$caught=$null
try{[void](Start-D10Child 'fixture' (Join-Path $env:WINDIR 'System32/WindowsPowerShell/v1.0/powershell.exe') @('-NoProfile','-NonInteractive','-EncodedCommand',$encoded))}
catch{$caught=$_.Exception.Message}
if(-not $caught -or $script:children.Count -ne 1){throw 'Expected real second-reader CreateNew failure.'}
$child=$script:children[0];$heldHandle=$child.process.SafeHandle
if(-not $child.readerOwned.stdout -or $child.readerOwned.stderr -or $child.discardTasks.Count -ne 1 -or $child.record.logError -cne $caught){throw 'Partial reader ownership/first error was not retained.'}
# Execute the real owner's finalization block. A pre-existing optimistic flag
# must be cleared by actual incomplete capture, even if the child exits0.
$report=[ordered]@{recording_complete=$true;error=$caught;productExitCode=$null;uiExitCode=$null;liveProduct=$null;restoreResponsibilityUnresolved=$false}
$tries=@($owner.EndBlock.Statements|Where-Object{$_ -is [Management.Automation.Language.TryStatementAst]})
if($tries.Count -ne 1){throw 'Expected one actual owner lifecycle.'}
$cleanup=$tries[0].Finally.Extent.Text
. ([scriptblock]::Create($cleanup.Substring(1,$cleanup.Length-2)))
$saved=[IO.File]::ReadAllText((Join-Path $script:evidence 'result.json'))|ConvertFrom-Json
if($saved.recording_complete -or $saved.error -cne $caught -or -not $child.finished -or $child.record.exitCode -ne 0 -or -not $child.record.logsIncomplete){throw 'Actual exit0 masked original incomplete/error evidence.'}
if($child.record.stderrSha256 -ne $null -or $child.record.discardFallback[0] -cne 'stderr' -or -not $child.discardTasks[0].task.IsCompleted -or $child.record.drainErrors.Count){throw 'Discard was mistaken for captured output or did not close.'}
if((Get-D10Sha $occupied) -cne $oldHash -or [IO.File]::ReadAllText($child.record.stdoutPath) -cne 'fixture-finished' -or -not $child.record.logs[0].eof){throw 'Actual original file/stdout/EOF differs.'}
$file=[IO.File]::Open($child.record.stdoutPath,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None);$file.Dispose()
$disposed=$heldHandle.IsClosed
if(-not $disposed){throw 'Owned process handle was not released.'}
@{actualChildPid=$child.record.pid;actualChildExitCode=$child.record.exitCode;naturalExitObserved=$true;discardStreams=$child.record.discardFallback;
    captureIncomplete=$saved.recording_complete -eq $false;firstErrorPreserved=$saved.error -ceq $caught;firstError=$caught;heldProcessDisposed=$disposed;
    processHandleObserved=$child.record.processHandle -match '^0x[0-9a-f]+$';creationFileTimeObserved=$child.record.creationFileTime -match '^[1-9][0-9]+$';
    ownedLogExclusiveReopen=$true;preexistingFileUnchanged=$true;productOrUiStarted=$false;forceKillPerformed=$false}|ConvertTo-Json -Compress
'''.replace('__ROOT__', directory.replace("'", "''")))
        self.assertGreater(facts['actualChildPid'], 0)
        self.assertEqual(facts['actualChildExitCode'], 0)
        self.assertEqual(facts['discardStreams'], ['stderr'])
        for field in ('naturalExitObserved', 'captureIncomplete', 'firstErrorPreserved',
                      'heldProcessDisposed', 'ownedLogExclusiveReopen', 'preexistingFileUnchanged',
                      'processHandleObserved', 'creationFileTimeObserved'):
            self.assertTrue(facts[field], field)
        self.assertFalse(facts['productOrUiStarted'])
        self.assertFalse(facts['forceKillPerformed'])


if __name__ == '__main__':
    unittest.main()
