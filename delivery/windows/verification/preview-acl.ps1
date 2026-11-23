<#
.SYNOPSIS
Prepare, protect, probe, or restore preview copies using stock Windows PowerShell.
.DESCRIPTION
The registration and ACL state stay outside the target. No original package,
user profile, arbitrary directory, or unregistered target is accepted. The
DELETE probe requests kernel DELETE access without unlinking an existing file.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][ValidateSet('Prepare', 'Protect', 'Probe', 'Restore')][string]$Action,
    [Parameter(Mandatory = $true)][string]$ScopeParent,
    [string]$Registration,
    [ValidateSet('application', 'data')][string]$Target,
    [string]$SourcePackage,
    [ValidatePattern('^[0-9a-fA-F]{64}$')][string]$ExpectedManifestSha256,
    [string]$BaselineDataRoot
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)

if (-not ('CordisPreviewAclNative' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
public static class CordisPreviewAclNative {
    [StructLayout(LayoutKind.Sequential)] struct Info {
        public uint Attributes;
        public System.Runtime.InteropServices.ComTypes.FILETIME Creation, Access, Write;
        public uint Volume, SizeHigh, SizeLow, Links, IndexHigh, IndexLow;
    }
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern SafeFileHandle CreateFileW(string name, uint access, uint share, IntPtr security,
                                            uint creation, uint flags, IntPtr template);
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern bool GetFileInformationByHandle(SafeFileHandle handle, out Info info);
    public static string[] Identity(string path) {
        using (var handle = CreateFileW(path, 0x80, 7, IntPtr.Zero, 3, 0x02200000, IntPtr.Zero)) {
            if (handle.IsInvalid) throw new Win32Exception(Marshal.GetLastWin32Error());
            Info info;
            if (!GetFileInformationByHandle(handle, out info)) throw new Win32Exception(Marshal.GetLastWin32Error());
            if ((info.Attributes & 0x400) != 0) throw new InvalidOperationException("Reparse object refused.");
            return new [] { info.Volume.ToString(), (((ulong)info.IndexHigh << 32) | info.IndexLow).ToString(), info.Links.ToString() };
        }
    }
    public static int OpenAccess(string path, uint access) {
        using (var handle = CreateFileW(path, access, 7, IntPtr.Zero, 3, 0x02200000, IntPtr.Zero)) {
            return handle.IsInvalid ? Marshal.GetLastWin32Error() : 0;
        }
    }
}
'@
}

function Assert-SafePath([string]$Path) {
    if (-not [System.IO.Path]::IsPathRooted($Path)) { throw 'An absolute path is required.' }
    $full = [System.IO.Path]::GetFullPath($Path).TrimEnd('\')
    $cursor = $full
    while ($cursor) {
        if (Test-Path -LiteralPath $cursor) {
            $item = Get-Item -LiteralPath $cursor -Force
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'Reparse path refused.' }
        }
        $parent = [System.IO.Path]::GetDirectoryName($cursor)
        if ($parent -eq $cursor) { break }
        $cursor = $parent
    }
    return $full
}

function Write-NewJson([string]$Path, $Value) {
    $bytes = [Text.UTF8Encoding]::new($false).GetBytes(($Value | ConvertTo-Json -Depth 20) + "`n")
    $stream = [IO.File]::Open($Path, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
    try { $stream.Write($bytes, 0, $bytes.Length) } finally { $stream.Dispose() }
}

function Get-Sha([string]$Path) {
    $algorithm = [Security.Cryptography.SHA256]::Create()
    $stream = [IO.File]::OpenRead($Path)
    try { return ([BitConverter]::ToString($algorithm.ComputeHash($stream))).Replace('-', '').ToLowerInvariant() }
    finally { $stream.Dispose(); $algorithm.Dispose() }
}

function Get-ObjectAcl([string]$Path) {
    if ([IO.Directory]::Exists($Path)) { return [IO.Directory]::GetAccessControl($Path) }
    return [IO.File]::GetAccessControl($Path)
}

function Compare-PreviewRestoredAcl(
    [Security.AccessControl.RawSecurityDescriptor]$Expected,
    [Security.AccessControl.RawSecurityDescriptor]$Actual
) {
    $differences = @()
    $expectedOwner = if ($null -ne $Expected.Owner) { $Expected.Owner.Value } else { $null }
    $actualOwner = if ($null -ne $Actual.Owner) { $Actual.Owner.Value } else { $null }
    $expectedGroup = if ($null -ne $Expected.Group) { $Expected.Group.Value } else { $null }
    $actualGroup = if ($null -ne $Actual.Group) { $Actual.Group.Value } else { $null }
    if ($expectedOwner -cne $actualOwner) { $differences += 'owner' }
    if ($expectedGroup -cne $actualGroup) { $differences += 'group' }
    $flagsXor = [int]$Expected.ControlFlags -bxor [int]$Actual.ControlFlags
    if (($flagsXor -band (-bnot 0x0400)) -ne 0) { $differences += 'controlFlags' }
    $expectedDacl = $Expected.DiscretionaryAcl
    $actualDacl = $Actual.DiscretionaryAcl
    if (($null -eq $expectedDacl) -ne ($null -eq $actualDacl)) {
        $differences += 'daclNullness'
    } elseif ($null -ne $expectedDacl) {
        $expectedBytes = [byte[]]::new($expectedDacl.BinaryLength)
        $actualBytes = [byte[]]::new($actualDacl.BinaryLength)
        $expectedDacl.GetBinaryForm($expectedBytes, 0)
        $actualDacl.GetBinaryForm($actualBytes, 0)
        if ([Convert]::ToBase64String($expectedBytes) -cne [Convert]::ToBase64String($actualBytes)) {
            $differences += 'orderedDaclBinary'
        }
    }
    [pscustomobject]@{
        passed = ($differences.Count -eq 0)
        autoInheritedNormalized = ($differences.Count -eq 0 -and $flagsXor -eq 0x0400)
        differences = $differences; controlFlagsXor = $flagsXor
        expectedControlFlags = [int]$Expected.ControlFlags; actualControlFlags = [int]$Actual.ControlFlags
        expectedOwner = $expectedOwner; actualOwner = $actualOwner
        expectedGroup = $expectedGroup; actualGroup = $actualGroup
    }
}

function Set-ObjectAcl([string]$Path, $Acl) {
    if ([IO.Directory]::Exists($Path)) { [IO.Directory]::SetAccessControl($Path, $Acl) }
    else { [IO.File]::SetAccessControl($Path, $Acl) }
}

function Read-Json([string]$Path) {
    $safe = Assert-SafePath $Path
    if (-not [IO.File]::Exists($safe) -or (Get-Item -LiteralPath $safe -Force).Length -gt 8388608) {
        throw 'Missing or excessive ACL metadata.'
    }
    return ([Text.UTF8Encoding]::new($false, $true).GetString([IO.File]::ReadAllBytes($safe)) | ConvertFrom-Json)
}

function Get-Identity([string]$Path) {
    $value = [CordisPreviewAclNative]::Identity((Assert-SafePath $Path))
    $item = Get-Item -LiteralPath $Path -Force
    if (-not $item.PSIsContainer -and $value[2] -ne '1') { throw 'Hard-linked file refused.' }
    return @{ volume = $value[0]; index = $value[1] }
}

function Assert-Identity([string]$Path, $Expected) {
    $actual = Get-Identity $Path
    if ($actual.volume -cne [string]$Expected.volume -or $actual.index -cne [string]$Expected.index) {
        throw 'Registered file identity changed.'
    }
}

function Get-TargetObjects([string]$Root) {
    $values = [Collections.Generic.List[object]]::new()
    $pending = [Collections.Generic.Queue[string]]::new()
    $pending.Enqueue((Assert-SafePath $Root))
    while ($pending.Count -gt 0) {
        $path = $pending.Dequeue()
        [void](Assert-SafePath $path)
        [void](Get-Identity $path)
        $item = Get-Item -LiteralPath $path -Force
        $values.Add($item)
        if ($item.PSIsContainer) {
            foreach ($child in Get-ChildItem -LiteralPath $path -Force) {
                [void](Assert-SafePath $child.FullName)
                $pending.Enqueue($child.FullName)
            }
        }
    }
    return @($values | Sort-Object { $_.FullName.Length })
}

function Get-Snapshot([string]$Root, [switch]$DataOnly) {
    $rootPath = Assert-SafePath $Root
    $items = if ($DataOnly) {
        $session = Join-Path $rootPath 'session.yaml'
        $store = Join-Path $rootPath 'store'
        if (-not [IO.File]::Exists($session) -or -not [IO.Directory]::Exists($store)) {
            throw 'Baseline requires the actual session.yaml and store; locators are never rewritten.'
        }
        @((Get-Item -LiteralPath $session -Force)) + @(Get-TargetObjects $store)
    } else { @(Get-TargetObjects $rootPath) }
    $result = [ordered]@{}
    foreach ($item in ($items | Sort-Object FullName)) {
        [void](Get-Identity $item.FullName)
        if (-not $item.PSIsContainer) {
            $relative = $item.FullName.Substring($rootPath.Length + 1).Replace('\', '/')
            $result[$relative] = [ordered]@{ bytes = $item.Length; sha256 = Get-Sha $item.FullName }
        }
    }
    return ,$result
}

function Assert-Snapshot($Actual, $Expected) {
    if ($Actual.Count -ne $Expected.Count) { throw 'Copy file set changed.' }
    foreach ($name in $Expected.Keys) {
        if (-not $Actual.Contains($name) -or $Actual[$name].bytes -ne $Expected[$name].bytes -or
            $Actual[$name].sha256 -cne $Expected[$name].sha256) { throw 'Copy bytes changed.' }
    }
}

function Copy-Tree([string]$Source, [string]$Destination, $Expected, [switch]$DataOnly) {
    $sourcePath = Assert-SafePath $Source
    $destinationPath = Assert-SafePath $Destination
    if ([IO.Directory]::Exists($destinationPath) -or [IO.File]::Exists($destinationPath)) { throw 'Copy destination must be new.' }
    Assert-Snapshot (Get-Snapshot $sourcePath -DataOnly:$DataOnly) $Expected
    [void][IO.Directory]::CreateDirectory($destinationPath)
    $items = if ($DataOnly) {
        @((Get-Item -LiteralPath (Join-Path $sourcePath 'session.yaml') -Force)) +
            @(Get-TargetObjects (Join-Path $sourcePath 'store'))
    } else { @(Get-TargetObjects $sourcePath) }
    foreach ($item in ($items | Sort-Object { $_.FullName.Length })) {
        if ($item.FullName -ieq $sourcePath) { continue }
        $relative = $item.FullName.Substring($sourcePath.Length + 1)
        $targetPath = Assert-SafePath (Join-Path $destinationPath $relative)
        if ($item.PSIsContainer) { [void][IO.Directory]::CreateDirectory($targetPath); continue }
        $identity = Get-Identity $item.FullName
        $stamp = [IO.File]::GetLastWriteTimeUtc($item.FullName).Ticks
        $row = $Expected[$relative.Replace('\', '/')]
        if ((Get-Sha $item.FullName) -cne $row.sha256) { throw 'Source changed before copy.' }
        [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($targetPath))
        $reader = [IO.File]::Open($item.FullName, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
        try {
            $writer = [IO.File]::Open($targetPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
            try { $reader.CopyTo($writer) } finally { $writer.Dispose() }
        } finally { $reader.Dispose() }
        Assert-Identity $item.FullName $identity
        if ([IO.File]::GetLastWriteTimeUtc($item.FullName).Ticks -ne $stamp -or
            (Get-Sha $item.FullName) -cne $row.sha256 -or (Get-Sha $targetPath) -cne $row.sha256 -or
            ([IO.FileInfo]::new($targetPath)).Length -ne $row.bytes) { throw 'Source changed during copy.' }
    }
    Assert-Snapshot (Get-Snapshot $sourcePath -DataOnly:$DataOnly) $Expected
    Assert-Snapshot (Get-Snapshot $destinationPath) $Expected
}

function Quote-Argument([string]$Value) {
    return '"' + [regex]::Replace([regex]::Replace($Value, '(\\*)"', '$1$1\"'), '(\\+)$', '$1$1') + '"'
}

function Invoke-SourceVerification([string]$Package, [string]$Anchor) {
    $verifier = Assert-SafePath (Join-Path $PSScriptRoot 'verify-package.ps1')
    if (-not [IO.File]::Exists($verifier) -or
        (Get-Sha $verifier) -cne (Get-Sha (Join-Path $Package 'verification/verify-package.ps1'))) {
        throw 'The independent verifier differs from the original verification source.'
    }
    $arguments = @('-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', $verifier,
                   '-PackageRoot', $Package, '-ExpectedManifestSha256', $Anchor)
    $info = [Diagnostics.ProcessStartInfo]::new((Join-Path $PSHOME 'powershell.exe'),
        (($arguments | ForEach-Object { Quote-Argument $_ }) -join ' '))
    $info.UseShellExecute = $false; $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true; $info.RedirectStandardError = $true
    $process = [Diagnostics.Process]::new(); $process.StartInfo = $info
    try {
        if (-not $process.Start()) { throw 'Independent verifier did not start.' }
        $out = $process.StandardOutput.ReadToEndAsync(); $err = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(60000)) {
            # This is our verifier helper, never a product process.
            $process.Kill(); $process.WaitForExit(); throw 'Independent verifier timed out.'
        }
        $result = [ordered]@{ arguments = $arguments; exitCode = $process.ExitCode; stdout = $out.Result; stderr = $err.Result }
        if ($process.ExitCode -ne 0) { throw ('Independent verification failed: ' + $err.Result) }
        return $result
    } finally { $process.Dispose() }
}

$selfPath = Assert-SafePath $PSCommandPath
if ($Action -eq 'Prepare') {
    if (-not $SourcePackage -or -not $ExpectedManifestSha256 -or $Registration -or $Target) {
        throw 'Prepare requires SourcePackage and its external manifest SHA; registration/target are generated.'
    }
    $source = Assert-SafePath $SourcePackage
    $scope = Assert-SafePath $ScopeParent
    if (-not [IO.Directory]::Exists($source) -or (Test-Path -LiteralPath $scope) -or
        $scope -ieq $source -or $scope.StartsWith($source + '\', [StringComparison]::OrdinalIgnoreCase) -or
        $source.StartsWith($scope + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Prepare requires a new scope outside the source package.'
    }
    $parent = Assert-SafePath ([IO.Path]::GetDirectoryName($scope))
    if (-not [IO.Directory]::Exists($parent)) { throw 'Scope parent must already exist.' }
    $baseline = $null
    if ($BaselineDataRoot) {
        $baseline = Assert-SafePath $BaselineDataRoot
        if ($baseline -ieq $scope -or $scope.StartsWith($baseline + '\', [StringComparison]::OrdinalIgnoreCase) -or
            $baseline.StartsWith($scope + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Scope/baseline overlap refused.' }
    }
    $anchor = $ExpectedManifestSha256.ToLowerInvariant()
    $selfSha = Get-Sha $selfPath
    if ((Get-Sha (Join-Path $source 'verification/preview-acl.ps1')) -cne $selfSha) {
        throw 'Prepare must use the original ACL verification source matching this package.'
    }
    $verification = Invoke-SourceVerification $source $anchor
    if ((Get-Sha (Join-Path $source 'build-manifest.json')) -cne $anchor) { throw 'Manifest changed after verification.' }
    $original = Get-Snapshot $source
    $manifest = Read-Json (Join-Path $source 'build-manifest.json')
    if ($original.Count -ne $manifest.files.Count + 2) { throw 'Verified source inventory changed.' }
    foreach ($file in $manifest.files) {
        if (-not $original.Contains($file.path) -or $original[$file.path].bytes -ne $file.bytes -or
            $original[$file.path].sha256 -cne $file.sha256.ToLowerInvariant()) {
            throw 'Source payload changed after independent verification.'
        }
    }
    $baselineBefore = if ($baseline) { Get-Snapshot $baseline -DataOnly } else { $null }
    [void][IO.Directory]::CreateDirectory($scope)
    $incomplete = Join-Path $scope 'prepare-incomplete.json'
    Write-NewJson $incomplete @{ schema = 'cordis.preview.acl-prepare-incomplete/1'; startedUtc = [DateTime]::UtcNow.ToString('o') }
    $prepared = [ordered]@{ schema = 'cordis.preview.acl-preparation/1'; passed = $false; preparationOnly = $true
        protectionApplied = $false; aclScopeParent = $scope; sourcePackage = $source; manifestSha256 = $anchor
        sourceBefore = $original; sourceVerification = $verification; baselineBefore = $baselineBefore }
    try {
        $app = Join-Path $scope 'acl/application'; $data = Join-Path $scope 'acl/data'
        [void][IO.Directory]::CreateDirectory((Join-Path $scope 'acl'))
        Copy-Tree $source $app $original
        $prepared.copyVerification = Invoke-SourceVerification $app $anchor
        if ($baseline) { Copy-Tree $baseline $data $baselineBefore -DataOnly }
        else { [void][IO.Directory]::CreateDirectory((Join-Path $data 'store')) }
        $targets = [ordered]@{}
        foreach ($pair in @(@('application', $app), @('data', $data))) {
            $targets[$pair[0]] = [ordered]@{ relativePath = 'acl/' + $pair[0]; identity = Get-Identity $pair[1]
                initialFiles = Get-Snapshot $pair[1] }
        }
        $registrationPath = Join-Path $scope 'acl-registration.json'
        $registrationValue = [ordered]@{ schema = 'cordis.preview.acl-registration/1'; createdUtc = [DateTime]::UtcNow.ToString('o')
            scopeParent = $scope; scopeIdentity = Get-Identity $scope
            createdBy = @{ path = $selfPath; sha256 = $selfSha }; targets = $targets }
        Assert-Snapshot (Get-Snapshot $source) $original
        if ($baseline) { Assert-Snapshot (Get-Snapshot $baseline -DataOnly) $baselineBefore }
        if ((Get-Sha $selfPath) -cne $selfSha -or (Get-Sha (Join-Path $source 'build-manifest.json')) -cne $anchor) {
            throw 'Original verification source changed during Prepare.'
        }
        Write-NewJson $registrationPath $registrationValue
        $prepared.registration = $registrationPath; $prepared.application = $app; $prepared.data = $data
        $prepared.sourceAfter = Get-Snapshot $source
        $prepared.baselineAfter = if ($baseline) { Get-Snapshot $baseline -DataOnly } else { $null }
        $prepared.passed = $true
    } catch { $prepared.error = $_.Exception.Message }
    Write-NewJson (Join-Path $scope 'prepare-report.json') $prepared
    if ($prepared.passed) { [IO.File]::Delete($incomplete) }
    $prepared | ConvertTo-Json -Depth 20
    if (-not $prepared.passed) { exit 1 }
    exit 0
}

function Test-Access([string]$Root, $Objects) {
    # The random new file is removed only if this very probe created it.
    $probe = Join-Path $Root ('.__cordis_acl_probe_' + [guid]::NewGuid().ToString('N'))
    $created = $false
    $createDenied = $false
    try {
        $stream = [IO.File]::Open($probe, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
        $created = $true
        $stream.Dispose()
    } catch [System.UnauthorizedAccessException] {
        $createDenied = $true
    } finally {
        if ($created) { Remove-Item -LiteralPath $probe -Force }
    }
    $files = @($Objects | Where-Object { -not $_.PSIsContainer })
    if ($files.Count -eq 0) { throw 'ACL target requires an existing file for write/delete access probes.' }
    $file = $files[0].FullName
    $writeError = [CordisPreviewAclNative]::OpenAccess($file, 0x00000002)
    $deleteError = [CordisPreviewAclNative]::OpenAccess($file, 0x00010000)
    return [ordered]@{
        createNewDenied = $createDenied
        existingFile = $file
        writeDataError = $writeError
        deleteAccessError = $deleteError
        deleteProbeUnlinksFile = $false
        passed = ($createDenied -and $writeError -eq 5 -and $deleteError -eq 5)
    }
}

$scope = Assert-SafePath $ScopeParent
if (-not $Registration -or -not $Target -or $SourcePackage -or $ExpectedManifestSha256 -or $BaselineDataRoot) {
    throw 'Protect/Probe/Restore require the generated registration and explicit target only.'
}
$registrationPath = Assert-SafePath $Registration
if (-not [IO.Directory]::Exists($scope) -or
    $registrationPath -ine (Join-Path $scope 'acl-registration.json')) {
    throw 'Registration must be the generated file in the exact scope parent.'
}
$registered = Read-Json $registrationPath
if ($registered.schema -cne 'cordis.preview.acl-registration/1' -or $registered.scopeParent -ine $scope) {
    throw 'ACL registration schema or scope differs.'
}
Assert-Identity $scope $registered.scopeIdentity
$creator = Assert-SafePath $registered.createdBy.path
$expectedCreator = $selfPath
if ($creator -ine $expectedCreator -or
    ($Action -ne 'Restore' -and (Get-Sha $creator) -cne $registered.createdBy.sha256)) {
    throw 'ACL registration was not made by the current copy tool.'
}
# Restore retains the original registration/token/object ownership chain even if
# this same source script was repaired. A new script digest must not strand an
# owned read-only copy; no alternate producer path or arbitrary target is allowed.
$entry = $registered.targets.$Target
$expectedRelative = 'acl/' + $Target
if ($entry.relativePath -cne $expectedRelative) { throw 'Only the generated application/data target is accepted.' }
$root = Assert-SafePath (Join-Path $scope ($expectedRelative.Replace('/', '\')))
if (-not $root.StartsWith($scope + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'ACL target leaves its scope.' }
Assert-Identity $root $entry.identity
$objects = @(Get-TargetObjects $root)
if (@($objects | Where-Object { -not $_.PSIsContainer }).Count -eq 0 -and $Action -ne 'Restore') {
    throw 'ACL target requires an existing file before any protection is changed.'
}
$statePath = Join-Path $scope ('acl-state-' + $Target + '.json')
$sid = [Security.Principal.WindowsIdentity]::GetCurrent().User
$report = [ordered]@{
    schema = 'cordis.preview.acl-observation/1'; action = $Action; target = $root
    scopeParent = $scope; tokenSid = $sid.Value; utc = [DateTime]::UtcNow.ToString('o'); passed = $false
    registrationSha256 = Get-Sha $registrationPath
}

if ($Action -eq 'Protect') {
    if (Test-Path -LiteralPath $statePath) { throw 'An ACL state already exists; restore it rather than replacing it.' }
    $saved = @()
    foreach ($item in $objects) {
        $saved += [ordered]@{
            relativePath = $item.FullName.Substring($root.Length).TrimStart('\').Replace('\', '/')
            identity = Get-Identity $item.FullName
            directory = [bool]$item.PSIsContainer
            sddl = (Get-ObjectAcl $item.FullName).GetSecurityDescriptorSddlForm(
                [Security.AccessControl.AccessControlSections]::Access -bor
                [Security.AccessControl.AccessControlSections]::Owner -bor
                [Security.AccessControl.AccessControlSections]::Group)
        }
    }
    Write-NewJson $statePath ([ordered]@{ schema = 'cordis.preview.acl-state/1'; target = $root
        registrationSha256 = $report.registrationSha256; tokenSid = $sid.Value; objects = $saved })
    foreach ($item in ($objects | Sort-Object { $_.FullName.Length } -Descending)) {
        $acl = Get-ObjectAcl $item.FullName
        $acl.SetAccessRuleProtection($true, $false)
        foreach ($rule in @($acl.Access)) { [void]$acl.RemoveAccessRuleSpecific($rule) }
        $inherit = [Security.AccessControl.InheritanceFlags]::None
        if ($item.PSIsContainer) {
            $inherit = [Security.AccessControl.InheritanceFlags]::ContainerInherit -bor
                       [Security.AccessControl.InheritanceFlags]::ObjectInherit
        }
        foreach ($grant in @(@($sid, [Security.AccessControl.FileSystemRights]::ReadAndExecute),
                             @([Security.Principal.SecurityIdentifier]::new('S-1-5-18'), [Security.AccessControl.FileSystemRights]::FullControl),
                             @([Security.Principal.SecurityIdentifier]::new('S-1-5-32-544'), [Security.AccessControl.FileSystemRights]::FullControl))) {
            $rule = [Security.AccessControl.FileSystemAccessRule]::new($grant[0], $grant[1], $inherit,
                [Security.AccessControl.PropagationFlags]::None, [Security.AccessControl.AccessControlType]::Allow)
            [void]$acl.AddAccessRule($rule)
        }
        Set-ObjectAcl $item.FullName $acl
    }
    $report.probe = Test-Access $root $objects
    $report.passed = $report.probe.passed
    $report.statePath = $statePath
} elseif ($Action -eq 'Probe') {
    if (-not (Test-Path -LiteralPath $statePath)) { throw 'The generated target has not been protected.' }
    $report.probe = Test-Access $root $objects
    $report.passed = $report.probe.passed
} else {
    $state = Read-Json $statePath
    if ($state.schema -cne 'cordis.preview.acl-state/1' -or $state.target -ine $root -or
        $state.registrationSha256 -cne $report.registrationSha256 -or $state.tokenSid -cne $sid.Value) {
        throw 'ACL state does not belong to this registration and token.'
    }
    $expected = @($state.objects)
    if ($expected.Count -ne $objects.Count) { throw 'Target object set changed; retained original ACL state.' }
    $seenPaths = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $seenObjects = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($row in $expected) {
        if ($row.relativePath -match '(^|/)[.]{1,2}(/|$)|[\\:]' -or $row.relativePath.StartsWith('/')) {
            throw 'Unsafe ACL state path.'
        }
        if (-not $seenPaths.Add([string]$row.relativePath) -or
            -not $seenObjects.Add(([string]$row.identity.volume + ':' + [string]$row.identity.index))) {
            throw 'Duplicate path/object in ACL state; nothing was restored.'
        }
        $path = Assert-SafePath (Join-Path $root $row.relativePath.Replace('/', '\'))
        Assert-Identity $path $row.identity
        if ($row.directory -isnot [bool] -or [bool][IO.Directory]::Exists($path) -ne $row.directory -or
            $row.sddl -isnot [string]) { throw 'Invalid ACL state object type.' }
        [void][Security.AccessControl.RawSecurityDescriptor]::new($row.sddl)
    }
    foreach ($row in ($expected | Sort-Object { $_.relativePath.Length })) {
        $path = Join-Path $root $row.relativePath.Replace('/', '\')
        $acl = Get-ObjectAcl $path
        # Restore only the DACL we changed; touching the SACL would require SeSecurityPrivilege.
        $acl.SetSecurityDescriptorSddlForm($row.sddl, [Security.AccessControl.AccessControlSections]::Access)
        Set-ObjectAcl $path $acl
    }
    $restored = $true
    $report.comparison = [ordered]@{
        rule = 'OwnerGroupOrderedDaclBinaryAllFlagsExceptDaclAutoInherited/1'
        allowedControlFlagsDifference = 0x0400; objectCount = $expected.Count
        normalizedObjectCount = 0; mismatches = @()
    }
    foreach ($row in $expected) {
        $actualSddl = (Get-ObjectAcl (Join-Path $root $row.relativePath.Replace('/', '\'))).GetSecurityDescriptorSddlForm(
            [Security.AccessControl.AccessControlSections]::Access -bor
            [Security.AccessControl.AccessControlSections]::Owner -bor
            [Security.AccessControl.AccessControlSections]::Group)
        $comparison = Compare-PreviewRestoredAcl ([Security.AccessControl.RawSecurityDescriptor]::new($row.sddl)) `
            ([Security.AccessControl.RawSecurityDescriptor]::new($actualSddl))
        if ($comparison.autoInheritedNormalized) { $report.comparison.normalizedObjectCount++ }
        if (-not $comparison.passed) {
            $restored = $false
            $report.comparison.mismatches += [ordered]@{
                relativePath = $row.relativePath; comparison = $comparison
                expectedSddl = $row.sddl; actualSddl = $actualSddl
            }
        }
    }
    $report.passed = $restored
    $report.statePath = $statePath
}
$receiptPath = Join-Path $scope ('acl-' + $Action.ToLowerInvariant() + '-' + $Target + '-' + [guid]::NewGuid().ToString('N') + '.json')
Write-NewJson $receiptPath $report
$report | ConvertTo-Json -Depth 20
if (-not $report.passed) { exit 1 }
