param([Parameter(Mandatory=$true)][string]$Request,
      [Parameter(Mandatory=$true)][string]$Output)
Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

function Get-PreviewBoundProcess($Expected) {
    if ($null -eq $Expected -or (-not ($Expected.pid -is [int] -or $Expected.pid -is [long])) -or
        $Expected.pid -le 0 -or $Expected.pid -gt [int]::MaxValue -or
        $Expected.creationFileTime -isnot [string] -or $Expected.creationFileTime -cnotmatch '^[0-9]{1,19}$' -or
        $Expected.path -isnot [string] -or -not [IO.Path]::IsPathRooted($Expected.path) -or
        $Expected.ownerSid -isnot [string] -or $Expected.ownerSid -cnotmatch '^S-1-5-([0-9]+-)*[0-9]+$' -or
        (-not ($Expected.sessionId -is [int] -or $Expected.sessionId -is [long])) -or $Expected.sessionId -lt 0 -or
        (-not ($Expected.parentPid -is [int] -or $Expected.parentPid -is [long])) -or $Expected.parentPid -lt 0) {
        throw 'Incomplete expected live process identity.'
    }
    if (-not ([IO.Path]::GetFullPath($Expected.path)).Equals($Expected.path, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Expected process path must be fully absolute and canonical.'
    }
    $process = $null
    try {
        $process = [Diagnostics.Process]::GetProcessById([int]$Expected.pid)
        [void]$process.Handle
        $process.Refresh()
        if ($process.HasExited) { throw 'Bound process has already exited.' }
        $created = $process.StartTime.ToUniversalTime().ToFileTimeUtc().ToString([Globalization.CultureInfo]::InvariantCulture)
        $row = Get-CimInstance -ClassName Win32_Process -Filter ('ProcessId = ' + [int]$Expected.pid) -ErrorAction Stop
        if ($null -eq $row) { throw 'Actual process metadata is unavailable.' }
        $owner = Invoke-CimMethod -InputObject $row -MethodName GetOwnerSid -ErrorAction Stop
        if ($owner.ReturnValue -ne 0 -or -not $row.ExecutablePath) { throw 'Actual process path/owner is unavailable.' }
        $actual = [ordered]@{
            pid = $process.Id; creationFileTime = $created
            path = [IO.Path]::GetFullPath([string]$row.ExecutablePath)
            sessionId = $process.SessionId; ownerSid = [string]$owner.Sid
            parentPid = [int]$row.ParentProcessId
        }
        if ($created -cne $Expected.creationFileTime -or
            -not $actual.path.Equals([IO.Path]::GetFullPath($Expected.path), [StringComparison]::OrdinalIgnoreCase) -or
            $actual.sessionId -ne $Expected.sessionId -or $actual.ownerSid -cne $Expected.ownerSid -or
            $actual.parentPid -ne $Expected.parentPid) { throw 'Actual live process identity differs from the anchored descriptor.' }
        $process.Refresh()
        if ($process.HasExited) { throw 'Bound process exited during identity observation.' }
        [pscustomobject]@{ process = $process; facts = [pscustomobject]$actual }
    } catch {
        if ($null -ne $process) { $process.Dispose() }
        throw
    }
}

function Assert-PreviewBoundAlive($Bindings) {
    foreach ($binding in @($Bindings)) {
        $binding.process.Refresh()
        if ($binding.process.HasExited -or
            $binding.process.StartTime.ToUniversalTime().ToFileTimeUtc().ToString([Globalization.CultureInfo]::InvariantCulture) -cne
                $binding.facts.creationFileTime) { throw 'An anchored process exited before the UI operation.' }
    }
}

# The caller owns this helper, never the target process. Reserve evidence before acting.
$stream = [IO.FileStream]::new($Output,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
$record = [ordered]@{schema='cordis.preview.ui.action/1';utc=[DateTime]::UtcNow.ToString('o');ok=$false}
$failed = $false
$bindings = @()
$legacyProcess = $null
try {
    $requestValue = ConvertFrom-Json -InputObject ([IO.File]::ReadAllText($Request,[Text.Encoding]::UTF8))
    if ($null -ne $requestValue.PSObject.Properties['expectedIdentity'] -and
        ((-not ($requestValue.pid -is [int] -or $requestValue.pid -is [long])) -or
         $requestValue.pid -le 0 -or $requestValue.pid -gt [int]::MaxValue)) {
        throw 'Anchored request PID must be a positive integer.'
    }
    $targetId = [int]$requestValue.pid
    if ($targetId -le 0 -or $requestValue.entry -notin @('business','configuration')) { throw 'Invalid target.' }
    $expected = if ($requestValue.entry -eq 'configuration') {'cordis_config_workbench'} else {'cordis_platform_workbench_demo'}
    if ($null -ne $requestValue.PSObject.Properties['expectedIdentity']) {
        $identity = $requestValue.expectedIdentity
        if ($null -eq $identity -or $identity.target.pid -ne $targetId -or
            $identity.target.parentPid -ne $identity.wrapper.pid -or
            $identity.target.sessionId -ne $identity.wrapper.sessionId -or
            $identity.target.ownerSid -cne $identity.wrapper.ownerSid -or
            $identity.target.pid -eq $identity.wrapper.pid) { throw 'Invalid anchored target/wrapper relationship.' }
        $targetBinding = Get-PreviewBoundProcess $identity.target
        $bindings += $targetBinding
        $wrapperBinding = Get-PreviewBoundProcess $identity.wrapper
        $bindings += $wrapperBinding
        if ($targetBinding.process.ProcessName -cne $expected) { throw 'Entry/PID mismatch.' }
        $record.actualIdentity = [ordered]@{target=$targetBinding.facts;wrapper=$wrapperBinding.facts}
        if ((-not ($requestValue.callerPid -is [int] -or $requestValue.callerPid -is [long])) -or
            $requestValue.callerPid -le 0 -or $requestValue.callerPid -eq $targetId -or
            $requestValue.callerPid -eq $identity.wrapper.pid) { throw 'A distinct actual UI caller is required.' }
        $caller = [Diagnostics.Process]::GetProcessById([int]$requestValue.callerPid)
        # Pin the original caller before any further metadata or UI operation.
        $callerBinding = [pscustomobject]@{process=$caller;facts=$null}
        $bindings += $callerBinding
        [void]$caller.Handle
        $callerRow = Get-CimInstance -ClassName Win32_Process -Filter ('ProcessId = ' + [int]$requestValue.callerPid) -ErrorAction Stop
        $callerOwner = Invoke-CimMethod -InputObject $callerRow -MethodName GetOwnerSid -ErrorAction Stop
        if ($callerOwner.ReturnValue -ne 0 -or -not $callerRow.ExecutablePath -or
            $callerRow.ParentProcessId -ne $identity.wrapper.pid -or
            $callerOwner.Sid -cne $identity.wrapper.ownerSid -or
            $caller.SessionId -ne $identity.wrapper.sessionId -or
            -not $caller.ProcessName.Equals('python', [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Actual UI caller is not the anchored wrapper ordinary Python child.'
        }
        $callerBinding.facts = [pscustomobject]@{
            pid=$caller.Id; creationFileTime=$caller.StartTime.ToUniversalTime().ToFileTimeUtc().ToString([Globalization.CultureInfo]::InvariantCulture)
            path=[IO.Path]::GetFullPath([string]$callerRow.ExecutablePath)
            sessionId=$caller.SessionId; ownerSid=[string]$callerOwner.Sid; parentPid=[int]$callerRow.ParentProcessId
        }
        $actorIdentity = [Security.Principal.WindowsIdentity]::GetCurrent()
        $actorProcess = [Diagnostics.Process]::GetCurrentProcess()
        try {
            $actorRow = Get-CimInstance -ClassName Win32_Process -Filter ('ProcessId = ' + $PID) -ErrorAction Stop
            $actorAdministrator = [Security.Principal.WindowsPrincipal]::new($actorIdentity).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
            $groupLines = @(& "$env:WINDIR\System32\whoami.exe" /groups /fo csv /nh)
            $groupExit = $LASTEXITCODE
            $integrityRids = @([regex]::Matches(($groupLines -join "`n"), 'S-1-16-([0-9]+)') |
                ForEach-Object {[int]$_.Groups[1].Value} | Select-Object -Unique)
            $record.actor = [ordered]@{
                pid=$PID; parentPid=[int]$actorRow.ParentProcessId; ownerSid=$actorIdentity.User.Value
                sessionId=$actorProcess.SessionId; administrator=$actorAdministrator; integrityRids=$integrityRids
                interactive=[Environment]::UserInteractive; is64Bit=[Environment]::Is64BitProcess; caller=$callerBinding.facts
            }
            if ($groupExit -ne 0 -or $integrityRids.Count -ne 1 -or $integrityRids[0] -ne 8192 -or
                $actorAdministrator -or $actorIdentity.User.Value -cne $identity.wrapper.ownerSid -or
                $actorProcess.SessionId -ne $identity.wrapper.sessionId -or $actorRow.ParentProcessId -ne $requestValue.callerPid -or
                -not [Environment]::UserInteractive -or -not [Environment]::Is64BitProcess) {
                throw 'UI action actor is not the anchored ordinary interactive QA token.'
            }
        } finally { $actorProcess.Dispose(); $actorIdentity.Dispose() }
        Assert-PreviewBoundAlive $bindings
        $record.identityVerified = $true
    } else {
        $legacyProcess = [Diagnostics.Process]::GetProcessById($targetId)
        if ($legacyProcess.ProcessName -cne $expected) { throw 'Entry/PID mismatch.' }
    }
    $record.pid=$targetId; $record.entry=$requestValue.entry; $record.action=$requestValue.action
    if ($requestValue.action -in @('Read','Grid')) {
        Import-Module (Join-Path $PSScriptRoot '../delivery/windows/verification/preview-ui.psm1') -Force
        $snapshot = Get-PreviewUiSnapshot -ProcessId $targetId -Entry $requestValue.entry
        $record.snapshot=$snapshot
        if (@($snapshot.errors).Count -gt 0 -or $snapshot.truncated) { throw 'UI snapshot is incomplete; see raw snapshot.' }
        Assert-PreviewBoundAlive $bindings
    } else {
        if ($requestValue.action -notin @('Invoke','SetValue','Expand','Close')) { throw 'Unsupported action.' }
        $record.selector=$requestValue.selector
        $framework=Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/WPF'
        Add-Type -Path (Join-Path $framework 'UIAutomationTypes.dll')
        Add-Type -Path (Join-Path $framework 'UIAutomationClient.dll')
        $condition=[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ProcessIdProperty,$targetId)
        $roots=[Windows.Automation.AutomationElement]::RootElement.FindAll([Windows.Automation.TreeScope]::Children,$condition)
        $found=[Collections.Generic.List[object]]::new()
        $selector=$requestValue.selector
        if ($selector.scope) {
            $scopes=[Collections.Generic.List[object]]::new()
            foreach ($root in $roots) {
                foreach ($element in $root.FindAll([Windows.Automation.TreeScope]::Subtree,[Windows.Automation.Condition]::TrueCondition)) {
                    $current=$element.Current; $scopeId=[string]$current.AutomationId
                    if ($current.ProcessId -eq $targetId -and -not $current.IsOffscreen -and
                        ($scopeId.Equals([string]$selector.scope,[StringComparison]::Ordinal) -or
                         $scopeId.EndsWith('.'+[string]$selector.scope,[StringComparison]::Ordinal))) { $scopes.Add($element) }
                }
            }
            if ($scopes.Count -ne 1) { throw 'Expected one live action scope.' }
            $roots=@($scopes[0]); $record.scope=$selector.scope
        }
        if (-not $selector.id -and -not $selector.name -and
            -not ($selector.scope -and $selector.emptyId -and $selector.type -and $selector.matchValue)) {
            throw 'An explicit UI selector is required.'
        }
        $visited=0
        foreach ($root in $roots) {
            $elements=$root.FindAll([Windows.Automation.TreeScope]::Subtree,[Windows.Automation.Condition]::TrueCondition)
            foreach ($element in $elements) {
                if (++$visited -gt 2400) { throw 'Action lookup exceeded its node budget.' }
                $current=$element.Current
                if ($current.ProcessId -ne $targetId) { continue }
                $id=[string]$current.AutomationId
                $idMatches=((-not $selector.emptyId -or $id.Length -eq 0) -and
                    (-not $selector.id -or $id.Equals([string]$selector.id,[StringComparison]::Ordinal) -or
                    $id.EndsWith('.'+[string]$selector.id,[StringComparison]::Ordinal))
                )
                $nameMatches=(-not $selector.name -or $current.Name.Equals([string]$selector.name,[StringComparison]::Ordinal))
                $typeMatches=(-not $selector.type -or $current.ControlType.ProgrammaticName -ceq $selector.type)
                $valueMatches=$true
                if ($selector.matchValue) {
                    $pattern=$null
                    $valueMatches=$element.TryGetCurrentPattern([Windows.Automation.ValuePattern]::Pattern,[ref]$pattern) -and
                        ([string]$pattern.Current.Value).Equals([string]$selector.expectedValue,[StringComparison]::Ordinal)
                }
                if ($idMatches -and $nameMatches -and $typeMatches -and $valueMatches -and -not $current.IsOffscreen) { $found.Add($element) }
            }
        }
        if ($found.Count -ne 1) { throw ('Expected one visible live UI control; found '+$found.Count) }
        $selected=$found[0]; $current=$selected.Current
        if (-not $current.IsEnabled) { throw 'Selected UI control is disabled.' }
        $record.actual=[ordered]@{id=$current.AutomationId;name=$current.Name;pid=$current.ProcessId;type=$current.ControlType.ProgrammaticName}
        Assert-PreviewBoundAlive $bindings
        # Each reference was obtained in this invocation and is discarded on return.
        switch ($requestValue.action) {
            'Invoke' { ([Windows.Automation.InvokePattern]$selected.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern)).Invoke() }
            'SetValue' {
                $record.value=[string]$requestValue.value
                $valuePattern=[Windows.Automation.ValuePattern]$selected.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern)
                if ($selector.matchValue -and -not ([string]$valuePattern.Current.Value).Equals([string]$selector.expectedValue,[StringComparison]::Ordinal)) {
                    throw 'Editor changed after lookup; no write was attempted.'
                }
                $valuePattern.SetValue([string]$requestValue.value)
            }
            'Expand' { ([Windows.Automation.ExpandCollapsePattern]$selected.GetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern)).Expand() }
            'Close' { ([Windows.Automation.WindowPattern]$selected.GetCurrentPattern([Windows.Automation.WindowPattern]::Pattern)).Close() }
        }
    }
    $record.ok=$true
} catch { $record.error=$_.Exception.Message; $failed=$true }
finally {
    try {
        $bytes=[Text.UTF8Encoding]::new($false).GetBytes(($record | ConvertTo-Json -Depth 14))
        $stream.Write($bytes,0,$bytes.Length)
    } finally {
        $stream.Dispose()
        foreach ($binding in $bindings) { $binding.process.Dispose() }
        if ($null -ne $legacyProcess) { $legacyProcess.Dispose() }
    }
}
if ($failed) { exit 1 }
