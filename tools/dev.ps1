<#
.SYNOPSIS
运行 Cordis 项目的配置、构建、测试或完整验证流程。

.DESCRIPTION
通过项目 CMake 预设执行所选开发操作，并在外部命令失败时报告错误。

.PARAMETER Action
要执行的操作：doctor、configure、build、test 或 verify。

.PARAMETER Preset
要使用的 CMake 预设。

.PARAMETER QtRoot
Qt 5.15.2 kit 根目录；Qt/Ela 预设使用，默认读取 CORDIS_QT5152_ROOT。
#>
[CmdletBinding()]
param(
    [ValidateSet('doctor', 'configure', 'build', 'test', 'verify')]
    [string]$Action = 'verify',
    [ValidateSet('core-gcc-debug', 'core-gcc-release', 'core-msvc-debug', 'core-msvc-release', 'qt-msvc-debug', 'qt-msvc-release', 'ela-msvc-debug', 'ela-msvc-release')]
    [string]$Preset = 'core-gcc-debug',
    [string]$QtRoot = $env:CORDIS_QT5152_ROOT
)

$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot

<#
.SYNOPSIS
查找可用的 CMake 可执行文件。

.OUTPUTS
System.String。返回 CMake 可执行文件的完整路径。

.NOTES
未找到 CMake 时抛出带有配置提示的错误。
#>
function Find-CMake {
    $taskFound = Get-Command cmake -ErrorAction SilentlyContinue
    if ($taskFound) { return $taskFound.Source }
    $taskCandidates = @()
    if ($env:CORDIS_CMAKE_BIN) {
        $taskCandidates += Join-Path $env:CORDIS_CMAKE_BIN 'cmake.exe'
    }
    $taskCandidates += @('D:\Qt\Tools\CMake_64\bin\cmake.exe', 'C:\Qt\Tools\CMake_64\bin\cmake.exe', 'C:\Program Files\CMake\bin\cmake.exe')
    foreach ($taskCandidate in $taskCandidates) {
        if (Test-Path -LiteralPath $taskCandidate -PathType Leaf) { return $taskCandidate }
    }
    throw 'CMake was not found. Add CMake to PATH or set CORDIS_CMAKE_BIN to its bin directory.'
}

<#
.SYNOPSIS
运行外部程序并检查退出码。

.PARAMETER Executable
要运行的可执行文件路径。

.PARAMETER Arguments
传递给可执行文件的参数。

.NOTES
外部程序返回非零退出码时抛出错误。
#>
function Invoke-Checked([string]$Executable, [string[]]$Arguments) {
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Command failed with exit code $LASTEXITCODE : $Executable" }
}

$taskOriginalPath = $env:PATH
$taskOriginalQtRoot = $env:CORDIS_QT5152_ROOT
Push-Location -LiteralPath $taskRoot
try {
    $taskCmake = Find-CMake
    $taskCtestName = if ($IsLinux -or $IsMacOS) { 'ctest' } else { 'ctest.exe' }
    $taskCtest = Join-Path (Split-Path -Parent $taskCmake) $taskCtestName
    if (-not (Test-Path -LiteralPath $taskCtest -PathType Leaf)) { throw 'CTest must be installed beside CMake.' }

    if ($Preset.StartsWith('qt-') -or $Preset.StartsWith('ela-')) {
        if (-not $QtRoot) { throw 'Set -QtRoot or CORDIS_QT5152_ROOT to the Qt 5.15.2 MSVC x64 kit.' }
        $taskQtRoot = (Resolve-Path -LiteralPath $QtRoot).Path
        $taskQmake = Join-Path $taskQtRoot 'bin/qmake.exe'
        if (-not (Test-Path -LiteralPath $taskQmake -PathType Leaf)) { throw 'QtRoot must contain bin/qmake.exe.' }
        $taskQtVersion = & $taskQmake -query QT_VERSION
        if ($LASTEXITCODE -ne 0 -or $taskQtVersion.Trim() -ne '5.15.2') { throw 'Qt preset requires exactly Qt 5.15.2.' }
        if (-not (Test-Path -LiteralPath (Join-Path $taskQtRoot 'lib/Qt5Core.lib') -PathType Leaf)) { throw 'Qt preset requires an MSVC kit.' }
        $env:CORDIS_QT5152_ROOT = $taskQtRoot
        $env:PATH = (Join-Path $taskQtRoot 'bin') + [System.IO.Path]::PathSeparator + $env:PATH
    }

    if ($Action -eq 'doctor') {
        Invoke-Checked $taskCmake @('--version')
        foreach ($taskTool in @('ninja', 'g++', 'cl', 'qmake')) {
            $taskResolved = Get-Command $taskTool -ErrorAction SilentlyContinue
            if ($taskResolved) { Write-Output "$taskTool : $($taskResolved.Source)" }
            else { Write-Output "$taskTool : not on PATH" }
        }
        Write-Output 'Qt target: exactly 5.15.2; doctor does not certify installed Qt compatibility.'
    }
    if ($Action -in @('configure', 'verify')) {
        Invoke-Checked $taskCmake @('--preset', $Preset)
    }
    if ($Action -in @('build', 'verify')) {
        Invoke-Checked $taskCmake @('--build', '--preset', $Preset)
    }
    if ($Action -in @('test', 'verify')) {
        Invoke-Checked $taskCtest @('--preset', $Preset)
    }
}
finally {
    $env:PATH = $taskOriginalPath
    $env:CORDIS_QT5152_ROOT = $taskOriginalQtRoot
    Pop-Location
}
