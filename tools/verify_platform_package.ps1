param(
    [Parameter(Mandatory = $true)][string]$Archive,
    [Parameter(Mandatory = $true)][string]$EvidenceDirectory,
    [string]$ExpectedSha256,
    [string]$ModuleManifest
)

$ErrorActionPreference = 'Stop'

function Require([bool]$Condition, [string]$Message) {
    if (!$Condition) {
        throw $Message
    }
}

function Get-Inventory([string]$Directory) {
    $items = [ordered]@{}
    Get-ChildItem -LiteralPath $Directory -File -Recurse -Force | Sort-Object FullName | ForEach-Object {
        $relative = $_.FullName.Substring($Directory.Length + 1).Replace([IO.Path]::DirectorySeparatorChar, '/')
        $items[$relative] = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
    }
    return ($items | ConvertTo-Json -Compress)
}

function Invoke-Product([string]$Program, [string[]]$Arguments, [bool]$HasWindow, [int]$ExpectedExit,
                        [string]$FixtureWorkspace) {
    $startup = [Diagnostics.ProcessStartInfo]::new()
    $startup.FileName = $Program
    $startup.WorkingDirectory = $foreignWorkingDirectory
    $startup.Arguments = ($Arguments | ForEach-Object { '"' + $_ + '"' }) -join ' '
    $startup.UseShellExecute = $false
    $startup.RedirectStandardOutput = $true
    $startup.RedirectStandardError = $true
    $startup.StandardOutputEncoding = [Text.Encoding]::UTF8
    $startup.StandardErrorEncoding = [Text.Encoding]::UTF8
    foreach ($name in @('QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'QT_QPA_PLATFORM',
            'QML_IMPORT_PATH', 'QML2_IMPORT_PATH', 'QTDIR')) {
        $startup.EnvironmentVariables.Remove($name)
    }
    $startup.EnvironmentVariables['PATH'] = (Join-Path $systemRoot 'System32') + ';' + $systemRoot
    if ($HasWindow) {
        $startup.EnvironmentVariables['QT_QPA_PLATFORM'] = 'windows'
    }
    $process = [Diagnostics.Process]::Start($startup)
    try {
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        $modules = @()
        $title = ''
        if ($HasWindow) {
            $limit = [Diagnostics.Stopwatch]::StartNew()
            while (!$process.HasExited -and $process.MainWindowHandle -eq [IntPtr]::Zero -and
                   $limit.ElapsedMilliseconds -lt 15000) {
                Start-Sleep -Milliseconds 50
                $process.Refresh()
            }
            if ($process.HasExited) {
                throw "Workbench exited before creating a window: $($stderr.GetAwaiter().GetResult())"
            }
            Require ($process.MainWindowHandle -ne [IntPtr]::Zero) 'The native workbench window was not created.'
            $title = $process.MainWindowTitle
            $modules = @($process.Modules | ForEach-Object {
                [pscustomobject]@{name = $_.ModuleName; path = $_.FileName}
            })
            foreach ($library in @('cordis_core.dll', 'cordis_qt.dll', 'cordis_ui.dll', 'ElaWidgetTools.dll',
                    'Qt5Core.dll', 'Qt5Gui.dll', 'Qt5Widgets.dll', 'qwindows.dll')) {
                $loaded = $modules | Where-Object name -EQ $library | Select-Object -First 1
                Require ($null -ne $loaded -and $loaded.path.StartsWith($packagePrefix,
                    [StringComparison]::OrdinalIgnoreCase)) "Expected a package-local $library."
            }
            if ($FixtureWorkspace) {
                $logged = $false
                do {
                    $matching = Get-ChildItem -LiteralPath $FixtureWorkspace -Recurse -File -Filter 'events-*.jsonl' |
                        Select-String -SimpleMatch 'TEST_OPERATION_REJECTED'
                    $logged = $null -ne $matching
                    if (!$logged) { Start-Sleep -Milliseconds 50 }
                } while (!$logged -and !$process.HasExited -and $limit.ElapsedMilliseconds -lt 15000)
                Require $logged 'The optional external acceptance module did not publish an event.'
            }
            Require ($process.CloseMainWindow()) 'The native close request was not delivered.'
        }
        Require ($process.WaitForExit(20000)) 'The process did not exit in time.'
        $output = $stdout.GetAwaiter().GetResult()
        $errors = $stderr.GetAwaiter().GetResult()
        Require ($process.ExitCode -eq $ExpectedExit) "Unexpected exit $($process.ExitCode): $errors"
        return [pscustomobject]@{pid = $process.Id; exitCode = $process.ExitCode; title = $title;
            stdout = $output; stderr = $errors; modules = $modules}
    } finally {
        if (!$process.HasExited) {
            $process.Kill()
            [void]$process.WaitForExit(2000)
        }
        $process.Dispose()
    }
}

function Get-RunDirectories([string]$Workspace) {
    Get-ChildItem -LiteralPath (Join-Path $Workspace '.cordis/logs') -Directory
}

function Get-TerminalRun([IO.DirectoryInfo]$Run) {
    $data = Get-Content -LiteralPath (Join-Path $Run.FullName 'run.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    Require ($data.run -eq $Run.Name -and $data.ended) 'A matching completed run is required.'
    Require (!(Test-Path -LiteralPath (Join-Path $Run.FullName 'active.lock'))) 'An active run lock remains.'
    $events = @(Get-ChildItem -LiteralPath $Run.FullName -Filter 'events-*.jsonl' -File | Sort-Object Name |
        ForEach-Object { Get-Content -LiteralPath $_.FullName -Encoding UTF8 } |
        ForEach-Object { ConvertFrom-Json $_ })
    Require (@($events | Where-Object code -EQ 'WORKBENCH_CLEANUP_COMPLETE').Count -eq 1) 'Missing unique cleanup event.'
    return [pscustomobject]@{metadata = $data; events = $events}
}

$archiveFile = Get-Item -LiteralPath $Archive
$archiveHash = (Get-FileHash -LiteralPath $archiveFile.FullName -Algorithm SHA256).Hash
if ($ExpectedSha256) {
    Require ($archiveHash -eq $ExpectedSha256) 'The archive SHA-256 does not match.'
}
Require (!(Test-Path -LiteralPath $EvidenceDirectory)) 'The evidence directory must be new.'
$outputDirectory = (New-Item -ItemType Directory -Path $EvidenceDirectory).FullName
$installed = (New-Item -ItemType Directory -Path (Join-Path $outputDirectory 'Installed platform')).FullName
Expand-Archive -LiteralPath $archiveFile.FullName -DestinationPath $installed
$executables = @(Get-ChildItem -LiteralPath $installed -Recurse -File -Filter 'cordis_plugin_workbench.exe')
Require ($executables.Count -eq 1) 'The package must contain one workbench.'
$workbench = $executables[0].FullName
$packageDirectory = Split-Path -Parent $workbench
$packagePrefix = $packageDirectory + [IO.Path]::DirectorySeparatorChar
$query = Join-Path $packageDirectory 'cordis_log_query.exe'
Require (Test-Path -LiteralPath $query -PathType Leaf) 'The packaged log query is missing.'
$manifest = Get-Content -LiteralPath (Join-Path $packageDirectory 'build-manifest.json') -Raw -Encoding UTF8 |
    ConvertFrom-Json
Require ($manifest.schema -eq 'cordis.platform.package/1' -and @($manifest.businessModules).Count -eq 0) 'Invalid package manifest.'
$systemRoot = [Environment]::GetFolderPath([Environment+SpecialFolder]::Windows)
$foreignWorkingDirectory = (New-Item -ItemType Directory -Path (Join-Path $outputDirectory 'Unrelated cwd')).FullName
$workspace = (New-Item -ItemType Directory -Path (Join-Path $outputDirectory ('Empty workspace ' +
    [char]0x5DE5 + [char]0x4F5C))).FullName
$installedBytes = Get-Inventory $packageDirectory

$first = Invoke-Product $workbench @('--workspace', $workspace) $true 0
Require (@(Get-RunDirectories $workspace).Count -eq 1) 'First empty launch must create exactly one run.'
$savedSession = Join-Path $workspace 'session.yaml'
$sessionBytes = (Get-FileHash -LiteralPath $savedSession).Hash
$configuration = Get-Content -LiteralPath $savedSession -Raw -Encoding UTF8 | ConvertFrom-Json
Require ($configuration.formatVersion -eq 1 -and @($configuration.plugins).Count -eq 0) 'Expected a saved empty configuration.'
$second = Invoke-Product $workbench @('--workspace', $workspace) $true 0
$archives = @(Get-RunDirectories $workspace)
Require ($archives.Count -eq 2) 'Reopen must append a second independent run.'
Require ((Get-FileHash -LiteralPath $savedSession).Hash -eq $sessionBytes) 'Reopen changed the saved configuration.'
$observations = @($archives | ForEach-Object { Get-TerminalRun $_ })
$logs = Join-Path $workspace '.cordis/logs'
$runsBefore = Get-Inventory $logs
$index = Invoke-Product $query @('--workspace', $workspace) $false 0
foreach ($observation in $observations) {
    $runId = $observation.metadata.run
    Require ($index.stdout.Contains($runId)) 'The packaged query missed a recorded run.'
    $cleanup = $observation.events | Where-Object code -EQ 'WORKBENCH_CLEANUP_COMPLETE'
    $event = Invoke-Product $query @('--workspace', $workspace, '--run', $runId, '--event', $cleanup.event) $false 0
    Require ($event.stdout.Contains('WORKBENCH_CLEANUP_COMPLETE')) 'The packaged query missed an exact event.'
}
Require ($runsBefore -eq (Get-Inventory $logs)) 'The read-only query modified an archive.'

$dependent = (New-Item -ItemType Directory -Path (Join-Path $outputDirectory 'Module required workspace')).FullName
$dependentFile = Join-Path $dependent 'session.yaml'
[IO.File]::WriteAllText($dependentFile, '{"formatVersion":1,"modules":{"extension":{"manifest":"module.json"}},"plugins":[{"id":"main","plugin":"test.logging","module":"extension"}]}')
$dependentHash = (Get-FileHash -LiteralPath $dependentFile).Hash
$rejected = Invoke-Product $workbench @('--workspace', $dependent) $false 1
Require ((Get-FileHash -LiteralPath $dependentFile).Hash -eq $dependentHash) 'The rejected launch changed the configuration.'
$rejectedRuns = @(Get-RunDirectories $dependent)
Require ($rejectedRuns.Count -eq 1) 'Module-required diagnostics must have a run.'
$failure = Get-TerminalRun $rejectedRuns[0]
Require (@($failure.events | Where-Object code -EQ 'WORKBENCH_MODULE_REQUIRED').Count -eq 1) 'Missing module-required diagnostic.'

$external = $null
if ($ModuleManifest) {
    $selected = (Get-Item -LiteralPath $ModuleManifest).FullName
    $moduleSpace = (New-Item -ItemType Directory -Path (Join-Path $outputDirectory 'External module workspace')).FullName
    $external = Invoke-Product $workbench @('--workspace', $moduleSpace, '--module', $selected) $true 0 $moduleSpace
    $externalRuns = @(Get-RunDirectories $moduleSpace)
    Require ($externalRuns.Count -eq 1) 'The optional module launch must have a run.'
    $moduleRun = Get-TerminalRun $externalRuns[0]
    Require (@($moduleRun.events | Where-Object source -EQ 'plugin/test.logging').Count -gt 0) 'Missing fixture event.'
}

Require ($installedBytes -eq (Get-Inventory $packageDirectory)) 'The package content changed after use.'
$receipt = [ordered]@{passed = $true; archive = $archiveFile.Name; sha256 = $archiveHash;
    systemPathOnly = $true; firstBoot = $first; reopen = $second; runIds = @($observations.metadata.run);
    sessionSha256 = $sessionBytes; queryRunList = $index.stdout; missingModule = $rejected;
    missingModuleRun = $failure.metadata.run; externalFixture = $external}
$receipt | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $outputDirectory 'result.json') -Encoding UTF8
Write-Output "Standalone platform acceptance passed: $outputDirectory"
