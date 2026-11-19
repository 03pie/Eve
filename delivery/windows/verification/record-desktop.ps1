<#
.SYNOPSIS
Record four user-operated preview processes; no automatic click, close or kill.
.DESCRIPTION
Requires a new external data directory and the manifest hash from the delivery
record. Recording completion is separate from UI and human acceptance.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$PackageRoot,
    [Parameter(Mandatory=$true)][string]$DataRoot,
    [Parameter(Mandatory=$true)][ValidatePattern('^[0-9a-fA-F]{64}$')][string]$ExpectedManifestSha256,
    [string]$WorkingDirectory,
    [ValidateRange(1,3600)][int]$TimeoutSeconds=1800
)
Set-StrictMode -Version 2.0
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot 'preview-windows.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'preview-ui.psm1') -Force
$clock=[Diagnostics.Stopwatch]::StartNew()
$script:ObservationBytes=0L; $script:ObservationCount=0; $script:BudgetExceeded=$false
$script:ObservationLimit=16777216L

function Write-DesktopJson([string]$Path, $Value, [switch]$Replace) {
    $data=[Text.UTF8Encoding]::new($false).GetBytes(($Value | ConvertTo-Json -Depth 18) + "`n")
    $target=$Path
    if ($Replace) { $target=$Path+'.next' }
    $pendingTemp=$false; $primaryError=$null
    try {
        $stream=[IO.File]::Open($target,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write)
        $pendingTemp=[bool]$Replace
        try { $stream.Write($data,0,$data.Length) } finally { $stream.Dispose() }
        if ($Replace) {
            # PS5.1 converts ordinary $null to an empty string for this overload.
            if ([IO.File]::Exists($Path)) { [IO.File]::Replace($target,$Path,[NullString]::Value) }
            else { [IO.File]::Move($target,$Path) }
            $pendingTemp=$false
        }
    } catch { $primaryError=$_; throw }
    finally {
        if ($pendingTemp) {
            # Single writer: never delete a pre-existing .next that CreateNew refused.
            try { [IO.File]::Delete($target) }
            catch {
                if ($null -eq $primaryError) { throw }
                [Console]::Error.WriteLine('Owned status temp cleanup also failed: '+$_.Exception.Message)
            }
        }
    }
}

function Write-DesktopCompletion($Report,[string]$Root) {
    $written=$true
    try {
        Write-DesktopJson (Join-Path $Root 'status.json') @{ phase=if ($Report.recording_complete) { 'processes-finished' } else { 'incomplete' };
            recordingOnly=$true; completionEvidence='recording.json and recorder exit code 0'; liveProcess=$Report.liveProcess; error=$Report.error } -Replace
    } catch {
        $Report['statusWriteError']=$_.Exception.Message; $Report.recording_complete=$false; $written=$false
        if (-not $Report.error) { $Report.error='Cannot finalize desktop status: '+$_.Exception.Message }
        [Console]::Error.WriteLine('Cannot finalize desktop status: '+$_.Exception.Message)
    }
    try { Write-DesktopJson (Join-Path $Root 'recording.json') $Report }
    catch {
        $written=$false; $Report.recording_complete=$false
        $Report['reportWriteError']=$_.Exception.Message
        if (-not $Report.error) { $Report.error='Cannot save desktop report: '+$_.Exception.Message }
        [Console]::Error.WriteLine('Cannot save desktop report; original error: '+$Report.error+'; report error: '+$_.Exception.Message)
    }
    return $written
}

function Save-DesktopObservation([string]$Kind, $Value) {
    $json=($Value | ConvertTo-Json -Depth 18) + "`n"
    $size=[Text.Encoding]::UTF8.GetByteCount($json)
    if ($script:ObservationBytes+$size -gt $script:ObservationLimit -or $script:ObservationCount -ge 256) {
        $script:BudgetExceeded=$true
        return
    }
    $script:ObservationCount+=1; $script:ObservationBytes+=$size
    Write-DesktopJson (Join-Path $DataRoot ('observations/{0:d4}-{1}.json' -f $script:ObservationCount,$Kind)) $Value
}

function Copy-DesktopConfiguration([string]$Path,[string]$Destination) {
    $snapshot=Get-PreviewFileSnapshot $Path
    if (-not $snapshot.exists -or $snapshot.bytes -gt 8388608) { throw 'A saved configuration of at most 8 MiB is required.' }
    $raw=[IO.File]::ReadAllBytes($Path)
    $stream=[IO.File]::Open($Destination,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write)
    try { $stream.Write($raw,0,$raw.Length) } finally { $stream.Dispose() }
    $text=[Text.UTF8Encoding]::new($false,$true).GetString($raw)
    if ($text.StartsWith([string][char]0xfeff,[StringComparison]::Ordinal)) { $text=$text.Substring(1) }
    $parsed=ConvertFrom-Json -InputObject $text -ErrorAction Stop
    if (-not $parsed.PSObject.Properties['formatVersion'] -or $parsed.formatVersion -ne 1) { throw 'The saved configuration is not normalized formatVersion 1 JSON.' }
    return @{ snapshot=$snapshot; value=$parsed }
}

function Get-DesktopPathEvidence($Observation,$Manifest,$StorePaths) {
    $packagePrefix=$PackageRoot.TrimEnd('\','/')+'\'
    $systemPrefix=[IO.Path]::GetFullPath($env:WINDIR).TrimEnd('\')+'\'
    $declaredWrong=@()
    foreach ($module in $Observation.modules) {
        $path=[IO.Path]::GetFullPath($module.path)
        $name=[IO.Path]::GetFileName($path)
        $declared=@($Manifest.files | Where-Object {
            [IO.Path]::GetExtension($_.path) -ieq '.dll' -and [IO.Path]::GetFileName($_.path) -ieq $name
        })
        if ($declared.Count -and -not $StorePaths.Contains($path)) {
            $packageMatches=@($declared | Where-Object {
                $expected=[IO.Path]::GetFullPath((Join-Path $PackageRoot $_.path.Replace('/','\')))
                $path.Equals($expected,[StringComparison]::OrdinalIgnoreCase)
            })
            if (-not $packageMatches.Count) { $declaredWrong+=('Declared DLL outside package/store: '+$path) }
        }
        $kind='unclassified'; $relative=$null
        if ($path.StartsWith($packagePrefix,[StringComparison]::OrdinalIgnoreCase)) {
            $relative=$path.Substring($packagePrefix.Length).Replace('\','/')
            $kind=if (@($Manifest.files | Where-Object { $_.path.Equals($relative,[StringComparison]::OrdinalIgnoreCase) }).Count -eq 1) { 'package' } else { 'unlisted-package-path' }
        } elseif ($StorePaths.Contains($path)) { $kind='registered-module-store' }
        elseif ($path.StartsWith($systemPrefix,[StringComparison]::OrdinalIgnoreCase)) { $kind='windows-directory' }
        $module | Add-Member -NotePropertyName location -NotePropertyValue $kind
        $module | Add-Member -NotePropertyName relative -NotePropertyValue $relative
    }
    $critical=@($Manifest.files | Where-Object {
        $_.role -in @('qualified-environment','release-crt-deployment') -or $_.path -ceq 'plugins/platforms/qwindows.dll'
    })
    $missing=@(); $wrong=@()
    foreach ($file in $critical) {
        $expected=[IO.Path]::GetFullPath((Join-Path $PackageRoot $file.path.Replace('/','\')))
        $name=[IO.Path]::GetFileName($expected)
        $matches=@($Observation.modules | Where-Object { [IO.Path]::GetFileName($_.path) -ieq $name })
        if (-not $matches.Count) { $missing+=$file.path }
        elseif (@($matches | Where-Object { -not $_.path.Equals($expected,[StringComparison]::OrdinalIgnoreCase) }).Count) { $wrong+=$file.path }
    }
    $wrong+= $declaredWrong
    return [pscustomobject]@{ verified=($critical.Count -ge 12 -and $missing.Count -eq 0 -and $wrong.Count -eq 0);
        missing=$missing; wrong=$wrong; classificationIsNotPublisherTrust=$true }
}

$report=[ordered]@{ schema='cordis.preview.desktop.recording/1'; recording_complete=$false;
    human_acceptance_recorded=$false; ui_actions_automated=$false; packageRoot=$null; dataRoot=$null;
    runs=@(); error=$null; liveProcess=$null; observationBudgetExceeded=$false }
$created=$false; $active=$null; $exitCode=1
try {
    if (-not [Environment]::Is64BitProcess -or -not [Environment]::UserInteractive) { throw 'Use interactive 64-bit Windows PowerShell.' }
    $report['recorderSources']=@(
        (Get-PreviewFileSnapshot $PSCommandPath),
        (Get-PreviewFileSnapshot (Join-Path $PSScriptRoot 'preview-windows.psm1')),
        (Get-PreviewFileSnapshot (Join-Path $PSScriptRoot 'preview-ui.psm1')))
    $PackageRoot=Resolve-PreviewDirectory $PackageRoot
    $DataRoot=[IO.Path]::GetFullPath($DataRoot).TrimEnd('\','/')
    $packagePrefix=$PackageRoot.TrimEnd('\','/')+'\'
    if ($DataRoot.Equals($PackageRoot,[StringComparison]::OrdinalIgnoreCase) -or $DataRoot.StartsWith($packagePrefix,[StringComparison]::OrdinalIgnoreCase) -or
        [IO.Directory]::Exists($DataRoot) -or [IO.File]::Exists($DataRoot)) { throw 'DataRoot must be a new directory outside the package.' }
    [void](Resolve-PreviewDirectory ([IO.Path]::GetDirectoryName($DataRoot)))
    $manifestPath=Join-Path $PackageRoot 'build-manifest.json'
    if ((Get-PreviewFileSnapshot $manifestPath).sha256 -cne $ExpectedManifestSha256.ToLowerInvariant()) { throw 'External manifest anchor mismatch.' }
    foreach ($name in @('cordis_config_workbench','cordis_platform_workbench_demo')) {
        if (@(Get-Process -Name $name -ErrorAction SilentlyContinue).Count) { throw 'Close existing preview workbenches normally before recording.' }
    }
    [void][IO.Directory]::CreateDirectory($DataRoot); $created=$true
    foreach ($folder in @('observations','configuration','business/store','cwd')) { [void][IO.Directory]::CreateDirectory((Join-Path $DataRoot $folder)) }
    $WorkingDirectory=if ($WorkingDirectory) { Resolve-PreviewDirectory $WorkingDirectory } else { Join-Path $DataRoot 'cwd' }
    if ($WorkingDirectory.Equals($PackageRoot,[StringComparison]::OrdinalIgnoreCase) -or $WorkingDirectory.StartsWith($packagePrefix,[StringComparison]::OrdinalIgnoreCase)) { throw 'WorkingDirectory must be independent of the package.' }
    $report.packageRoot=$PackageRoot; $report.dataRoot=$DataRoot
    $hostFacts=Get-PreviewHostFacts
    Write-DesktopJson (Join-Path $DataRoot 'host.json') $hostFacts
    if ($null -eq $hostFacts.token -or $hostFacts.token.elevated -or $hostFacts.token.integrityRid -ge 12288) { throw 'Recording requires an observable non-elevated user token.' }
    $verify=Join-Path $PackageRoot 'verification/verify-package.ps1'
    $verifyInfo=New-PreviewProcessStartInfo -Executable (Join-Path $PSHOME 'powershell.exe') -WorkingDirectory $WorkingDirectory -Arguments @(
        '-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',$verify,'-PackageRoot',$PackageRoot,
        '-ExpectedManifestSha256',$ExpectedManifestSha256,'-ReportPath',(Join-Path $DataRoot 'package-verification.json'))
    $verification=[Diagnostics.Process]::new(); $verification.StartInfo=$verifyInfo; $verifyStarted=$false
    try {
        $verifyStarted=$verification.Start(); $vo=$verification.StandardOutput.ReadToEndAsync(); $ve=$verification.StandardError.ReadToEndAsync()
        $verificationBudget=[int][Math]::Max(1,[Math]::Min(30000,($TimeoutSeconds-$clock.Elapsed.TotalSeconds)*1000))
        if (-not $verification.WaitForExit($verificationBudget)) { throw 'Package verification did not finish; no product was started.' }
        [IO.File]::WriteAllText((Join-Path $DataRoot 'package-verification.stdout.log'),$vo.Result,[Text.UTF8Encoding]::new($false))
        [IO.File]::WriteAllText((Join-Path $DataRoot 'package-verification.stderr.log'),$ve.Result,[Text.UTF8Encoding]::new($false))
        if ($verification.ExitCode -ne 0) { throw 'Independent package verification failed.' }
    } finally {
        if ($verifyStarted -and -not $verification.HasExited) { $verification.Kill(); [void]$verification.WaitForExit(1000) }
        $verification.Dispose()
    }
    $manifest=ConvertFrom-Json -InputObject ([IO.File]::ReadAllText($manifestPath)) -ErrorAction Stop
    $frozen=Get-PreviewTreeSnapshot $PackageRoot
    Write-DesktopJson (Join-Path $DataRoot 'package-before.json') $frozen
    $configurationFile=Join-Path $DataRoot 'configuration/cordis.yaml'; $businessFile=Join-Path $DataRoot 'business/session.yaml'
    $storeRoot=Join-Path $DataRoot 'business/store'
    $expectedLocators=[ordered]@{}; $storePaths=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($alias in @('counter','reporter','notebook')) {
        foreach ($variant in @('a','b')) {
            $branch=if ($alias -eq 'notebook') { 'qt-upgrade' } else { 'native-upgrade' }
            $source=ConvertFrom-Json -InputObject ([IO.File]::ReadAllText((Join-Path $PackageRoot "$branch/$alias-$variant/module.json")))
            [void]$storePaths.Add([IO.Path]::GetFullPath((Join-Path $storeRoot ($source.sha256+'/'+$source.library))))
            if (($alias -eq 'notebook' -and $variant -eq 'b') -or ($alias -ne 'notebook' -and $variant -eq 'a')) { $expectedLocators[$alias]=$source.sha256+'/module.json' }
        }
    }
    $registration=[ordered]@{ schema='cordis.preview.desktop.registration/1'; packageRoot=$PackageRoot;
        manifestSha256=$ExpectedManifestSha256.ToLowerInvariant(); dataRoot=$DataRoot; workingDirectory=$WorkingDirectory;
        configurationFile=$configurationFile; businessFile=$businessFile; storeRoot=$storeRoot; preferencesFile=$hostFacts.preferencesFile }
    Write-DesktopJson (Join-Path $DataRoot 'registration.json') $registration
    Write-DesktopJson (Join-Path $DataRoot 'preferences-before.json') (Get-PreviewFileSnapshot $hostFacts.preferencesFile)
    Write-DesktopJson (Join-Path $DataRoot 'directory-permissions.json') ([ordered]@{
        packageSddl=([IO.DirectoryInfo]::new($PackageRoot).GetAccessControl().GetSecurityDescriptorSddlForm('Access, Owner, Group'));
        dataSddl=([IO.DirectoryInfo]::new($DataRoot).GetAccessControl().GetSecurityDescriptorSddlForm('Access, Owner, Group'));
        effectiveReadOnlyProven=$false; note='Record DACL and effective access separately; this recorder does not change ACLs.' })
    foreach ($entry in @('configuration','business')) {
        for ($ordinal=1; $ordinal -le 2; ++$ordinal) {
            if ($clock.Elapsed.TotalSeconds -ge $TimeoutSeconds) { throw 'Total recording time budget expired before launch.' }
            $index=$report.runs.Count+1; $prefix=Join-Path $DataRoot ('run-{0}' -f $index)
            $before=Get-PreviewTreeSnapshot $PackageRoot
            if (($before | ConvertTo-Json -Depth 5 -Compress) -cne ($frozen | ConvertTo-Json -Depth 5 -Compress)) { throw 'Package files changed before launch.' }
            Write-DesktopJson ($prefix+'-before.json') $before
            $executable=Join-Path $PackageRoot $(if ($entry -eq 'configuration') { 'cordis_config_workbench.exe' } else { 'cordis_platform_workbench_demo.exe' })
            $arguments=if ($entry -eq 'configuration') { @('--file',$configurationFile) } else { @($businessFile,$storeRoot) }
            $info=New-PreviewProcessStartInfo -Executable $executable -Arguments $arguments -WorkingDirectory $WorkingDirectory
            $run=[ordered]@{ index=$index; entry=$entry; ordinal=$ordinal; command=@($executable)+$arguments;
                cwd=$WorkingDirectory; environment=@{ PATH=$info.EnvironmentVariables['PATH']; QT_QPA_PLATFORM=$info.EnvironmentVariables['QT_QPA_PLATFORM'] };
                startedUtc=[DateTime]::UtcNow.ToString('o'); pid=$null; exitCode=$null; visibleWindowObserved=$false;
                runtimePathsVerified=$false; uiObserved=$false; logs=@(); error=$null }
            $report.runs+=,$run
            $active=[Diagnostics.Process]::new(); $active.StartInfo=$info; $readers=@(); $runFinished=$false
            try {
                if (-not $active.Start()) { throw 'Preview process did not start.' }
                $run.pid=$active.Id; $run['processHandle']=('0x{0:x}' -f $active.Handle.ToInt64())
                $run['actualProcessStartUtc']=$active.StartTime.ToUniversalTime().ToString('o')
                $readers+=New-PreviewLogReader $active.StandardOutput.BaseStream ($prefix+'.stdout.log')
                $readers+=New-PreviewLogReader $active.StandardError.BaseStream ($prefix+'.stderr.log')
                Write-Output ("Opened {0} {1}/2, PID {2}. Operate normally, explicitly save, then close the main window." -f $entry,$ordinal,$active.Id)
                if ($entry -eq 'business' -and $ordinal -eq 1) {
                    Write-Output 'Use module management: Counter B -> Reporter B -> Counter A -> Reporter A -> Notebook B; save accepted configuration.'
                    Write-Output ('Candidate declarations are under '+$PackageRoot+'\native-upgrade and \qt-upgrade.')
                }
                $nextObservation=0.0; $lastUi=$null; $lastModules=$null
                while (-not $active.HasExited) {
                    foreach ($reader in $readers) { Receive-PreviewLog $reader }
                    if ($clock.Elapsed.TotalSeconds -ge $TimeoutSeconds) { throw 'Total recording timeout; the product is left running.' }
                    if ($clock.Elapsed.TotalSeconds -ge $nextObservation) {
                        $observation=Get-PreviewObservation -ProcessId $active.Id
                        $paths=Get-DesktopPathEvidence $observation $manifest $storePaths
                        if ($observation.windows.Count) { $run.visibleWindowObserved=$true }
                        if ($paths.verified) { $run.runtimePathsVerified=$true }
                        $moduleKey=$observation.modules | ConvertTo-Json -Depth 5 -Compress
                        if ($moduleKey -cne $lastModules) { Save-DesktopObservation ('run-'+$index+'-windows') @{ observation=$observation; paths=$paths }; $lastModules=$moduleKey }
                        if ($paths.wrong.Count) { throw 'A required runtime was observed outside its packaged location.' }
                        if (-not $active.HasExited) {
                            $budget=[int][Math]::Min(2500,[Math]::Max(250,($TimeoutSeconds-$clock.Elapsed.TotalSeconds)*1000))
                            $ui=Get-PreviewUiSnapshot -ProcessId $active.Id -Entry $entry -BudgetMilliseconds $budget
                            if ($ui.rootCount -gt 0 -and $ui.elements.Count -gt 0) { $run.uiObserved=$true }
                            $meaningful=@($ui.elements | Where-Object {
                                $_.id -match 'Cordis(Config|Upgrade|Notebook|CommandResult|WorkbenchOutput|CurrentPage)' -or
                                ($_.id -eq '' -and $_.type -eq 'ControlType.Edit') -or $_.name -ceq $configurationFile
                            } | Select-Object id,parentIndex,name,value,helpText)
                            $uiKey=@{ rows=$ui.moduleRows; values=$meaningful; errors=$ui.errors } | ConvertTo-Json -Depth 8 -Compress
                            if ($uiKey -cne $lastUi) { Save-DesktopObservation ('run-'+$index+'-ui') $ui; $lastUi=$uiKey }
                        }
                        Write-DesktopJson (Join-Path $DataRoot 'status.json') @{ phase='user-operating'; entry=$entry; ordinal=$ordinal;
                            pid=$active.Id; startedUtc=$run.startedUtc; command=$run.command; recordingOnly=$true } -Replace
                        $nextObservation=$clock.Elapsed.TotalSeconds+2
                    }
                    Start-Sleep -Milliseconds 100
                }
                $active.WaitForExit(); $run.exitCode=$active.ExitCode; $run['exitedUtc']=[DateTime]::UtcNow.ToString('o')
                $drain=[Diagnostics.Stopwatch]::StartNew()
                while (@($readers | Where-Object { -not $_.eof }).Count -gt 0 -and $drain.Elapsed.TotalSeconds -lt 2) {
                    foreach ($reader in $readers) { Receive-PreviewLog $reader }; Start-Sleep -Milliseconds 10
                }
                $runFinished=$true
            } catch { $run.error=$_.Exception.Message; throw }
            finally {
                $bodyError=$run.error; $finalizationErrors=[Collections.Generic.List[string]]::new()
                try {
                    foreach ($reader in $readers) {
                        try { $reader.File.Dispose() } catch { $finalizationErrors.Add('log close: '+$_.Exception.Message) }
                        $run.logs+=,@{ bytesObserved=$reader.seen; bytesSaved=$reader.saved; truncated=$reader.truncated; eof=$reader.eof }
                    }
                    try {
                        if (-not $runFinished -and $null -ne $run.pid -and -not $active.HasExited) {
                            $report.liveProcess=@{ pid=$active.Id; processHandle=$run.processHandle; confirmedLiveAtUtc=[DateTime]::UtcNow.ToString('o'); noTerminationAttempted=$true }
                        }
                    } catch { $finalizationErrors.Add('live process query: '+$_.Exception.Message) }
                } finally {
                    try { $active.Dispose() } catch { $finalizationErrors.Add('process dispose: '+$_.Exception.Message) }
                    finally { $active=$null }
                }
                if ($finalizationErrors.Count) {
                    $run['finalizationErrors']=@($finalizationErrors.ToArray())
                    if (-not $bodyError) { $run.error='Run finalization failed: '+$finalizationErrors[0] }
                }
                try { Write-DesktopJson ($prefix+'.json') $run }
                catch {
                    $finalizationErrors.Add('run report: '+$_.Exception.Message)
                    $run['finalizationErrors']=@($finalizationErrors.ToArray())
                    if (-not $bodyError -and -not $run.error) { $run.error='Run finalization failed: '+$finalizationErrors[0] }
                }
                if ($finalizationErrors.Count) {
                    foreach ($secondary in $finalizationErrors) { [Console]::Error.WriteLine('Run finalization also failed: '+$secondary) }
                    if (-not $bodyError) { throw $run.error }
                }
            }
            Write-Output ("Exited PID {0}, code {1}." -f $run.pid,$run.exitCode)
            $after=Get-PreviewTreeSnapshot $PackageRoot
            Write-DesktopJson ($prefix+'-after.json') $after
            if ($run.exitCode -ne 0) { throw 'A preview process exited nonzero; no restart attempted.' }
            if (($after | ConvertTo-Json -Depth 5 -Compress) -cne ($frozen | ConvertTo-Json -Depth 5 -Compress)) { throw 'Package files changed during operation.' }
            $file=if ($entry -eq 'configuration') { $configurationFile } else { $businessFile }
            $saved=Copy-DesktopConfiguration $file ($prefix+'-configuration.yaml')
            Write-DesktopJson ($prefix+'-saved.json') $saved.snapshot
            if ($entry -eq 'business') {
                foreach ($alias in @('counter','reporter','notebook')) {
                    if (-not $saved.value.PSObject.Properties['modules'] -or -not $saved.value.modules.PSObject.Properties[$alias] -or
                        $saved.value.modules.$alias.manifest -cne $expectedLocators[$alias]) { throw 'Incomplete: saved business configuration is not exact Counter A / Reporter A / Notebook B; no restart attempted.' }
                }
                Write-DesktopJson ($prefix+'-store.json') (Get-PreviewTreeSnapshot $storeRoot)
            } else { Write-DesktopJson ($prefix+'-preferences.json') (Get-PreviewFileSnapshot $hostFacts.preferencesFile) }
            if (-not $run.visibleWindowObserved -or -not $run.runtimePathsVerified -or -not $run.uiObserved -or
                @($run.logs | Where-Object { $_.truncated -or -not $_.eof }).Count -gt 0 -or $script:BudgetExceeded) {
                throw 'Incomplete window/runtime/UI/log observation; raw evidence retained without restarting.'
            }
        }
    }
    $report.recording_complete=$true; $exitCode=0
    Write-Output 'Four preview processes finished; final recording evidence requires recording.json and recorder exit code 0.'
} catch { $report.error=$_.Exception.Message; [Console]::Error.WriteLine('Desktop recording incomplete: '+$report.error) }
finally {
    if ($created) {
        $report.observationBudgetExceeded=$script:BudgetExceeded
        $report['observationFiles']=$script:ObservationCount; $report['observationBytes']=$script:ObservationBytes
        if (-not (Write-DesktopCompletion $report $DataRoot)) { $exitCode=1 }
    }
}
exit $exitCode
