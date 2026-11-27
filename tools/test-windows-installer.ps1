<#
.SYNOPSIS
在交互式 Windows x64 桌面验证当前用户安装、覆盖安装、运行日志和卸载。
.DESCRIPTION
从普通 PowerShell 运行。已安装相同 AppId 时终止，避免覆盖已有安装。
验证在临时目录下创建独立安装和工作区；输出目录保留安装日志和工作区以便复查。
.PARAMETER Installer
待验证的 Eve Windows 安装程序。
.PARAMETER LegacyInstaller
可选的 0.1.5 基线安装程序，用同一 AppId 验证改名升级、原工作区与快捷方式。
#>
[CmdletBinding()]
param([Parameter(Mandatory)][string]$Installer, [string]$LegacyInstaller)

$ErrorActionPreference = 'Stop'
if (-not [Environment]::Is64BitProcess) { throw '请使用 64 位 PowerShell 运行桌面验收。' }
$principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if ($principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw '请在普通权限的 PowerShell 中验证当前用户安装。'
}
$setup = (Get-Item -LiteralPath $Installer).FullName
if ($LegacyInstaller) { $LegacyInstaller = (Get-Item -LiteralPath $LegacyInstaller).FullName }
$definition = Get-Content -LiteralPath (Join-Path $PSScriptRoot '../packaging/windows/Eve.iss') -Encoding UTF8 -Raw
$id = [regex]::Match($definition, '(?im)^AppId=\{(\{[0-9a-f-]{36}\})\s*$')
if (-not $id.Success) { throw '无法从安装定义读取稳定的 AppId。' }
$key = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\' + $id.Groups[1].Value + '_is1'
$suffix = [Guid]::NewGuid().ToString('N')
$evidence = Join-Path ([IO.Path]::GetTempPath()) "Eve-install-$suffix"
$app = Join-Path $evidence '应用 安装'
$workspace = Join-Path $evidence '工作区 数据'
$sessionFile = Join-Path $workspace 'session.yaml'
$group = "Eve-Smoke-$suffix"
$groupDir = Join-Path ([Environment]::GetFolderPath('Programs')) $group

# 同时检查两种安装范围与两种注册表视图。注册表句柄只在读取期间持有。
function Get-Registration {
    foreach ($hive in 'CurrentUser', 'LocalMachine') {
        foreach ($view in 'Registry64', 'Registry32') {
            $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey($hive, $view)
            try {
                $entry = $base.OpenSubKey($key)
                if ($entry) {
                    try {
                        [pscustomobject]@{ Hive=$hive; Location=([string]$entry.GetValue('InstallLocation')).TrimEnd('\') }
                    } finally { $entry.Dispose() }
                }
            } finally { $base.Dispose() }
        }
    }
}

function Invoke-SetupProcess([string]$Exe, [string[]]$Options, [int]$ExpectedExit = 0) {
    $process = Start-Process -FilePath $Exe -ArgumentList $Options -WorkingDirectory $evidence -WindowStyle Hidden -PassThru
    try {
        if (-not $process.WaitForExit(120000)) { throw "安装程序未按时结束：$Exe" }
        if ($process.ExitCode -ne $ExpectedExit) { throw "$Exe 退出码 $($process.ExitCode)，预期 $ExpectedExit，详见 $evidence" }
    } finally { $process.Dispose() }
}

function Get-OwnedInstallation {
    $entries = @(Get-Registration)
    if (-not $entries.Count -or @($entries | Where-Object { $_.Hive -ne 'CurrentUser' -or $_.Location -ne $app }).Count) {
        throw "注册表未指向本次当前用户安装：$app"
    }
}

function Install-Release([string]$Exe, [string]$Phase, [switch]$FirstInstall, [int]$ExpectedExit = 0) {
    $log = Join-Path $evidence "$Phase.log"
    $options = @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/NOCLOSEAPPLICATIONS', '/TASKS=!desktopicon',
        ('/LOG="{0}"' -f $log))
    if ($FirstInstall) { $options += "/GROUP=$group", ('/DIR="{0}"' -f $app) }
    Invoke-SetupProcess $Exe $options $ExpectedExit
    Get-OwnedInstallation
}

function Uninstall-Eve {
    Get-OwnedInstallation
    $uninstaller = @(Get-ChildItem -LiteralPath $app -Filter 'unins*.exe' -File)
    if ($uninstaller.Count -ne 1) { throw '测试安装目录中缺少唯一的卸载器。' }
    Invoke-SetupProcess $uninstaller[0].FullName @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART',
        ('/LOG="{0}"' -f (Join-Path $evidence 'uninstall.log')))
    if (@(Get-Registration).Count) { throw '卸载后仍有注册记录。' }
}

function Start-Workbench([string]$Executable, [string]$Product = 'Eve') {
    $process = Start-Process -FilePath $Executable -ArgumentList @('--workspace', ('"{0}"' -f $workspace)) `
        -WorkingDirectory $evidence -WindowStyle Normal -PassThru
    try {
        $clock = [Diagnostics.Stopwatch]::StartNew()
        do {
            $process.Refresh()
            if ($process.HasExited -or $clock.Elapsed.TotalSeconds -gt 30) { throw '工作台未能显示可响应的主窗口。' }
            if ($process.MainWindowHandle -ne 0 -and $process.MainWindowTitle -like "$Product*" -and $process.Responding) { break }
            Start-Sleep -Milliseconds 100
        } while ($true)
        # 同时核验已加载模块路径，避免开发机的 Qt kit 或全局安装的 VC 库掩盖遗漏。
        $loaded = @{}
        foreach ($module in $process.Modules) { $loaded[$module.ModuleName] = $module.FileName }
        foreach ($dll in "${Product}_core.dll","${Product}_qt.dll","${Product}_ui.dll",'ElaWidgetTools.dll',
                'Qt5Core.dll','Qt5Gui.dll','Qt5Widgets.dll','qwindows.dll','msvcp140.dll','vcruntime140.dll') {
            if (-not $loaded[$dll] -or -not $loaded[$dll].StartsWith(($app + '\'), [StringComparison]::OrdinalIgnoreCase)) {
                throw "未从安装目录加载 $dll ($($loaded[$dll]))"
            }
        }
    } finally {
        if (-not $process.HasExited) {
            if (-not $process.CloseMainWindow() -or -not $process.WaitForExit(30000)) {
                throw "工作台未能正常退出，进程 $($process.Id)。"
            }
        }
        $code = $process.ExitCode
        $process.Dispose()
    }
    if ($code -ne 0) { throw "工作台退出码 $code。" }
}

function Read-Log([string[]]$Options) {
    $output = @(& $query --workspace $workspace @Options)
    if ($LASTEXITCODE -ne 0) { throw "归档 CLI 退出码 $LASTEXITCODE。" }
    $output
}

$lock = [Threading.Mutex]::new($false, ('Local\Eve-Installer-Smoke-' + $id.Groups[1].Value))
$held = $false
$uninstallNeeded = $false
$environment = @{}
try {
    try { $held = $lock.WaitOne(0) } catch [Threading.AbandonedMutexException] { $held = $true }
    if (-not $held -or @(Get-Registration).Count) { throw '已有相同 AppId 的安装或验收正在进行。' }
    if ((Test-Path -LiteralPath $evidence) -or (Test-Path -LiteralPath $groupDir)) { throw '测试目录发生冲突。' }
    $null = New-Item -ItemType Directory -Path $workspace
    Write-Host "验收工作目录：$evidence"
    foreach ($name in 'Path','QT_PLUGIN_PATH','QT_QPA_PLATFORM_PLUGIN_PATH','QT_QPA_PLATFORM','QT_QPA_PLATFORMTHEME') {
        $environment[$name] = [Environment]::GetEnvironmentVariable($name)
        [Environment]::SetEnvironmentVariable($name, $null)
    }
    $env:Path = "$env:SystemRoot\System32;$env:SystemRoot"
    $env:QT_QPA_PLATFORM = 'windows'
    $uninstallNeeded = $true
    if ($LegacyInstaller) {
        Install-Release $LegacyInstaller 'legacy-install' -FirstInstall
        $oldLink = Join-Path $groupDir 'Cordis.lnk'
        if (-not (Test-Path -LiteralPath $oldLink)) { throw '基线安装未创建测试快捷方式。' }
        Start-Workbench (Join-Path $app 'cordis_plugin_workbench.exe') 'Cordis'
        $legacyConfigHash = (Get-FileHash -LiteralPath $sessionFile).Hash

        # 改写旧安装拥有的名字，升级应在写入新程序之前退出，原始内容由测试自己恢复。
        $oldCli = Join-Path $app 'cordis_log_query.exe'
        $backup = [IO.File]::ReadAllBytes($oldCli)
        try {
            [IO.File]::WriteAllText($oldCli, $suffix)
            Install-Release $setup 'reject-modified' -ExpectedExit 7
            if ([IO.File]::ReadAllText($oldCli) -ne $suffix -or (Test-Path (Join-Path $app 'eve_core.dll'))) {
                throw '升级预检更改了受保护的安装文件。'
            }
        } finally { [IO.File]::WriteAllBytes($oldCli, $backup) }
        Install-Release $setup 'upgrade'
        if ((Test-Path -LiteralPath $oldLink) -or @(Get-ChildItem -LiteralPath $app -Filter 'cordis_*').Count) {
            throw '升级仍残留旧版程序或旧快捷方式。'
        }
        if ((Get-FileHash -LiteralPath $sessionFile).Hash -ne $legacyConfigHash) { throw '升级更改了旧工作区。' }
    } else {
        Install-Release $setup 'install' -FirstInstall
    }
    $shortcut = Get-ChildItem -LiteralPath $groupDir -Filter '*.lnk' -File | Where-Object BaseName -eq 'Eve'
    if (-not $shortcut) { throw '缺少开始菜单快捷方式。' }
    $shell = New-Object -ComObject WScript.Shell
    $link = $shell.CreateShortcut($shortcut.FullName)
    $exe = $link.TargetPath
    if (-not $exe.StartsWith(($app + '\'), [StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetFileName($exe) -ne 'eve_plugin_workbench.exe') { throw "快捷方式目标错误：$exe" }
    $query = Join-Path (Split-Path $exe -Parent) 'eve_log_query.exe'
    Start-Workbench $exe
    $configuration = Get-Content -LiteralPath $sessionFile -Encoding UTF8 -Raw | ConvertFrom-Json
    if ($configuration.formatVersion -ne 1 -or $configuration.plugins.Count -ne 0) { throw '首次启动未生成空工作区。' }
    $firstHash = (Get-FileHash -LiteralPath $sessionFile).Hash
    # 模拟用户手工放进应用目录的文件，用于核验升级和卸载的文件所有权。
    $userFile = Join-Path $app '用户备注.txt'
    Set-Content -LiteralPath $userFile -Value $suffix -Encoding UTF8
    $userHash = (Get-FileHash -LiteralPath $userFile).Hash
    Install-Release $setup 'reinstall'
    Start-Workbench $exe
    $runs = @(Read-Log @())
    $expectedRuns = if ($LegacyInstaller) { 3 } else { 2 }
    if ($runs.Count -ne $expectedRuns) { throw "预期保留 $expectedRuns 次独立运行的归档，实际 $($runs.Count) 次。" }
    $runs | Set-Content -LiteralPath (Join-Path $evidence 'runs.tsv') -Encoding UTF8
    foreach ($run in $runs) {
        $runId = ($run -split "`t")[0]
        $sources = @(Read-Log @('--run', $runId, '--sources'))
        $events = @(Read-Log @('--run', $runId, '--source', 'platform'))
        if (-not ($sources -match "^platform`t") -or -not ($events -match 'code=WORKBENCH_CLEANUP_COMPLETE')) {
            throw "安装目录内的 CLI 无法读回平台来源及关闭事件：$runId"
        }
    }
    $hashes = @(Get-ChildItem -LiteralPath $workspace -Recurse -File | Get-FileHash)
    Uninstall-Eve
    $uninstallNeeded = $false
    # Inno 从临时副本执行卸载，启动进程退出后仍可能短暂停留着卸载器自身。
    $clock = [Diagnostics.Stopwatch]::StartNew()
    do {
        $remaining = @(Get-ChildItem -LiteralPath $app -Force)
        $clean = $remaining.Count -eq 1 -and $remaining[0].FullName -eq $userFile -and -not (Test-Path -LiteralPath $groupDir)
        if ($clean) { break }
        Start-Sleep -Milliseconds 200
    } while ($clock.Elapsed.TotalSeconds -lt 10)
    if (-not $clean -or (Get-FileHash -LiteralPath $userFile).Hash -ne $userHash) {
        throw '卸载后的文件或快捷方式与预期不符，应只剩用户备注。'
    }
    if ((Get-FileHash -LiteralPath $sessionFile).Hash -ne $firstHash) { throw '覆盖安装后工作区配置发生变化。' }
    foreach ($file in $hashes) {
        if ((Get-FileHash -LiteralPath $file.Path).Hash -ne $file.Hash) { throw "卸载更改了工作区文件：$($file.Path)" }
    }
    Write-Output "通过安装、覆盖、$expectedRuns 次本机桌面启动、日志查询和卸载验证。记录：$evidence"
} finally {
    try {
        # 只对随机目录中、注册表确认属于本次调用的安装执行失败清理。
        if ($uninstallNeeded -and @(Get-Registration | Where-Object Location -eq $app).Count) { Uninstall-Eve }
    } finally {
        foreach ($name in $environment.Keys) { [Environment]::SetEnvironmentVariable($name, $environment[$name]) }
        if ($held) { $lock.ReleaseMutex() }
        $lock.Dispose()
    }
}
