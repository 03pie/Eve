Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'
$script:PreviewUiPath = $PSCommandPath

function Get-PreviewUiSnapshotCore([int]$ProcessId, [string]$Entry) {
    $result = [ordered]@{ pid=$ProcessId; utc=[DateTime]::UtcNow.ToString('o'); rootCount=0;
        roots=@(); elements=@(); moduleRows=@(); errors=@(); truncated=$false; readOnly=$true }
    try {
        $expected = if ($Entry -eq 'configuration') { 'cordis_config_workbench' } else { 'cordis_platform_workbench_demo' }
        $target = [Diagnostics.Process]::GetProcessById($ProcessId)
        if ($target.ProcessName -cne $expected) { throw 'PID does not identify the selected preview entry.' }
        $framework = Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/WPF'
        Add-Type -Path (Join-Path $framework 'UIAutomationTypes.dll')
        Add-Type -Path (Join-Path $framework 'UIAutomationClient.dll')
        $condition = [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ProcessIdProperty, $ProcessId)
        $roots = [Windows.Automation.AutomationElement]::RootElement.FindAll([Windows.Automation.TreeScope]::Children, $condition)
        $walker = [Windows.Automation.TreeWalker]::ControlViewWalker
        $pending = [Collections.Generic.Queue[object]]::new()
        $rootRows = [Collections.Generic.List[object]]::new()
        for ($i=0; $i -lt $roots.Count; ++$i) {
            $current = $roots[$i].Current
            $rootRows.Add([pscustomobject]@{ id=$current.AutomationId; name=$current.Name;
                hwnd=$current.NativeWindowHandle; pid=$current.ProcessId })
            $pending.Enqueue(@{ element=$roots[$i]; depth=0; parentIndex=$null })
        }
        $result.rootCount=$roots.Count; $result.roots=@($rootRows.ToArray())
        $rows = [Collections.Generic.List[object]]::new(); $errors = [Collections.Generic.List[string]]::new()
        while ($pending.Count -gt 0 -and $rows.Count -lt 1200) {
            $item=$pending.Dequeue()
            try {
                $element=$item.element; $current=$element.Current
                if ($current.ProcessId -ne $ProcessId) { continue }
                $rect=$current.BoundingRectangle; $rectangle=$null
                if (-not [double]::IsInfinity($rect.X) -and -not [double]::IsInfinity($rect.Y) -and
                    -not [double]::IsInfinity($rect.Width) -and -not [double]::IsInfinity($rect.Height) -and
                    -not [double]::IsNaN($rect.X) -and -not [double]::IsNaN($rect.Y)) {
                    $rectangle=@{ x=$rect.X; y=$rect.Y; width=$rect.Width; height=$rect.Height }
                }
                $name=$current.Name; $help=$current.HelpText; $value=$null; $valueTruncated=$false
                $pattern=$null
                if ($element.TryGetCurrentPattern([Windows.Automation.ValuePattern]::Pattern, [ref]$pattern)) {
                    $value=[string]$pattern.Current.Value
                } elseif ($current.AutomationId.EndsWith('CordisWorkbenchOutput')) {
                    if ($element.TryGetCurrentPattern([Windows.Automation.TextPattern]::Pattern, [ref]$pattern)) {
                        $value=$pattern.DocumentRange.GetText(-1)
                    }
                }
                if ($null -ne $value -and $value.Length -gt 4096) {
                    $valueTruncated=$true
                    $value = if ($current.AutomationId.EndsWith('CordisWorkbenchOutput')) { $value.Substring($value.Length-4096) } else { $value.Substring(0,4096) }
                }
                $rowIndex=$rows.Count
                $rows.Add([pscustomobject]@{ index=$rowIndex; parentIndex=$item.parentIndex; id=$current.AutomationId;
                    name=$name.Substring(0,[Math]::Min(512,$name.Length)); nameTruncated=($name.Length -gt 512);
                    helpText=$help.Substring(0,[Math]::Min(4096,$help.Length)); helpTextTruncated=($help.Length -gt 4096);
                    type=$current.ControlType.ProgrammaticName; class=$current.ClassName; pid=$current.ProcessId;
                    value=$value; textTruncated=$valueTruncated; enabled=$current.IsEnabled;
                    offscreen=$current.IsOffscreen; focused=$current.HasKeyboardFocus; focusable=$current.IsKeyboardFocusable;
                    rect=$rectangle; patterns=@($element.GetSupportedPatterns() | ForEach-Object { $_.ProgrammaticName }) })
                if ($current.AutomationId.EndsWith('CordisModuleTable')) {
                    $grid=$null
                    if ($element.TryGetCurrentPattern([Windows.Automation.GridPattern]::Pattern, [ref]$grid)) {
                        $gridRows=[Collections.Generic.List[object]]::new()
                        $count=[Math]::Min(32,$grid.Current.RowCount); $columns=[Math]::Min(8,$grid.Current.ColumnCount)
                        if ($count -ne $grid.Current.RowCount -or $columns -ne $grid.Current.ColumnCount) { $result.truncated=$true }
                        for ($row=0; $row -lt $count; ++$row) {
                            $cells=@()
                            for ($column=0; $column -lt $columns; ++$column) { $cells += $grid.GetItem($row,$column).Current.Name }
                            $gridRows.Add($cells)
                        }
                        $result.moduleRows=@($gridRows.ToArray())
                    }
                }
                if ($item.depth -lt 12) {
                    $child=$walker.GetFirstChild($element)
                    while ($null -ne $child -and $pending.Count -lt 1200) {
                        $pending.Enqueue(@{ element=$child; depth=($item.depth+1); parentIndex=$rowIndex }); $child=$walker.GetNextSibling($child)
                    }
                    if ($null -ne $child) { $result.truncated=$true }
                }
            } catch { if ($errors.Count -lt 32) { $errors.Add($_.Exception.Message) } }
        }
        $result.elements=@($rows.ToArray()); $result.errors=@($errors.ToArray())
        $result.truncated=$result.truncated -or $pending.Count -gt 0
    } catch { $result.errors=@($_.Exception.Message) }
    return [pscustomobject]$result
}

function Get-PreviewUiSnapshot {
    [CmdletBinding()]
    param([Parameter(Mandatory=$true)][ValidateRange(1,2147483647)][int]$ProcessId,
          [ValidateSet('configuration','business')][string]$Entry='business',
          [ValidateRange(250,5000)][int]$BudgetMilliseconds=2500)
    # UIA providers may block. Only this newly-created observer may be terminated.
    $path=$script:PreviewUiPath.Replace("'","''")
    $command="[Console]::OutputEncoding=[Text.UTF8Encoding]::new(`$false); Import-Module '$path' -Force; " +
        "& (Get-Module preview-ui) { Get-PreviewUiSnapshotCore -ProcessId $ProcessId -Entry '$Entry' } | ConvertTo-Json -Depth 10 -Compress"
    $encoded=[Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($command))
    $start=[Diagnostics.ProcessStartInfo]::new((Join-Path $PSHOME 'powershell.exe'),'-NoProfile -NonInteractive -STA -EncodedCommand '+$encoded)
    $start.UseShellExecute=$false; $start.CreateNoWindow=$true; $start.RedirectStandardOutput=$true; $start.RedirectStandardError=$true
    $start.StandardOutputEncoding=[Text.Encoding]::UTF8; $start.StandardErrorEncoding=[Text.Encoding]::UTF8
    $worker=[Diagnostics.Process]::new(); $worker.StartInfo=$start
    $failure=$null; $helperPid=$null; $result=$null
    try {
        if (-not $worker.Start()) { throw 'Cannot start the owned UIA observer.' }
        $helperPid=$worker.Id; $output=$worker.StandardOutput.ReadToEndAsync(); $errorOutput=$worker.StandardError.ReadToEndAsync()
        if (-not $worker.WaitForExit($BudgetMilliseconds)) { throw 'Owned UIA observer exceeded its time budget.' }
        if (-not $output.Wait(500) -or -not $errorOutput.Wait(500)) { throw 'Owned UIA observer pipe did not finish.' }
        $raw=$output.Result
        if ($worker.ExitCode -ne 0 -or $raw.Length -gt 4194304) { throw 'Owned UIA observer failed or exceeded its output budget.' }
        $result=ConvertFrom-Json -InputObject $raw -ErrorAction Stop
        if ($result.pid -ne $ProcessId -or -not $result.readOnly) { throw 'Owned UIA observer returned mismatched evidence.' }
    } catch { $failure=$_.Exception.Message }
    finally {
        if ($null -ne $helperPid -and -not $worker.HasExited) {
            $worker.Kill() # The worker was created above; never look up or kill ProcessId.
            [void]$worker.WaitForExit(1000)
        }
        $worker.Dispose()
    }
    if ($failure) {
        return [pscustomobject]@{ pid=$ProcessId; utc=[DateTime]::UtcNow.ToString('o'); rootCount=0;
            roots=@(); elements=@(); moduleRows=@(); errors=@($failure); truncated=$false; readOnly=$true; observerPid=$helperPid }
    }
    $result | Add-Member -NotePropertyName observerPid -NotePropertyValue $helperPid
    return $result
}

Export-ModuleMember -Function Get-PreviewUiSnapshot
