<#
.SYNOPSIS
Own one ordinary-QA configuration process for the seven-phase D10 Save test.
.DESCRIPTION
No product or ACL writer is terminated. An unknown phase writes owner-aborted,
restores only registered data when safe, and retains the real process handles
and bounded log readers until their original processes naturally finish.
#>
#Requires -Version 5.1
[CmdletBinding()]
param(
[Parameter(Mandatory=$true)][string]$SourcePackage,
[Parameter(Mandatory=$true)][ValidatePattern('^[0-9a-fA-F]{64}$')][string]$ExpectedManifestSha256,
[Parameter(Mandatory=$true)][string]$ScopeRoot,
[Parameter(Mandatory=$true)][string]$UiToolRoot,
[Parameter(Mandatory=$true)][string]$PythonExe,
[Parameter(Mandatory=$true)][string]$ComparisonSource,
[Parameter(Mandatory=$true)][ValidatePattern('^[0-9a-fA-F]{64}$')][string]$ExpectedToolsManifestSha256,
[Parameter(Mandatory=$true)][string]$Prerequisite,
[Parameter(Mandatory=$true)][ValidatePattern('^[0-9a-fA-F]{64}$')][string]$ExpectedPrerequisiteSha256
)
Set-StrictMode -Version 2.0
$ErrorActionPreference='Stop'
[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)
$script:children=[Collections.Generic.List[object]]::new()
$script:product=$null;
$script:driver=$null;
$script:writer=$null
$script:responsible=$false;
$script:restoreAttempted=$false;
$script:restored=$false
$script:created=$false;
$script:registrationHash=$null;
$script:nonce=[guid]::NewGuid().ToString('N')
$script:phaseNames=@('owner-ready',
'ui-applied',
'owner-protected',
'ui-save-failed',
'owner-restored',
'ui-saved',
'ui-complete')
$script:phasePaths=@();
$script:phaseHashes=@{
};
$script:phases=@();
$script:dataStateHash=$null
$report=[ordered]@{
    schema='cordis.preview.readonly-save.recording/1';
    startedUtc=[DateTime]::UtcNow.ToString('o');
    recording_complete=$false;
    error=$null;
    liveProduct=$null;
    restoreResponsibilityUnresolved=$false;
    productExitCode=$null;
    uiExitCode=$null;
}
function Assert-D10Path([string]$Path,
[switch]$New){
    if(-not [IO.Path]::IsPathRooted($Path) -or
       $Path -notmatch '^(?:[A-Za-z]:[\\/]|\\\\[^\\/?]+\\[^\\/?]+(?:\\|$))' -or
       $Path.StartsWith('\\?\') -or $Path.StartsWith('\\.\')){
        throw 'An absolute D10 path is required.'
    }
    $full=[IO.Path]::GetFullPath($Path).TrimEnd('\')
    if($full -ieq [IO.Path]::GetPathRoot([IO.Path]::GetFullPath($Path)).TrimEnd('\')){
        throw 'A volume root is not a D10 input or output scope.'
    }
    if($New -and
    (Test-Path -LiteralPath $full)){
        throw 'D10 output scope already exists.'
    }
    $cursor=$full
    while($cursor){
        if(Test-Path -LiteralPath $cursor){
            if((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){
                throw 'D10 reparse path refused.'
            }
        };
        $parent=[IO.Path]::GetDirectoryName($cursor);
        if($parent -eq $cursor){
            break
        };
        $cursor=$parent
    }
    return $full
}
function Test-D10Integer($Value){
    return ($Value -is [int] -or $Value -is [long])
}
function Assert-D10Prerequisite($Value){
    if($Value.schema -cne 'cordis.preview.readonly-save.prerequisite/1' -or
       $Value.hostExportVerified -isnot [bool] -or -not $Value.hostExportVerified -or
       $Value.humanWrapperExitObserved -isnot [bool] -or -not $Value.humanWrapperExitObserved -or
       -not (Test-D10Integer $Value.humanWrapperExitCode) -or $Value.humanWrapperExitCode -ne 0 -or
       -not (Test-D10Integer $Value.humanWrapperPid) -or $Value.humanWrapperPid -le 0 -or
       -not (Test-D10Integer $Value.humanProductPid) -or $Value.humanProductPid -le 0 -or
       -not (Test-D10Integer $Value.sessionId) -or $Value.sessionId -le 0 -or
       $Value.qaSid -isnot [string] -or $Value.qaSid -notmatch '^S-[0-9]+(?:-[0-9]+)+$' -or
       $Value.humanReviewRoot -isnot [string]){
        throw 'Human normal-exit prerequisite has not been established with exact field types.'
    }
    foreach($hash in @($Value.hostExportReceiptSha256,$Value.humanSessionSummarySha256,$Value.humanProductExitSha256)){
        if($hash -isnot [string] -or $hash -cnotmatch '^[0-9a-f]{64}$'){
            throw 'Human prerequisite hash has an invalid type or value.'
        }
    }
}
function Assert-D10HumanExit($Summary,$Exit,$PrerequisiteValue){
    if($Summary.schema -cne 'cordis.p6.human-review-session/1' -or $null -ne $Summary.error -or
       $Summary.processExitObserved -isnot [bool] -or -not $Summary.processExitObserved -or
       $Summary.packageFilesUnchanged -isnot [bool] -or -not $Summary.packageFilesUnchanged -or
       -not (Test-D10Integer $Summary.productExitCode) -or $Summary.productExitCode -ne 0 -or
       -not (Test-D10Integer $Summary.productPid) -or $Summary.productPid -ne $PrerequisiteValue.humanProductPid -or
       $null -ne $Summary.liveProductPid -or -not (Test-D10Integer $Exit.pid) -or
       $Exit.pid -ne $PrerequisiteValue.humanProductPid -or -not (Test-D10Integer $Exit.exitCode) -or
       $Exit.exitCode -ne 0 -or $Exit.processHandleObserved -isnot [bool] -or -not $Exit.processHandleObserved -or
       $Exit.forcedCloseOrKillPerformed -isnot [bool] -or $Exit.forcedCloseOrKillPerformed){
        throw 'Human process did not normally finish with exact typed exit evidence.'
    }
}
function Assert-D10Relative([string]$Path){
    if(-not $Path -or
    $Path.StartsWith('/') -or
    $Path -match '[\\:<>"|?*\x00-\x1f]'){
        throw 'Unsafe relative D10 tool path.'
    }
    foreach($part in $Path.Split('/')){
        if(-not $part -or
        $part -in @('.',
        '..') -or
        $part.EndsWith('.') -or
        $part.EndsWith(' ') -or
        $part -match '^(?i:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$)'){
            throw 'Unsafe relative D10 tool component.'
        }
    }
    return $Path
}
function Test-D10Inside([string]$Path,
[string]$Parent){
    return $Path.StartsWith($Parent.TrimEnd('\')+'\',
    [StringComparison]::OrdinalIgnoreCase)
}
function Assert-D10SourceSnapshot($Snapshot,$Manifest){
    if($Manifest.schema -cne 'cordis.preview.package/1' -or
       @($Manifest.files).Count -ne 137 -or $Snapshot.Count -ne 139){
        throw 'D10 source package schema/file count differs.'
    }
    $paths=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach($entry in $Manifest.files){
        $relative=Assert-D10Relative $entry.path
        if(-not $paths.Add($relative) -or -not $Snapshot.ContainsKey($relative) -or
           $entry.sha256 -cnotmatch '^[0-9a-f]{64}$' -or $entry.bytes -lt 0 -or
           $Snapshot[$relative].bytes -ne $entry.bytes -or
           $Snapshot[$relative].sha256 -cne $entry.sha256){
            throw 'D10 source payload differs from its externally anchored manifest.'
        }
    }
    foreach($name in @('build-manifest.json','SHA256SUMS.txt')){
        if(-not $paths.Add($name) -or -not $Snapshot.ContainsKey($name)){
            throw 'D10 source manifest/checksum inventory differs.'
        }
    }
    if($paths.Count -ne $Snapshot.Count){
        throw 'D10 source contains an unexpected file.'
    }
}
function Get-D10Sha([string]$Path){
    [void](Assert-D10Path $Path);
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}
function Get-D10Json([string]$Path){
    [void](Assert-D10Path $Path);
    if(-not [IO.File]::Exists($Path) -or
    (Get-Item -LiteralPath $Path).Length -gt 8MB){
        throw 'Missing/excessive D10 JSON.'
    };
    return ([IO.File]::ReadAllText($Path,
    [Text.UTF8Encoding]::new($false,
    $true))|ConvertFrom-Json)
}
function Write-D10Json([string]$Path,
$Value){
    $bytes=[Text.UTF8Encoding]::new($false).GetBytes(($Value|ConvertTo-Json -Depth 24)+"`n");
    $stream=[IO.File]::Open($Path,
    [IO.FileMode]::CreateNew,
    [IO.FileAccess]::Write,
    [IO.FileShare]::Read);
    try{
        $stream.Write($bytes,
        0,
        $bytes.Length)
    }finally{
        $stream.Dispose()
    }
}
function Get-D10Ast([string]$Path,
[string]$Hash){
    [void](Assert-D10Path $Path);
    $bytes=[IO.File]::ReadAllBytes($Path);
    $algorithm=[Security.Cryptography.SHA256]::Create()
    try{
        $actual=[BitConverter]::ToString($algorithm.ComputeHash($bytes)).Replace('-',
        '').ToLowerInvariant()
    }finally{
        $algorithm.Dispose()
    }
    if($actual -cne $Hash){
        throw 'D10 executable source bytes changed.'
    }
    $tokens=$null;
    $errors=$null;
    $ast=[Management.Automation.Language.Parser]::ParseInput([Text.UTF8Encoding]::new($false,
    $true).GetString($bytes).TrimStart([char]0xFEFF),
    [ref]$tokens,
    [ref]$errors)
    if($errors.Count){
        throw 'D10 executable source parser failed.'
    };
    return $ast
}
function Import-D10Function($Ast,
[string]$Name){
    $functionName=$Name;
    $node=$Ast.Find({
        param($entry)$entry -is [Management.Automation.Language.FunctionDefinitionAst] -and
        $entry.Name -ceq $functionName
    },
    $true);
    if($null -eq $node){
        throw 'Required reviewed function is absent.'
    };
    return $node.Extent.Text
}
function Assert-D10Value($Value,
$Expected){
    if(@($Value.PSObject.Properties).Count -ne 2 -or
    -not (Test-D10Integer $Value.formatVersion) -or $Value.formatVersion -ne 1 -or
    @($Value.plugins).Count -ne 1){
        throw 'Unexpected D10 configuration schema.'
    }
    $plugin=@($Value.plugins)[0]
    if(@($plugin.PSObject.Properties).Count -ne 3 -or
    $plugin.id -isnot [string] -or $plugin.plugin -isnot [string] -or
    $plugin.id -cne 'notes' -or
    $plugin.plugin -cne 'demo.notes' -or
    @($plugin.config.PSObject.Properties).Count -ne 1 -or
    $plugin.config.title -isnot [string] -or $plugin.config.title -cne $Expected.plugins[0].config.title){
        throw 'D10 configuration value differs.'
    }
}
function Drain-D10Children{
    foreach($child in $script:children){
        if(-not $child.finished){
            foreach($reader in $child.readers){
                Receive-PreviewLog $reader
            }
        }
    }
}
function Test-D10Live($Child){
    return ($null -ne $Child -and
    $Child.record.started -and
    -not $Child.process.HasExited)
}
function Get-D10Descriptor([Diagnostics.Process]$Process){
    $native=Get-CimInstance Win32_Process -Filter ('ProcessId='+$Process.Id)
    if($null -eq $native){
        throw 'Cannot establish actual held process identity.'
    }
    $owner=Invoke-CimMethod -InputObject $native -MethodName GetOwnerSid
    if($owner.ReturnValue -ne 0){
        throw 'Cannot establish actual process owner SID.'
    }
    return [ordered]@{
        pid=$Process.Id;
        creationFileTime=$Process.StartTime.ToUniversalTime().ToFileTimeUtc().ToString([Globalization.CultureInfo]::InvariantCulture);
        path=$Process.MainModule.FileName;
        sessionId=$Process.SessionId;
        ownerSid=$owner.Sid;
        parentPid=[int]$native.ParentProcessId
    }
}
function Start-D10Child([string]$Label,
[string]$Executable,
[string[]]$Arguments){
    $info=New-PreviewProcessStartInfo -Executable $Executable -Arguments $Arguments -WorkingDirectory $script:cwd
    $process=[Diagnostics.Process]::new();
    $process.StartInfo=$info
    $record=[ordered]@{
        started=$false;
        command=@($Executable)+$Arguments;
        pid=$null;
        processHandle=$null;
        creationFileTime=$null;
        startedUtc=[DateTime]::UtcNow.ToString('o');
        exitCode=$null;
        timedOut=$false;
        processTerminated=$false;
        stdoutPath=(Join-Path $script:evidence ($Label+'.stdout.log'));
        stderrPath=(Join-Path $script:evidence ($Label+'.stderr.log'))
    }
    $child=[pscustomobject]@{
        label=$Label;
        process=$process;
        readers=@();
        finished=$false;
        record=$record
    }
    $script:children.Add($child)
    if($Label.StartsWith('acl-')){
        $script:writer=$child
    }elseif($Label -ceq 'product'){
        $script:product=$child
    }elseif($Label -ceq 'ui-adapter'){
        $script:driver=$child
    }
    if(-not $process.Start()){
        throw 'Owned D10 child did not start.'
    }
    $record.started=$true;
    $record.pid=$process.Id
    # Force acquisition now; do not replace this object by PID lookup later.
    $handle=('0x{0:x}' -f $process.Handle.ToInt64())
    $child.readers+=New-PreviewLogReader $process.StandardOutput.BaseStream (Join-Path $script:evidence ($Label+'.stdout.log'))
    $child.readers+=New-PreviewLogReader $process.StandardError.BaseStream (Join-Path $script:evidence ($Label+'.stderr.log'))
    $record.processHandle=$handle;
    $record.creationFileTime=$process.StartTime.ToUniversalTime().ToFileTimeUtc().ToString([Globalization.CultureInfo]::InvariantCulture)
    Write-D10Json (Join-Path $script:evidence ($Label+'-started.json')) $child.record
    $record['commandPath']=Join-Path $script:evidence ($Label+'-started.json');
    $record['commandSha256']=Get-D10Sha $record.commandPath
    return $child
}
function Finish-D10Child($Child){
    if($Child.finished){
        return $Child.record
    }
    if(-not $Child.record.started){
        $Child.finished=$true;
        return $Child.record
    }
    if(Test-D10Live $Child){
        throw 'Cannot finalize a live owned child.'
    }
    $Child.process.WaitForExit();
    $Child.record.exitCode=$Child.process.ExitCode;
    $Child.record['exitedUtc']=[DateTime]::UtcNow.ToString('o')
    $clock=[Diagnostics.Stopwatch]::StartNew()
    while(@($Child.readers|Where-Object {
        -not $_.eof
    }).Count -and
    $clock.Elapsed.TotalSeconds -lt 2){
        Drain-D10Children;
        Start-Sleep -Milliseconds 20
    }
    foreach($reader in $Child.readers){
        $reader.File.Dispose()
    }
    $Child.record['logs']=@($Child.readers|ForEach-Object {
        @{
            seen=$_.seen;
            saved=$_.saved;
            eof=$_.eof;
            truncated=$_.truncated
        }
    })
    $Child.record['stdoutSha256']=Get-D10Sha $Child.record.stdoutPath;
    $Child.record['stderrSha256']=Get-D10Sha $Child.record.stderrPath
    $Child.finished=$true;
    Write-D10Json (Join-Path $script:evidence ($Child.label+'-exit.json')) $Child.record
    return $Child.record
}
function Invoke-D10Acl([string]$Action,
[string]$Label){
    if(Test-D10Live $script:writer){
        throw 'A live ACL writer still owns its operation; no concurrent ACL action.'
    }
    if((Get-D10Sha $script:producer) -cne $producerHash){
        throw 'Original ACL producer bytes changed.'
    }
    $arguments=@('-NoProfile',
    '-NonInteractive',
    '-ExecutionPolicy',
    'Bypass',
    '-File',
    $script:producer,
    '-Action',
    $Action,
    '-ScopeParent',
    $script:permissions)
    if($Action -eq 'Prepare'){
        $arguments+=@('-SourcePackage',
        $script:source,
        '-ExpectedManifestSha256',
        $ExpectedManifestSha256.ToLowerInvariant(),
        '-BaselineDataRoot',
        $script:seed)
    }else{
        $arguments+=@('-Registration',
        $script:aclRegistration,
        '-Target',
        'data')
    }
    $script:writer=Start-D10Child $Label $script:powershell $arguments
    $clock=[Diagnostics.Stopwatch]::StartNew()
    while(Test-D10Live $script:writer){
        Drain-D10Children;
        if($clock.Elapsed.TotalSeconds -ge 120){
            $script:writer.record.timedOut=$true;
            Write-D10Json (Join-Path $script:evidence ($Label+'-timeout.json')) $script:writer.record;
            throw 'ACL child timed out and is retained alive; restoration cannot race it.'
        };
        Start-Sleep -Milliseconds 50
    }
    $record=Finish-D10Child $script:writer
    $record['stdout']=Get-D10Json $record.stdoutPath
    if($Action -eq 'Prepare'){
        $record['reportPath']=Join-Path $script:permissions 'prepare-report.json'
    }else{
        $reports=@(Get-ChildItem -LiteralPath $script:permissions -File -Filter ('acl-'+$Action.ToLowerInvariant()+'-data-*.json'))
        if($reports.Count -ne 1){
            throw 'Original ACL report is missing/ambiguous.'
        };
        $record['reportPath']=$reports[0].FullName
    }
    $record['reportSha256']=Get-D10Sha $record.reportPath;
    $record['report']=Get-D10Json $record.reportPath
    Write-D10Json (Join-Path $script:evidence ($Label+'-record.json')) $record
    return $record
}
function Assert-D10C0{
    if((Get-D10Sha $script:configuration) -cne $script:baselineHash){
        throw 'C0 bytes changed before recovery Save.'
    }
}
function Assert-D10DataIdentity{
    if((Get-D10Sha $script:aclRegistration) -cne $script:aclRegistrationHash){
        throw 'Original ACL registration changed.'
    }
    $hash=Get-D10Sha $script:statePath
    if($null -ne $script:dataStateHash -and
    $hash -cne $script:dataStateHash){
        throw 'Original ACL state bytes changed.'
    }
    $state=Get-D10Json $script:statePath
    if($state.schema -cne 'cordis.preview.acl-state/1' -or
    $state.target -ine $script:data -or
    $state.registrationSha256 -cne $script:aclRegistrationHash -or
    $state.tokenSid -cne $script:qaSid){
        throw 'Original data ACL state ownership differs.'
    }
    $objects=@(Get-TargetObjects $script:data);
    $rows=@($state.objects)
    if($objects.Count -ne $rows.Count){
        throw 'Original data object set changed.'
    }
    $seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $identities=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach($row in $rows){
        if($row.relativePath -match '(^|/)[.]{1,2}(/|$)|[\\:]' -or
        $row.relativePath.StartsWith('/') -or
        -not $seen.Add([string]$row.relativePath) -or
        -not $identities.Add(([string]$row.identity.volume+':'+[string]$row.identity.index))){
            throw 'Unsafe/duplicate data state object.'
        };
        $path=Assert-SafePath (Join-Path $script:data $row.relativePath.Replace('/',
        '\'));
        Assert-Identity $path $row.identity;
        if($row.directory -isnot [bool] -or
        [bool][IO.Directory]::Exists($path) -ne $row.directory){
            throw 'Original data object type changed.'
        }
    }
    $script:dataStateHash=$hash
    return $state
}
function Test-D10Restoration{
    $state=Assert-D10DataIdentity;
    $mismatches=@();
    $normalized=0;
    $observations=@()
    foreach($row in $state.objects){
        $path=Join-Path $script:data $row.relativePath.Replace('/',
        '\');
        $actual=(Get-ObjectAcl $path).GetSecurityDescriptorSddlForm([Security.AccessControl.AccessControlSections]::Access -bor
        [Security.AccessControl.AccessControlSections]::Owner -bor
        [Security.AccessControl.AccessControlSections]::Group);
        $comparison=Compare-PreviewRestoredAcl ([Security.AccessControl.RawSecurityDescriptor]::new($row.sddl)) ([Security.AccessControl.RawSecurityDescriptor]::new($actual));
        if($comparison.autoInheritedNormalized){
            $normalized++
        };
        if(-not $comparison.passed){
            $mismatches+=@{
                relativePath=$row.relativePath;
                comparison=$comparison
            }
        };
        $observations+=@{
            relativePath=$row.relativePath;
            identityVerified=$true;
            expectedSddl=$row.sddl;
            actualSddl=$actual;
            comparison=$comparison
        }
    }
    $write=[CordisPreviewAclNative]::OpenAccess($script:configuration,
    0x2);
    $delete=[CordisPreviewAclNative]::OpenAccess($script:configuration,
    0x10000)
    $result=@{
        passed=($mismatches.Count -eq 0 -and
        $write -eq 0 -and
        $delete -eq 0);
        rule='OwnerGroupOrderedDaclBinaryAllFlagsExceptDaclAutoInherited/1';
        allowedControlFlagsDifference=0x0400;
        objectCount=@($state.objects).Count;
        normalizedObjectCount=$normalized;
        mismatches=$mismatches;
        objects=$observations;
        identityVerified=$true;
        writeDataError=$write;
        deleteAccessError=$delete;
        filesWrittenOrDeleted=$false;
        stateSha256=(Get-D10Sha $script:statePath)
    }
    return $result
}
function Restore-D10Data([string]$Label){
    if($script:restoreAttempted){
        throw 'Original data restoration was already attempted; no replay.'
    }
    Assert-D10C0;
    [void](Assert-D10DataIdentity);
    $script:restoreAttempted=$true
    $original=Invoke-D10Acl 'Restore' $Label
    $semantic=Test-D10Restoration
    Write-D10Json (Join-Path $script:evidence ($Label+'-semantic.json')) $semantic
    if(-not $semantic.passed){
        throw 'Original data restoration is not independently verified.'
    }
    # Successful Save may atomically replace this file. Clear the old-state
    # responsibility BEFORE authorizing that Save and never Restore it again.
    $script:responsible=$false;
    $script:restored=$true
    return @{
        originalRestore=$original;
        semanticComparison=$semantic;
        restorationVerified=$true;
        baselineSha256=$script:baselineHash;
        stateSha256=(Get-D10Sha $script:statePath)
    }
}
function Write-D10Abort([string]$ErrorText){
    $path=Join-Path $script:control 'owner-aborted.json'
    if(-not(Test-Path -LiteralPath $path)){
        Write-D10Json $path @{
            schema='cordis.preview.readonly-save.abort/1';
            nonce=$script:nonce;
            registrationSha256=$script:registrationHash;
            utc=[DateTime]::UtcNow.ToString('o');
            error=$ErrorText;
            pid=$(if($null -ne $script:product -and
            $script:product.record.started){
                $script:product.process.Id
            }else{
                $null
            });
            restoreResponsibilityUnresolved=$script:responsible
        }
    }
}
function Assert-D10ControlOrder([int]$Maximum){
    if((Get-D10Sha $registrationPath) -cne $script:registrationHash){
        throw 'D10 registration bytes changed.'
    }
    foreach($file in Get-ChildItem -LiteralPath $script:control -Force){
        if($file.Name -ceq 'owner-aborted.json'){
            throw 'D10 owner has aborted.'
        };
        if($file.PSIsContainer -or
        $file.FullName -cnotin $script:phasePaths){
            throw 'Unexpected D10 control item.'
        };
        $index=[Array]::IndexOf($script:phasePaths,
        $file.FullName);
        if($index -gt $Maximum){
            throw 'Out-of-order D10 phase.'
        };
        if($script:phaseHashes.ContainsKey($index) -and
        (Get-D10Sha $file.FullName) -cne $script:phaseHashes[$index]){
            throw 'An already accepted D10 phase changed.'
        }
    }
}
function Write-D10Phase([int]$Index,
$Facts){
    Assert-D10ControlOrder ($Index-1)
    $previous=if($Index -eq 0){
        $script:registrationHash
    }else{
        $script:phaseHashes[$Index-1]
    }
    $value=@{
        schema='cordis.preview.readonly-save.phase/1';
        nonce=$script:nonce;
        registrationSha256=$script:registrationHash;
        phase=$script:phaseNames[$Index];
        previousSha256=$previous;
        utc=[DateTime]::UtcNow.ToString('o');
        pid=$script:targetIdentity.pid;
        creationFileTime=$script:targetIdentity.creationFileTime;
        facts=$Facts
    }
    Write-D10Json $script:phasePaths[$Index] $value;
    $script:phaseHashes[$Index]=Get-D10Sha $script:phasePaths[$Index];
    $script:phases+=,
    $value
}
function Read-D10PhaseJson([int]$Index){
    $clock=[Diagnostics.Stopwatch]::StartNew()
    while($true){
        try{
            return Get-D10Json $script:phasePaths[$Index]
        }catch{
            $transient=($_.Exception -is [IO.IOException] -or
            $_.Exception -is [Text.DecoderFallbackException] -or
            $_.FullyQualifiedErrorId -like '*ConvertFromJson*')
            if(-not $transient -or
            $clock.Elapsed.TotalSeconds -ge 5){
                throw
            }
            # A CreateNew writer may still be publishing its sole immutable
            # byte sequence. Retry this observation only, never any UI input.
            Drain-D10Children;
            Assert-D10ControlOrder $Index;
            Start-Sleep -Milliseconds 25
        }
    }
}
function Wait-D10Phase([int]$Index){
    $clock=[Diagnostics.Stopwatch]::StartNew()
    while(-not(Test-Path -LiteralPath $script:phasePaths[$Index])){
        Drain-D10Children;
        Assert-D10ControlOrder $Index;
        if(-not(Test-D10Live $script:product) -and
        ($Index -ne 6 -or
        $script:product.process.ExitCode -ne 0)){
            throw 'Original product exited before the required phase.'
        };
        if($null -ne $script:driver -and
        -not(Test-D10Live $script:driver) -and
        -not(Test-Path -LiteralPath $script:phasePaths[$Index])){
            throw 'UI adapter exited before the required phase.'
        };
        if($clock.Elapsed.TotalSeconds -ge 300){
            throw 'D10 phase wait expired; input is not replayed.'
        };
        Start-Sleep -Milliseconds 50
    }
    Assert-D10ControlOrder $Index
    $phase=Read-D10PhaseJson $Index
    if(-not(Test-D10Live $script:product)){
        if($Index -lt 5 -or $script:product.process.ExitCode -ne 0){
            throw 'Original product exited before a live pre-Save phase was accepted.'
        }
    }
    if($phase.schema -cne 'cordis.preview.readonly-save.phase/1' -or
    $phase.nonce -cne $script:nonce -or
    $phase.registrationSha256 -cne $script:registrationHash -or
    $phase.phase -cne $script:phaseNames[$Index] -or
    $phase.previousSha256 -cne $script:phaseHashes[$Index-1] -or
    -not (Test-D10Integer $phase.pid) -or $phase.pid -ne $script:targetIdentity.pid -or
    $phase.creationFileTime -isnot [string] -or $phase.creationFileTime -cnotmatch '^[0-9]+$' -or
    $phase.creationFileTime -cne $script:targetIdentity.creationFileTime){
        throw 'D10 immutable phase chain differs.'
    }
    $parsedUtc=[DateTimeOffset]::MinValue;
    if(-not[DateTimeOffset]::TryParse([string]$phase.utc,
    [ref]$parsedUtc)){
        throw 'D10 phase timestamp is invalid.'
    }
    $script:phaseHashes[$Index]=Get-D10Sha $script:phasePaths[$Index];
    $script:phases+=,
    $phase;
    return $phase.facts
}
function Assert-D10UiFacts($Facts,
$Previous){
    if(($Facts.revision -isnot [int] -and
    $Facts.revision -isnot [long]) -or
    $Facts.revision -le 0){
        throw 'Observed UI revision is not positive.'
    }
    Assert-D10Value $Facts.targetValue $script:wanted
    $entries=@($Facts.entries)
    if($entries.Count -ne 4 -or
    @($entries|Where-Object {
        $_ -isnot [string]
    }).Count -ne 0 -or
    $entries[0] -cne 'notes' -or
    $entries[1] -cne '启用' -or
    $entries[2] -cne '运行中' -or
    $entries[3] -cnotmatch '^[1-9][0-9]*$'){
        throw 'Actual UI entry row differs.'
    }
    $snapshot=Assert-D10Path $Facts.snapshotPath
    if(-not(Test-D10Inside $snapshot $script:uiEvidence) -or
    $Facts.snapshotSha256 -cnotmatch '^[0-9a-f]{64}$' -or
    (Get-D10Sha $snapshot) -cne $Facts.snapshotSha256){
        throw 'UI phase snapshot provenance differs.'
    }
    if($null -ne $Previous -and
    ($Facts.revision -ne $Previous.revision -or
    (($entries|ConvertTo-Json -Compress) -cne (@($Previous.entries)|ConvertTo-Json -Compress)))){
        throw 'UI accepted revision/entry/instance changed.'
    }
}
try{
    # Read only prerequisite facts before inspecting any package DLL. Its host
    # export/wrapper fields are management attestations anchored by the caller.
    $Prerequisite=Assert-D10Path $Prerequisite
    if((Get-D10Sha $Prerequisite) -cne $ExpectedPrerequisiteSha256.ToLowerInvariant()){
        throw 'D10 prerequisite anchor differs.'
    }
    $pre=Get-D10Json $Prerequisite
    Assert-D10Prerequisite $pre
    $identity=[Security.Principal.WindowsIdentity]::GetCurrent();
    $principal=[Security.Principal.WindowsPrincipal]::new($identity)
    $current=[Diagnostics.Process]::GetCurrentProcess();
    $script:qaSid=$identity.User.Value
    $groupText=(& "$env:WINDIR\System32\whoami.exe" /groups /fo csv /nh) -join "`n"
    $integrity=@([regex]::Matches($groupText,
    'S-1-16-([0-9]+)')|ForEach-Object {
        [int]$_.Groups[1].Value
    }|Select-Object -Unique)
    if($LASTEXITCODE -ne 0 -or
    $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator) -or
    -not[Environment]::UserInteractive -or
    -not[Environment]::Is64BitProcess -or
    $integrity.Count -ne 1 -or
    $integrity[0] -ne 8192 -or
    $pre.qaSid -cne $script:qaSid -or
    $pre.sessionId -ne $current.SessionId){
        throw 'D10 requires the actual ordinary interactive QA Medium token.'
    }
    $human=Assert-D10Path $pre.humanReviewRoot
    if(-not(Test-D10Inside $human ([IO.Path]::GetFullPath($env:USERPROFILE)))){
        throw 'Human prerequisite is outside the current QA profile.'
    }
    $humanSummary=Join-Path $human 'session-summary.json';
    $humanExit=Join-Path $human 'product-exit.json'
    if((Get-D10Sha $humanSummary) -cne $pre.humanSessionSummarySha256 -or
    (Get-D10Sha $humanExit) -cne $pre.humanProductExitSha256){
        throw 'Human exit bytes differ from the prerequisite.'
    }
    $hs=Get-D10Json $humanSummary;
    $he=Get-D10Json $humanExit
    Assert-D10HumanExit $hs $he $pre
    if(@(Get-Process -Name cordis_config_workbench,
    cordis_platform_workbench_demo -ErrorAction SilentlyContinue|Where-Object {
        $_.SessionId -eq $current.SessionId
    }).Count -ne 0 -or
    $null -ne (Get-Process -Id $pre.humanWrapperPid -ErrorAction SilentlyContinue)){
        throw 'A prerequisite human/Cordis process is still present.'
    }
    $script:source=Assert-D10Path $SourcePackage;
    $script:toolsRoot=Assert-D10Path $UiToolRoot;
    $ScopeRoot=Assert-D10Path $ScopeRoot -New
    foreach($other in @($script:source,
    $script:toolsRoot,
    $human)){
        if($ScopeRoot -ieq $other -or
        (Test-D10Inside $ScopeRoot $other) -or
        (Test-D10Inside $other $ScopeRoot)){
            throw 'D10 scope overlaps an input/human directory.'
        }
    }
    $toolsPath=Join-Path $script:toolsRoot 'tools-manifest.json'
    if((Get-D10Sha $toolsPath) -cne $ExpectedToolsManifestSha256.ToLowerInvariant()){
        throw 'External D10 tool manifest anchor differs.'
    }
    $tools=Get-D10Json $toolsPath
    if($tools.schema -cne 'cordis.preview.readonly-save.tools/1' -or
    $tools.qaSid -cne $script:qaSid -or
    $tools.sessionId -ne $current.SessionId){
        throw 'D10 tool manifest QA/session differs.'
    }
    $toolRecords=@();
    $seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach($entry in $tools.files){
        $relative=Assert-D10Relative $entry.path;
        $path=Assert-D10Path (Join-Path $script:toolsRoot $relative.Replace('/',
        '\'));
        if(-not(Test-D10Inside $path $script:toolsRoot) -or
        -not $seen.Add($path) -or
        $entry.sha256 -cnotmatch '^[0-9a-f]{64}$' -or
        (Get-Item -LiteralPath $path).Length -ne $entry.bytes -or
        (Get-D10Sha $path) -cne $entry.sha256){
            throw 'D10 tool bytes/path differ.'
        };
        $toolRecords+=@{
            path=$path;
            bytes=$entry.bytes;
            sha256=$entry.sha256
        }
    }
    $required=@((Assert-D10Path $PSCommandPath),
    (Assert-D10Path $PythonExe),
    (Assert-D10Path $ComparisonSource),
    (Join-Path $script:toolsRoot 'tools\preview_readonly_ui.py'),
    (Join-Path $script:toolsRoot 'tools\preview_ui.py'),
    (Join-Path $script:toolsRoot 'tools\preview_ui_action.ps1'),
    (Join-Path $script:toolsRoot 'delivery\windows\verification\preview-windows.psm1'),
    (Join-Path $script:toolsRoot 'delivery\windows\verification\preview-ui.psm1'))
    foreach($path in $required){
        if(-not $seen.Contains($path)){
            throw 'A required D10 executable/tool is not listed.'
        }
    }
    $actualTools=@(Get-ChildItem -LiteralPath $script:toolsRoot -Force -Recurse|Where-Object {
        -not $_.PSIsContainer -and
        $_.FullName -ine $toolsPath
    })
    if($actualTools.Count -ne $toolRecords.Count){
        throw 'D10 tool inventory differs.'
    }
    foreach($item in $actualTools){
        [void](Assert-D10Path $item.FullName);
        if(-not $seen.Contains($item.FullName)){
            throw 'An unexpected D10 tool file exists.'
        }
    }
    Import-Module (Join-Path $script:toolsRoot 'delivery\windows\verification\preview-windows.psm1') -Force
    $manifestPath=Join-Path $script:source 'build-manifest.json'
    if((Get-D10Sha $manifestPath) -cne $ExpectedManifestSha256.ToLowerInvariant()){
        throw 'External source package anchor differs.'
    }
    $frozen=Get-PreviewTreeSnapshot $script:source;
    $sourceManifest=Get-D10Json $manifestPath
    # Bind all executable bytes, especially the producer and verifier, BEFORE
    # extracting or invoking any code from this untrusted input directory.
    Assert-D10SourceSnapshot $frozen $sourceManifest
    $script:producer=Join-Path $script:source 'verification\preview-acl.ps1';
    $producerHash=Get-D10Sha $script:producer
    $producerAst=Get-D10Ast $script:producer $producerHash
    if('CordisPreviewAclNative' -as [type]){
        throw 'D10 original ACL native type must be a fresh definition.'
    }
    $native=@($producerAst.FindAll({
        param($node)$node -is [Management.Automation.Language.CommandAst] -and
        $node.GetCommandName() -ceq 'Add-Type'
    },
    $true));
    if($native.Count -ne 1){
        throw 'Original native ACL identity definition is ambiguous.'
    };
    . ([ScriptBlock]::Create($native[0].Extent.Text))
    foreach($name in @('Assert-SafePath',
    'Get-ObjectAcl',
    'Get-Identity',
    'Assert-Identity',
    'Get-TargetObjects')){
        . ([ScriptBlock]::Create((Import-D10Function $producerAst $name)))
    }
    $comparisonRecord=@($toolRecords|Where-Object {
        $_.path -ieq (Assert-D10Path $ComparisonSource)
    })[0]
    $comparisonAst=Get-D10Ast $comparisonRecord.path $comparisonRecord.sha256
    . ([ScriptBlock]::Create((Import-D10Function $comparisonAst 'Compare-PreviewRestoredAcl')))
    [void](New-Item -ItemType Directory -Path $ScopeRoot);
    $script:created=$true
    $script:evidence=Join-Path $ScopeRoot 'evidence';
    $script:control=Join-Path $script:evidence 'control';
    $script:uiEvidence=Join-Path $script:evidence 'ui';
    $script:seed=Join-Path $ScopeRoot 'seed';
    $script:cwd=Join-Path $ScopeRoot 'cwd';
    $script:permissions=Join-Path $ScopeRoot 'permissions'
    foreach($path in @($script:evidence,
    $script:control,
    $script:seed,
    $script:cwd,
    (Join-Path $script:seed 'store'))){
        [void](New-Item -ItemType Directory -Path $path)
    }
    for($index=0;
    $index -lt 7;
    $index++){
        $script:phasePaths+=Join-Path $script:control ('{0:d2}-{1}.json' -f ($index+1),
        $script:phaseNames[$index])
    }
    $baseline=[ordered]@{
        formatVersion=1;
        plugins=@([ordered]@{
            id='notes';
            plugin='demo.notes';
            config=[ordered]@{
                title=('D10-C0-'+$script:nonce)
            }
        })
    }
    $script:wanted=[ordered]@{
        formatVersion=1;
        plugins=@([ordered]@{
            id='notes';
            plugin='demo.notes';
            config=[ordered]@{
                title=('D10-R1-'+$script:nonce)
            }
        })
    }
    Write-D10Json (Join-Path $script:seed 'session.yaml') $baseline
    $script:powershell=Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'
    $prepare=Invoke-D10Acl 'Prepare' 'acl-prepare'
    if($prepare.exitCode -ne 0 -or
    -not $prepare.report.passed -or
    $prepare.report.protectionApplied){
        throw 'Original ACL Prepare did not produce unprotected verified copies.'
    }
    $script:aclRegistration=Join-Path $script:permissions 'acl-registration.json';
    $script:aclRegistrationHash=Get-D10Sha $script:aclRegistration
    $aclReg=Get-D10Json $script:aclRegistration
    if($aclReg.schema -cne 'cordis.preview.acl-registration/1' -or
    $aclReg.createdBy.path -ine $script:producer -or
    $aclReg.createdBy.sha256 -cne $producerHash){
        throw 'Original ACL producer registration differs.'
    }
    $application=Join-Path $script:permissions 'acl\application';
    $script:data=Join-Path $script:permissions 'acl\data';
    $script:configuration=Join-Path $script:data 'session.yaml';
    $script:statePath=Join-Path $script:permissions 'acl-state-data.json';
    $script:baselineHash=Get-D10Sha $script:configuration
    if($script:baselineHash -cne (Get-D10Sha (Join-Path $script:seed 'session.yaml'))){
        throw 'Prepared C0 bytes differ.'
    }
    $packageBefore=Get-PreviewTreeSnapshot $application
    if(($packageBefore|ConvertTo-Json -Depth 5 -Compress) -cne ($frozen|ConvertTo-Json -Depth 5 -Compress)){
        throw 'Prepared application copy differs.'
    }
    $facts=Get-PreviewHostFacts;
    $preferencesBefore=Get-PreviewFileSnapshot $facts.preferencesFile
    Write-D10Json (Join-Path $script:evidence 'package-before.json') $packageBefore;
    Write-D10Json (Join-Path $script:evidence 'preferences-before.json') $preferencesBefore
    $wrapperIdentity=Get-D10Descriptor $current
    $script:product=Start-D10Child 'product' (Join-Path $application 'cordis_config_workbench.exe') @('--file',
    $script:configuration)
    $script:targetIdentity=Get-D10Descriptor $script:product.process
    if($script:targetIdentity.parentPid -ne $wrapperIdentity.pid -or
    $script:targetIdentity.ownerSid -cne $script:qaSid -or
    $script:targetIdentity.sessionId -ne $current.SessionId){
        throw 'Actual product parent/token/session differs.'
    }
    $registration=[ordered]@{
        schema='cordis.preview.readonly-save.registration/1';
        nonce=$script:nonce;
        scopeRoot=$ScopeRoot;
        evidenceRoot=$script:evidence;
        packageRoot=$application;
        configurationFile=$script:configuration;
        manifestSha256=$ExpectedManifestSha256.ToLowerInvariant();
        qaSid=$script:qaSid;
        sessionId=$current.SessionId;
        target=$script:targetIdentity;
        wrapper=$wrapperIdentity;
        baselineValue=$baseline;
        wantedValue=$script:wanted;
        baselineSha256=$script:baselineHash;
        aclRegistrationPath=$script:aclRegistration;
        aclRegistrationSha256=$script:aclRegistrationHash;
        aclProducerPath=$script:producer;
        aclProducerSha256=$producerHash;
        tools=$toolRecords
    }
    $registrationPath=Join-Path $script:evidence 'registration.json';
    Write-D10Json $registrationPath $registration;
    $script:registrationHash=Get-D10Sha $registrationPath
    Write-D10Phase 0 @{
        token=@{
            qaSid=$script:qaSid;
            sessionId=$current.SessionId;
            administrator=$false;
            integrityRid=8192
        };
        baselineSha256=$script:baselineHash;
        productHandle=$script:product.record.processHandle
    }
    $script:driver=Start-D10Child 'ui-adapter' $PythonExe @('-I',
    '-B',
    (Join-Path $script:toolsRoot 'tools\preview_readonly_ui.py'),
    '--registration',
    $registrationPath,
    '--expected-registration-sha256',
    $script:registrationHash)
    $applied=Wait-D10Phase 1;
    Assert-D10UiFacts $applied $null;
    Assert-D10C0
    # Ownership starts BEFORE Protect can create state or change the first DACL.
    $script:responsible=$true
    $protect=Invoke-D10Acl 'Protect' 'acl-protect'
    if($protect.exitCode -ne 0 -or
    -not $protect.report.passed){
        throw 'Original Protect failed; only owned recovery may follow.'
    }
    [void](Assert-D10DataIdentity)
    $probe=Invoke-D10Acl 'Probe' 'acl-probe'
    foreach($record in @($protect,
    $probe)){
        if($record.exitCode -ne 0 -or
        -not $record.report.passed -or
        -not $record.report.probe.createNewDenied -or
        $record.report.probe.writeDataError -ne 5 -or
        $record.report.probe.deleteAccessError -ne 5){
            throw 'Original effective data protection was not established.'
        }
    }
    Assert-D10C0;
    [void](Assert-D10DataIdentity)
    Write-D10Phase 2 @{
        effectiveProtectionVerified=$true;
        baselineSha256=$script:baselineHash;
        stateSha256=(Get-D10Sha $script:statePath);
        protect=$protect;
        probe=$probe
    }
    $failed=Wait-D10Phase 3;
    Assert-D10UiFacts $failed $applied;
    Assert-D10C0;
    [void](Assert-D10DataIdentity)
    $restoration=Restore-D10Data 'acl-restore'
    Write-D10Phase 4 $restoration
    $saved=Wait-D10Phase 5;
    Assert-D10UiFacts $saved $applied;
    Assert-D10Value (Get-D10Json $script:configuration) $script:wanted
    [void](Wait-D10Phase 6)
    while((Test-D10Live $script:product) -or
    (Test-D10Live $script:driver)){
        Drain-D10Children;
        Start-Sleep -Milliseconds 50
    }
    $productExit=Finish-D10Child $script:product;
    $driverExit=Finish-D10Child $script:driver
    $report.productExitCode=$productExit.exitCode;
    $report.uiExitCode=$driverExit.exitCode
    if($productExit.exitCode -ne 0 -or
    $driverExit.exitCode -ne 0 -or
    $script:responsible -or
    -not $script:restored){
        throw 'D10 actual process exits/restoration are incomplete.'
    }
    foreach($child in $script:children){
        if(@($child.record.logs|Where-Object {
            $_.truncated -or
            -not $_.eof
        }).Count){
            throw 'D10 log capture is incomplete/truncated.'
        }
    }
    $after=Get-PreviewTreeSnapshot $application
    if(($after|ConvertTo-Json -Depth 5 -Compress) -cne ($frozen|ConvertTo-Json -Depth 5 -Compress) -or
    (Get-D10Sha (Join-Path $script:seed 'session.yaml')) -cne $script:baselineHash){
        throw 'Original application/seed bytes changed.'
    }
    foreach($tool in $toolRecords){
        if((Get-D10Sha $tool.path) -cne $tool.sha256){
            throw 'Reviewed D10 tool changed.'
        }
    }
    if((Get-PreviewTreeSnapshot $script:source|ConvertTo-Json -Depth 5 -Compress) -cne ($frozen|ConvertTo-Json -Depth 5 -Compress)){
        throw 'Original source package changed.'
    }
    Write-D10Json (Join-Path $script:evidence 'package-after.json') $after
    Write-D10Json (Join-Path $script:evidence 'preferences-after.json') (Get-PreviewFileSnapshot $facts.preferencesFile)
    $report['savedFile']=Get-PreviewFileSnapshot $script:configuration;
    $report['phases']=$script:phases;
    $report['restoration']=$restoration;
    $report.recording_complete=$true
}catch{
    $report.error=$_.Exception.Message
    if($script:created -and
    $null -ne $script:product){
        try{
            Write-D10Abort $report.error
        }catch{
            $report['abortWriteError']=$_.Exception.Message
        }
    }
}finally{
    if($script:created){
        # A timed-out original writer retains exclusive ownership until it really
        # exits. Keep its handle/logs and the product's handle; never race Restore.
        if($null -ne $script:writer){
            while(Test-D10Live $script:writer){
                try{
                    Drain-D10Children
                }catch{
                    $report['drainError']=$_.Exception.Message
                };
                Start-Sleep -Milliseconds 100
            };
            if(-not $script:writer.finished){
                try{
                    [void](Finish-D10Child $script:writer)
                }catch{
                    $report['writerFinalizationError']=$_.Exception.Message
                }
            }
        }
        if($script:responsible -and
        -not $script:restoreAttempted -and
        (Test-Path -LiteralPath $script:statePath)){
            try{
                $report['cleanupRestoration']=Restore-D10Data 'acl-cleanup-restore'
            }catch{
                $report['cleanupRestorationError']=$_.Exception.Message
            }
        }
        # If Restore itself timed out, wait for that one writer, then independently
        # verify it; never issue Restore a second time.
        if($script:restoreAttempted -and
        $script:responsible -and
        $null -ne $script:writer){
            while(Test-D10Live $script:writer){
                try{
                    Drain-D10Children
                }catch{
                    $report['drainError']=$_.Exception.Message
                };
                Start-Sleep -Milliseconds 100
            }
            try{
                if(-not $script:writer.finished){
                    [void](Finish-D10Child $script:writer)
                };
                $semantic=Test-D10Restoration;
                $report['lateRestorationComparison']=$semantic;
                if($semantic.passed){
                    $script:responsible=$false;
                    $script:restored=$true
                }
            }catch{
                $report['lateRestorationError']=$_.Exception.Message
            }
        }
        while(@($script:children|Where-Object {
            Test-D10Live $_
        }).Count){
            try{
                Drain-D10Children
            }catch{
                $report['drainError']=$_.Exception.Message
            };
            if(Test-D10Live $script:product){
                $report.liveProduct=@{
                    pid=$script:product.process.Id;
                    processHandle=$script:product.record.processHandle;
                    creationFileTime=$script:product.record.creationFileTime;
                    noTerminationAttempted=$true
                }
            };
            Start-Sleep -Milliseconds 100
        }
        foreach($child in $script:children){
            try{
                [void](Finish-D10Child $child)
            }catch{
                $report['childFinalizationError']=$_.Exception.Message
            };
            if($child.record.started){
                if($child.label -eq 'product'){
                    $report.productExitCode=$child.process.ExitCode;
                    $report.liveProduct=$null
                };
                if($child.label -eq 'ui-adapter'){
                    $report.uiExitCode=$child.process.ExitCode
                }
            };
            $child.process.Dispose()
        }
        $report.restoreResponsibilityUnresolved=$script:responsible;
        $report['finishedUtc']=[DateTime]::UtcNow.ToString('o')
        Write-D10Json (Join-Path $script:evidence 'result.json') $report
    }
}
if(-not $report.recording_complete){
    [Console]::Error.WriteLine($report.error);
    exit 1
}
