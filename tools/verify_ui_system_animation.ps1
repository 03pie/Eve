param(
    [Parameter(Mandatory)][string]$TestExecutable,
    [Parameter(Mandatory)][string]$QtRoot,
    [string]$EvidenceFile
)
$ErrorActionPreference = 'Stop'
if (-not ('EveVerification.ClientAnimation' -as [type])) {
    Add-Type @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
namespace EveVerification {
    public static class ClientAnimation {
        [DllImport("user32.dll", EntryPoint="SystemParametersInfoW", SetLastError=true)]
        private static extern bool Read(uint action, uint parameter, out int result, uint flags);
        [DllImport("user32.dll", EntryPoint="SystemParametersInfoW", SetLastError=true)]
        private static extern bool Write(uint action, uint parameter, IntPtr value, uint flags);
        public static bool Get() {
            int value;
            if (!Read(0x1042, 0, out value, 0)) throw new Win32Exception();
            return value != 0;
        }
        public static void Set(bool value) {
            if (!Write(0x1043, 0, new IntPtr(value ? 1 : 0), 0)) throw new Win32Exception();
        }
    }
}
'@
}
$exe = (Resolve-Path -LiteralPath $TestExecutable).Path
$qt = (Resolve-Path -LiteralPath $QtRoot).Path
$before = [EveVerification.ClientAnimation]::Get()
$report = [ordered]@{
    systemBefore = $before
    systemAfter = $null
    platform = 'qwindows'
    os = [Environment]::OSVersion.VersionString
    executableSHA256 = (Get-FileHash -LiteralPath $exe).Hash
    status = 'failed'
}
$oldPath = $env:PATH
$oldPlugins = $env:QT_PLUGIN_PATH
$oldPlatform = $env:QT_QPA_PLATFORM
try {
    if ($before) {
        $env:PATH = "$qt/bin;$oldPath"
        $env:QT_PLUGIN_PATH = "$qt/plugins"
        $env:QT_QPA_PLATFORM = 'windows'
        $output = & $exe settings_system_animation
        if ($LASTEXITCODE -ne 0) { throw "Native animation test exited with $LASTEXITCODE." }
        $report.native = ($output | Out-String | ConvertFrom-Json)
        $report.status = 'passed'
    } else {
        $report.status = 'skipped: system animations already disabled'
    }
} finally {
    $env:PATH = $oldPath
    $env:QT_PLUGIN_PATH = $oldPlugins
    $env:QT_QPA_PLATFORM = $oldPlatform
    $actual = [EveVerification.ClientAnimation]::Get()
    if ($before -and !$actual) { [EveVerification.ClientAnimation]::Set($before) }
    $report.systemAfter = [EveVerification.ClientAnimation]::Get()
    if ($report.systemAfter -ne $before) { throw 'System animation setting differs from its original state.' }
    $json = $report | ConvertTo-Json -Depth 5
    if ($EvidenceFile) { $json | Set-Content -Encoding utf8 -LiteralPath $EvidenceFile }
    $json
}
