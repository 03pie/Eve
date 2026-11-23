"""Real PS5.1 RawSecurityDescriptor comparisons; no filesystem ACL changes."""
import base64
import json
import os
from pathlib import Path
import subprocess
import unittest


ROOT = Path(__file__).resolve().parents[2]
ACL = ROOT / 'delivery/windows/verification/preview-acl.ps1'
PS = Path(os.environ.get('SystemRoot', 'C:/Windows')) / 'System32/WindowsPowerShell/v1.0/powershell.exe'


@unittest.skipUnless(os.name == 'nt' and PS.is_file(), 'Stock Windows PowerShell 5.1 is required')
class PreviewAclComparisonTests(unittest.TestCase):
    def test_only_auto_inherited_control_state_is_normalized(self):
        script = r'''
[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)
$ErrorActionPreference='Stop'
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile('__ACL__',[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'ACL source parser failed'}
$node=$ast.Find({param($n)$n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq 'Compare-PreviewRestoredAcl'},$true)
if($null -eq $node){throw 'Missing actual comparison function'}
. ([ScriptBlock]::Create($node.Extent.Text))
$base=[Security.AccessControl.RawSecurityDescriptor]::new('O:SYG:BAD:(A;;FR;;;SY)(A;;FW;;;BA)')
function With-Flags($Value,[int]$Xor){
    $copy=[Security.AccessControl.RawSecurityDescriptor]::new($Value.GetSddlForm([Security.AccessControl.AccessControlSections]::All))
    $copy.SetFlags([Security.AccessControl.ControlFlags]([int]$Value.ControlFlags -bxor $Xor))
    return $copy
}
$cases=[Collections.Generic.List[object]]::new()
function Check([string]$Name,$Expected,$Actual,[bool]$Passed,[bool]$Normalized=$false,[string]$Difference=''){
    $cases.Add(@{name=$Name;expectedPassed=$Passed;expectedNormalized=$Normalized;expectedDifference=$Difference;actual=(Compare-PreviewRestoredAcl $Expected $Actual)})
}
Check 'same' $base $base $true
$ai=With-Flags $base 0x0400
Check 'only-AI' $base $ai $true $true
Check 'only-AI-reverse' $ai $base $true $true
Check 'owner' $base ([Security.AccessControl.RawSecurityDescriptor]::new('O:BAG:BAD:(A;;FR;;;SY)(A;;FW;;;BA)')) $false $false 'owner'
Check 'group' $base ([Security.AccessControl.RawSecurityDescriptor]::new('O:SYG:BUD:(A;;FR;;;SY)(A;;FW;;;BA)')) $false $false 'group'
foreach($bit in @(0x0001,0x0002,0x0004,0x0008,0x0010,0x0020,0x0040,0x0080,0x0100,0x0200,0x0800,0x1000,0x2000,0x4000)){
    Check ('other-flag-'+$bit) $base (With-Flags $base $bit) $false $false 'controlFlags'
}
$selfRelative=With-Flags $base 0x8000
if($selfRelative.ControlFlags -ne $base.ControlFlags){throw 'Unexpected RawSecurityDescriptor SelfRelative invariant'}
Check 'runtime-SelfRelative-required' $base $selfRelative $true
Check 'AI-and-protected' $base (With-Flags $base 0x1400) $false $false 'controlFlags'
foreach($change in @(
    @('order','O:SYG:BAD:(A;;FW;;;BA)(A;;FR;;;SY)'),
    @('ace-count','O:SYG:BAD:(A;;FR;;;SY)'),
    @('ace-flags','O:SYG:BAD:(A;CI;FR;;;SY)(A;;FW;;;BA)'),
    @('ace-mask','O:SYG:BAD:(A;;FW;;;SY)(A;;FW;;;BA)'),
    @('ace-sid','O:SYG:BAD:(A;;FR;;;BU)(A;;FW;;;BA)'),
    @('ace-type','O:SYG:BAD:(D;;FR;;;SY)(A;;FW;;;BA)'))){
    Check $change[0] $base ([Security.AccessControl.RawSecurityDescriptor]::new($change[1])) $false $false 'orderedDaclBinary'
}
$wrongAce=[Security.AccessControl.RawSecurityDescriptor]::new('O:SYG:BAD:(A;;FW;;;SY)(A;;FW;;;BA)')
Check 'AI-and-wrong-ACE' $base (With-Flags $wrongAce 0x0400) $false $false 'orderedDaclBinary'
$objectA=[Security.AccessControl.RawSecurityDescriptor]::new('O:SYG:BAD:(OA;;GR;11111111-1111-1111-1111-111111111111;;SY)')
$objectB=[Security.AccessControl.RawSecurityDescriptor]::new('O:SYG:BAD:(OA;;GR;22222222-2222-2222-2222-222222222222;;SY)')
Check 'object-guid' $objectA $objectB $false $false 'orderedDaclBinary'
$revision4=[Security.AccessControl.RawAcl]::new(4,2)
for($i=0;$i -lt $base.DiscretionaryAcl.Count;$i++){$revision4.InsertAce($i,$base.DiscretionaryAcl[$i])}
$differentRevision=[Security.AccessControl.RawSecurityDescriptor]::new($base.ControlFlags,$base.Owner,$base.Group,$null,$revision4)
Check 'acl-revision' $base $differentRevision $false $false 'orderedDaclBinary'
$empty=[Security.AccessControl.RawSecurityDescriptor]::new($base.ControlFlags,$base.Owner,$base.Group,$null,[Security.AccessControl.RawAcl]::new(2,0))
$nullAcl=[Security.AccessControl.RawSecurityDescriptor]::new($base.ControlFlags,$base.Owner,$base.Group,$null,$null)
Check 'empty-same' $empty $empty $true
Check 'null-same' $nullAcl $nullAcl $true
Check 'null-empty' $nullAcl $empty $false $false 'daclNullness'
Check 'empty-null' $empty $nullAcl $false $false 'daclNullness'
$cases.ToArray()|ConvertTo-Json -Depth 8
'''.replace('__ACL__', str(ACL).replace("'", "''"))
        result = subprocess.run([str(PS), '-NoProfile', '-NonInteractive', '-EncodedCommand',
                                 base64.b64encode(script.encode('utf-16le')).decode()],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30, check=False)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors='replace'))
        cases = json.loads(result.stdout.decode('utf-8-sig'))
        self.assertEqual(len(cases), 34)
        for case in cases:
            with self.subTest(case=case['name']):
                actual = case['actual']
                self.assertEqual(actual['passed'], case['expectedPassed'])
                self.assertEqual(actual['autoInheritedNormalized'], case['expectedNormalized'])
                if case['expectedDifference']:
                    self.assertIn(case['expectedDifference'], actual['differences'])
                if actual['passed']:
                    self.assertEqual(actual['differences'], [])


if __name__ == '__main__':
    unittest.main()
