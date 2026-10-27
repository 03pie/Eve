<#
.SYNOPSIS
在普通 PowerShell 中加载本地 MSVC/Qt 环境并运行桌面构建或工作台。
.DESCRIPTION
自动发现 VS2022 x64 工具链，默认使用 build/Qt 中的 Qt 5.15.2。
环境变量只在本次调用中生效，退出时恢复；依赖安装独立进行。
.PARAMETER Action
doctor、configure、build、test、verify 或 run；run 启动配置工作台。
.PARAMETER Preset
Qt/Ela 的 MSVC Debug 或 Release 预设。
.PARAMETER QtRoot
Qt kit 路径，优先采用显式参数及 CORDIS_QT5152_ROOT。
.PARAMETER ConfigurationFile
run 时传给工作台的可选配置文件，适合使用独立验收文件。
.PARAMETER DeveloperChild
内部执行标记；公开入口始终在独立 PowerShell 进程中加载开发环境。
#>
[CmdletBinding()]
param(
    [ValidateSet('doctor', 'configure', 'build', 'test', 'verify', 'run')]
    [string]$Action = 'verify',
    [ValidateSet('qt-msvc-debug', 'qt-msvc-release', 'ela-msvc-debug', 'ela-msvc-release')]
    [string]$Preset = 'ela-msvc-debug',
    [string]$QtRoot = $env:CORDIS_QT5152_ROOT,
    [string]$ConfigurationFile,
    [switch]$DeveloperChild
)

$ErrorActionPreference = 'Stop'
if (-not $DeveloperChild) {
    # VS 开发环境加载器也会重写空环境变量；用子进程隔离其全部环境及模块副作用。
    $taskShellName = if ($PSVersionTable.PSEdition -eq 'Core') { 'pwsh.exe' } else { 'powershell.exe' }
    $taskChildArguments = @('-NoProfile', '-File', $PSCommandPath, '-DeveloperChild', '-Action', $Action, '-Preset', $Preset)
    if ($QtRoot) { $taskChildArguments += @('-QtRoot', $QtRoot) }
    if ($ConfigurationFile) { $taskChildArguments += @('-ConfigurationFile', $ConfigurationFile) }
    $ErrorActionPreference = 'Continue'
    & (Join-Path $PSHOME $taskShellName) @taskChildArguments
    $taskChildExitCode = $LASTEXITCODE
    if ($taskChildExitCode -ne 0) { throw "Desktop command failed with exit code $taskChildExitCode." }
    return
}
$taskRoot = Split-Path -Parent $PSScriptRoot
Push-Location -LiteralPath $taskRoot
try {
    if (-not $QtRoot) { $QtRoot = Join-Path $taskRoot 'build/Qt/5.15.2/msvc2019_64' }
    $QtRoot = (Resolve-Path -LiteralPath $QtRoot).Path
    $taskQmake = Join-Path $QtRoot 'bin/qmake.exe'
    if (-not (Test-Path -LiteralPath $taskQmake -PathType Leaf)) { throw 'QtRoot must contain bin/qmake.exe.' }
    $taskQtVersion = & $taskQmake -query QT_VERSION
    if ($LASTEXITCODE -ne 0 -or $taskQtVersion.Trim() -ne '5.15.2') { throw 'Desktop requires exactly Qt 5.15.2.' }
    if (-not (Test-Path -LiteralPath (Join-Path $QtRoot 'lib/Qt5Core.lib'))) { throw 'Desktop requires an MSVC Qt kit.' }

    if (-not (Get-Command cl -ErrorAction SilentlyContinue)) {
        $taskVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
        if (-not (Test-Path -LiteralPath $taskVswhere)) { throw 'Install Visual Studio C++ x64 tools first.' }
        $taskVsRoot = & $taskVswhere -latest -products '*' -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($LASTEXITCODE -ne 0 -or -not $taskVsRoot) { throw 'Visual Studio 2022 C++ tools were not found.' }
        & (Join-Path $taskVsRoot 'Common7/Tools/Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
    }
    if ($env:VSCMD_ARG_TGT_ARCH -ne 'x64') { throw 'Desktop requires an x64 MSVC developer environment.' }
    $taskTemp = New-Item -ItemType Directory -Force -Path (Join-Path $taskRoot 'build/msvc-tmp')
    $env:TEMP = $taskTemp.FullName
    $env:TMP = $taskTemp.FullName
    # 只选择实际安装的语言资源；中文字符串由码点构造，兼容 Windows PowerShell 的脚本编码。
    $taskCompilerDirectory = Split-Path -Parent (Get-Command cl).Source
    if (Test-Path -LiteralPath (Join-Path $taskCompilerDirectory '1033/clui.dll')) {
        $env:VSLANG = '1033'
        $env:CORDIS_MSVC_SHOWINCLUDES_PREFIX = 'Note: including file:'
    }
    elseif (Test-Path -LiteralPath (Join-Path $taskCompilerDirectory '2052/clui.dll')) {
        $env:VSLANG = '2052'
        $env:CORDIS_MSVC_SHOWINCLUDES_PREFIX = -join [char[]]@(0x6ce8, 0x610f, 0x3a, 0x20, 0x5305, 0x542b, 0x6587, 0x4ef6, 0x3a)
    }
    $env:CORDIS_QT5152_ROOT = $QtRoot
    $env:PATH = (Join-Path $QtRoot 'bin') + [System.IO.Path]::PathSeparator + $env:PATH
    if (-not $env:CMAKE_BUILD_PARALLEL_LEVEL) { $env:CMAKE_BUILD_PARALLEL_LEVEL = '4' }

    if ($Action -eq 'run') {
        if (-not $Preset.StartsWith('ela-')) { throw 'The configuration workbench requires an Ela preset.' }
        $taskExecutable = Join-Path $taskRoot "build/$Preset/bin/cordis_config_workbench.exe"
        if (-not (Test-Path -LiteralPath $taskExecutable)) { throw 'Build the selected Ela preset before running it.' }
        $env:QT_QPA_PLATFORM = 'windows'
        $taskArguments = @()
        if ($ConfigurationFile) { $taskArguments = @('--file', $ConfigurationFile) }
        $ErrorActionPreference = 'Continue'
        & $taskExecutable @taskArguments
        $taskRunExitCode = $LASTEXITCODE
        $ErrorActionPreference = 'Stop'
        if ($taskRunExitCode -ne 0) { throw "Workbench exited with code $taskRunExitCode." }
    }
    else {
        & (Join-Path $PSScriptRoot 'dev.ps1') -Action $Action -Preset $Preset -QtRoot $QtRoot
    }
}
finally {
    Pop-Location
}
