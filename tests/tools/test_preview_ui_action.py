"""Exercise the real native identity gate without a desktop or a Cordis process."""
import base64
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
ACTION = ROOT / 'tools/preview_ui_action.ps1'


@unittest.skipUnless(os.name == 'nt', 'Windows process identity is required')
class PreviewUiActionIdentity(unittest.TestCase):
    def test_actual_creation_owner_session_path_and_held_exit_guard(self):
        source = str(ACTION).replace("'", "''")
        script = r'''
$ErrorActionPreference='Stop'
[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile('__SOURCE__',[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'Action helper did not parse.'}
foreach($name in @('Get-PreviewBoundProcess','Assert-PreviewBoundAlive')){
    $definitions=@($ast.FindAll({param($node)$node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq $name},$true))
    if($definitions.Count -ne 1){throw 'Expected exactly one production function.'}
    . ([scriptblock]::Create($definitions[0].Extent.Text))
}
function Actual-Descriptor($Process){
    $row=Get-CimInstance Win32_Process -Filter ('ProcessId = '+$Process.Id)
    $owner=Invoke-CimMethod -InputObject $row -MethodName GetOwnerSid
    if($owner.ReturnValue -ne 0){throw 'Fixture process owner is unavailable.'}
    [pscustomobject]@{pid=[int]$Process.Id;creationFileTime=$Process.StartTime.ToUniversalTime().ToFileTimeUtc().ToString([Globalization.CultureInfo]::InvariantCulture);path=[string]$row.ExecutablePath;sessionId=[int]$Process.SessionId;ownerSid=[string]$owner.Sid;parentPid=[int]$row.ParentProcessId}
}
$self=[Diagnostics.Process]::GetCurrentProcess()
$descriptor=Actual-Descriptor $self
$differentSid=if($descriptor.ownerSid -ceq 'S-1-5-18'){'S-1-5-19'}else{'S-1-5-18'}
$bound=Get-PreviewBoundProcess $descriptor
try{Assert-PreviewBoundAlive @($bound);if($bound.facts.pid -ne $PID){throw 'Actual PID was not observed.'}}finally{$bound.process.Dispose()}
$cases=@(
    @{field='pid';value=$true},@{field='pid';value=1.25},@{field='pid';value=0},
    @{field='creationFileTime';value=([long]$descriptor.creationFileTime+1).ToString()},
    @{field='creationFileTime';value=[long]$descriptor.creationFileTime},
    @{field='path';value=(Join-Path $env:WINDIR 'not-the-running-process.exe')},
    @{field='path';value='relative.exe'},
    @{field='path';value=((Split-Path -Parent $descriptor.path)+'\.\'+[IO.Path]::GetFileName($descriptor.path))},
    @{field='path';value=($descriptor.path.Substring(0,2)+[IO.Path]::GetFileName($descriptor.path))},
    @{field='ownerSid';value=$differentSid},
    @{field='sessionId';value=([int]$descriptor.sessionId+1)},
    @{field='sessionId';value=1.5},@{field='parentPid';value=([int]$descriptor.parentPid+4)},
    @{field='parentPid';value=-1}
)
$rejected=0
foreach($case in $cases){
    $bad=$descriptor|ConvertTo-Json|ConvertFrom-Json
    $bad.($case.field)=$case.value
    $unexpected=$null;$didReject=$false
    try{$unexpected=Get-PreviewBoundProcess $bad}catch{$didReject=$true}
    finally{if($null -ne $unexpected){$unexpected.process.Dispose()}}
    if(-not $didReject){throw ('Wrong actual identity was accepted: '+$case.field)}
    $rejected++
}
# A harmless owned PowerShell fixture waits on its redirected stdin, then exits
# naturally. No Cordis application or UI surface is opened or terminated.
$fixture=[Diagnostics.Process]::new();$fixtureBinding=$null;$fixtureStarted=$false
try{
    $start=[Diagnostics.ProcessStartInfo]::new()
    $start.FileName=Join-Path $env:WINDIR 'System32/WindowsPowerShell/v1.0/powershell.exe'
    $start.Arguments='-NoProfile -NonInteractive -Command "$null = [Console]::ReadLine()"'
    $start.UseShellExecute=$false;$start.CreateNoWindow=$true;$start.RedirectStandardInput=$true
    $fixture.StartInfo=$start
    if(-not $fixture.Start()){throw 'Harmless fixture did not start.'}
    $fixtureStarted=$true
    $fixtureBinding=Get-PreviewBoundProcess (Actual-Descriptor $fixture)
    Assert-PreviewBoundAlive @($fixtureBinding)
    $fixture.StandardInput.WriteLine('')
    if(-not $fixture.WaitForExit(10000) -or $fixture.ExitCode -ne 0){throw 'Fixture did not naturally exit0.'}
    $exitRejected=$false
    try{Assert-PreviewBoundAlive @($fixtureBinding)}catch{$exitRejected=$true}
    if(-not $exitRejected){throw 'Exited pinned process was accepted as live.'}
}finally{
    if($null -ne $fixtureBinding){$fixtureBinding.process.Dispose()}
    if($fixtureStarted -and -not $fixture.HasExited){$fixture.StandardInput.WriteLine('');[void]$fixture.WaitForExit(10000)}
    $fixture.Dispose();$self.Dispose()
}
@{actualIdentityVerified=$true;wrongDescriptorsRejected=$rejected;naturalExitRejected=$true;uiPerformed=$false;cordisProcessesStarted=0}|ConvertTo-Json -Compress
'''.replace('__SOURCE__', source)
        encoded = base64.b64encode(script.encode('utf-16le')).decode('ascii')
        powershell = Path(os.environ.get('SystemRoot', r'C:\Windows')) / 'System32/WindowsPowerShell/v1.0/powershell.exe'
        result = subprocess.run([str(powershell), '-NoProfile', '-NonInteractive', '-EncodedCommand', encoded],
                                capture_output=True, timeout=60, check=False)
        self.assertEqual(result.returncode, 0, result.stderr.decode('utf-8-sig', errors='replace'))
        facts = json.loads(result.stdout.decode('utf-8-sig'))
        self.assertTrue(facts['actualIdentityVerified'])
        self.assertEqual(facts['wrongDescriptorsRejected'], 14)
        self.assertTrue(facts['naturalExitRejected'])
        self.assertFalse(facts['uiPerformed'])
        self.assertEqual(facts['cordisProcessesStarted'], 0)

    def test_anchored_request_pid_is_checked_before_any_process_or_ui_lookup(self):
        powershell = Path(os.environ.get('SystemRoot', r'C:\Windows')) / 'System32/WindowsPowerShell/v1.0/powershell.exe'
        with tempfile.TemporaryDirectory(prefix='cordis-ui-identity-') as directory:
            root = Path(directory).resolve()
            for index, pid in enumerate((str(os.getpid()), True, float(os.getpid()) + .25, 2**31)):
                request, output = root / f'{index}-request.json', root / f'{index}-output.json'
                request.write_text(json.dumps({'pid': pid, 'entry': 'configuration', 'action': 'Read',
                                               'expectedIdentity': {}}), encoding='utf-8')
                result = subprocess.run([str(powershell), '-NoProfile', '-NonInteractive', '-File', str(ACTION),
                                         '-Request', str(request), '-Output', str(output)], capture_output=True, timeout=20)
                self.assertEqual(result.returncode, 1)
                raw = json.loads(output.read_text(encoding='utf-8-sig'))
                self.assertFalse(raw['ok'])
                self.assertEqual(raw['error'], 'Anchored request PID must be a positive integer.')
                self.assertNotIn('actualIdentity', raw)


if __name__ == '__main__':
    unittest.main()
