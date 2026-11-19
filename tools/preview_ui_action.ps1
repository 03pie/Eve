param([Parameter(Mandatory=$true)][string]$Request,
      [Parameter(Mandatory=$true)][string]$Output)
Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'
# The caller owns this helper, never the target process. Reserve evidence before acting.
$stream = [IO.FileStream]::new($Output,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
$record = [ordered]@{schema='cordis.preview.ui.action/1';utc=[DateTime]::UtcNow.ToString('o');ok=$false}
$failed = $false
try {
    $requestValue = ConvertFrom-Json -InputObject ([IO.File]::ReadAllText($Request,[Text.Encoding]::UTF8))
    $targetId = [int]$requestValue.pid
    if ($targetId -le 0 -or $requestValue.entry -notin @('business','configuration')) { throw 'Invalid target.' }
    $expected = if ($requestValue.entry -eq 'configuration') {'cordis_config_workbench'} else {'cordis_platform_workbench_demo'}
    if ([Diagnostics.Process]::GetProcessById($targetId).ProcessName -cne $expected) { throw 'Entry/PID mismatch.' }
    $record.pid=$targetId; $record.entry=$requestValue.entry; $record.action=$requestValue.action
    if ($requestValue.action -in @('Read','Grid')) {
        Import-Module (Join-Path $PSScriptRoot '../delivery/windows/verification/preview-ui.psm1') -Force
        $snapshot = Get-PreviewUiSnapshot -ProcessId $targetId -Entry $requestValue.entry
        $record.snapshot=$snapshot
        if (@($snapshot.errors).Count -gt 0 -or $snapshot.truncated) { throw 'UI snapshot is incomplete; see raw snapshot.' }
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
    } finally { $stream.Dispose() }
}
if ($failed) { exit 1 }
