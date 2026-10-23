[CmdletBinding()]
param(
    [ValidateSet('doctor', 'configure', 'build', 'test', 'verify')]
    [string]$Action = 'verify',
    [ValidateSet('core-gcc-debug', 'core-gcc-release', 'core-msvc-debug', 'core-msvc-release')]
    [string]$Preset = 'core-gcc-debug'
)

$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot

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

function Invoke-Checked([string]$Executable, [string[]]$Arguments) {
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Command failed with exit code $LASTEXITCODE : $Executable" }
}

Push-Location -LiteralPath $taskRoot
try {
    $taskCmake = Find-CMake
    $taskCtestName = if ($IsLinux -or $IsMacOS) { 'ctest' } else { 'ctest.exe' }
    $taskCtest = Join-Path (Split-Path -Parent $taskCmake) $taskCtestName
    if (-not (Test-Path -LiteralPath $taskCtest -PathType Leaf)) { throw 'CTest must be installed beside CMake.' }

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
finally { Pop-Location }
