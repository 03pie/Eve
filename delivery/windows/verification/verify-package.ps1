<#
.SYNOPSIS
Verify an extracted internal preview package without loading its libraries.
.DESCRIPTION
Checks the manifest, optional external manifest anchor, exact file inventory and
SHA256SUMS. Consistency does not establish publisher trust or authorize plugins.
The optional report must be outside the package and must not already exist.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$PackageRoot,
    [string]$ExpectedManifestSha256,
    [string]$ReportPath
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

function Read-StrictUtf8([string]$Path) {
    if ((Get-Item -LiteralPath $Path -Force).Length -gt 8388608) {
        throw 'Package metadata exceeds the 8 MiB verification limit.'
    }
    $encoding = [System.Text.UTF8Encoding]::new($false, $true)
    return $encoding.GetString([System.IO.File]::ReadAllBytes($Path))
}

function Expect-JsonToken($State, [string]$Token) {
    if ($State.Position -ge $State.Tokens.Count -or $State.Tokens[$State.Position] -cne $Token) {
        throw 'Invalid JSON structure in build-manifest.json.'
    }
    $State.Position += 1
}

function Read-JsonValue($State, [int]$Depth) {
    if ($Depth -gt 64 -or $State.Position -ge $State.Tokens.Count) {
        throw 'Invalid or excessively nested package JSON.'
    }
    $token = $State.Tokens[$State.Position]
    $State.Position += 1
    if ($token -ceq '{') {
        $keys = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
        if ($State.Position -lt $State.Tokens.Count -and $State.Tokens[$State.Position] -ceq '}') {
            $State.Position += 1
            return
        }
        while ($true) {
            if ($State.Position -ge $State.Tokens.Count -or -not $State.Tokens[$State.Position].StartsWith('"')) {
                throw 'Expected a JSON object key.'
            }
            $keyToken = $State.Tokens[$State.Position]
            $State.Position += 1
            # Decode escapes before comparing keys; PowerShell objects are case insensitive.
            $decoded = @(ConvertFrom-Json -InputObject ('[' + $keyToken + ']') -ErrorAction Stop)
            if (-not $keys.Add([string]$decoded[0])) {
                throw 'Duplicate or case-colliding JSON object key.'
            }
            Expect-JsonToken $State ':'
            Read-JsonValue $State ($Depth + 1)
            if ($State.Position -lt $State.Tokens.Count -and $State.Tokens[$State.Position] -ceq ',') {
                $State.Position += 1
                continue
            }
            Expect-JsonToken $State '}'
            return
        }
    }
    if ($token -ceq '[') {
        if ($State.Position -lt $State.Tokens.Count -and $State.Tokens[$State.Position] -ceq ']') {
            $State.Position += 1
            return
        }
        while ($true) {
            Read-JsonValue $State ($Depth + 1)
            if ($State.Position -lt $State.Tokens.Count -and $State.Tokens[$State.Position] -ceq ',') {
                $State.Position += 1
                continue
            }
            Expect-JsonToken $State ']'
            return
        }
    }
    if (-not ($token.StartsWith('"') -or $token -cmatch '^(true|false|null|-?[0-9])')) {
        throw 'Invalid JSON value.'
    }
}

function Read-PackageManifest([string]$Path) {
    $text = Read-StrictUtf8 $Path
    if ($text.StartsWith([string][char]0xfeff, [System.StringComparison]::Ordinal)) { $text = $text.Substring(1) }
    $lexer = [regex]::new('\G(?:[ \t\r\n]+|"(?:[^"\\\x00-\x1f]|\\(?:["\\/bfnrt]|u[0-9a-fA-F]{4}))*"|-?(?:0|[1-9][0-9]*)(?:\.[0-9]+)?(?:[eE][+-]?[0-9]+)?|true|false|null|[{}\[\]:,])')
    $tokens = [System.Collections.Generic.List[string]]::new()
    $position = 0
    while ($position -lt $text.Length) {
        $match = $lexer.Match($text, $position)
        if (-not $match.Success -or $match.Index -ne $position) { throw 'Invalid package JSON token.' }
        if (-not [char]::IsWhiteSpace($match.Value[0])) { $tokens.Add($match.Value) }
        $position += $match.Length
    }
    $state = @{ Tokens = $tokens; Position = 0 }
    Read-JsonValue $state 0
    if ($state.Position -ne $tokens.Count) { throw 'Unexpected trailing JSON content.' }
    return ConvertFrom-Json -InputObject $text -ErrorAction Stop
}

function Require-Properties($Value, [string[]]$Names) {
    if ($null -eq $Value -or $Value -isnot [System.Management.Automation.PSCustomObject]) {
        throw 'Expected a package metadata object.'
    }
    foreach ($name in $Names) {
        if (@($Value.PSObject.Properties.Name) -cnotcontains $name) {
            throw ('Missing package metadata field: ' + $name)
        }
    }
}

function Assert-RelativePath($Value) {
    if ($Value -isnot [string] -or [string]::IsNullOrEmpty($Value) -or
        $Value -match '[\\:\x00-\x1f\x7f<>"|?*]' -or $Value.StartsWith('/') -or $Value.EndsWith('/')) {
        throw 'Invalid relative POSIX package path.'
    }
    foreach ($part in $Value.Split('/')) {
        if ($part -eq '' -or $part -eq '.' -or $part -eq '..' -or $part -match '[ .]$' -or
            $part -match '^(?i:CON|PRN|AUX|NUL|COM[1-9¹²³]|LPT[1-9¹²³])(?:\.|$)') {
            throw ('Unsafe package path: ' + $Value)
        }
    }
}

function Get-FileSha256([string]$Path) {
    $stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Open,
        [System.IO.FileAccess]::Read, [System.IO.FileShare]::Read)
    $algorithm = [System.Security.Cryptography.SHA256]::Create()
    try { return [System.BitConverter]::ToString($algorithm.ComputeHash($stream)).Replace('-', '').ToLowerInvariant() }
    finally { $algorithm.Dispose(); $stream.Dispose() }
}

function Assert-OrdinaryDirectory([System.IO.DirectoryInfo]$Directory) {
    $ancestor = $Directory
    while ($null -ne $ancestor) {
        if (($ancestor.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw 'Directory or ancestor is a reparse point.'
        }
        $ancestor = $ancestor.Parent
    }
}

function Get-PackageFiles([System.IO.DirectoryInfo]$Root) {
    # Check ancestors too: a junction in PackageRoot itself must not hide an escape.
    Assert-OrdinaryDirectory $Root
    $found = [System.Collections.Generic.Dictionary[string,object]]::new([System.StringComparer]::OrdinalIgnoreCase)
    $directories = [System.Collections.Generic.Queue[System.IO.DirectoryInfo]]::new()
    $directories.Enqueue($Root)
    $prefix = $Root.FullName.TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
    while ($directories.Count -ne 0) {
        $directory = $directories.Dequeue()
        foreach ($item in @(Get-ChildItem -LiteralPath $directory.FullName -Force)) {
            if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw 'Package contains a file or directory reparse point.'
            }
            if ($item.PSIsContainer) { $directories.Enqueue($item); continue }
            $relative = $item.FullName.Substring($prefix.Length).Replace('\', '/')
            Assert-RelativePath $relative
            if ($found.ContainsKey($relative)) { throw 'Case-colliding package file paths.' }
            $found.Add($relative, @{ Path = $relative; File = $item })
        }
    }
    return ,$found
}

$result = [ordered]@{ schema = 'cordis.preview.verification/1'; verified = $false;
    integrityOnly = $true; externalManifestAnchorProvided = [bool]$ExpectedManifestSha256;
    payloadFiles = 0; payloadBytes = 0; error = $null }
$exitCode = 1
$rootPath = $null
try {
    $rootPath = [System.IO.Path]::GetFullPath($PackageRoot)
    $root = Get-Item -LiteralPath $rootPath -Force
    if ($root -isnot [System.IO.DirectoryInfo]) { throw 'PackageRoot must be an existing directory.' }
    $rootPath = $root.FullName
    $actual = Get-PackageFiles $root
    foreach ($required in @('build-manifest.json', 'SHA256SUMS.txt')) {
        if (-not $actual.ContainsKey($required) -or $actual[$required].Path -cne $required) {
            throw ('Missing package file: ' + $required)
        }
    }
    $manifestPath = $actual['build-manifest.json'].File.FullName
    $manifestSha = Get-FileSha256 $manifestPath
    $result['manifestSha256'] = $manifestSha
    if ($ExpectedManifestSha256) {
        if ($ExpectedManifestSha256 -notmatch '^[0-9a-fA-F]{64}$' -or
            $manifestSha -cne $ExpectedManifestSha256.ToLowerInvariant()) { throw 'External manifest SHA256 anchor mismatch.' }
    }
    $manifest = Read-PackageManifest $manifestPath
    Require-Properties $manifest @('schema', 'deliveryVersion', 'applicationVersion', 'scope', 'platform', 'profile', 'qualification', 'files')
    if ($manifest.schema -isnot [string] -or $manifest.scope -isnot [string] -or $manifest.platform -isnot [string] -or
        $manifest.schema -cne 'cordis.preview.package/1' -or
        $manifest.scope -cne 'internal-technical-preview' -or $manifest.platform -cne 'windows-x64') {
        throw 'Unsupported package schema, scope or platform.'
    }
    foreach ($name in @('deliveryVersion', 'applicationVersion')) {
        if ($manifest.$name -isnot [string] -or [string]::IsNullOrWhiteSpace($manifest.$name)) { throw 'Invalid package version.' }
    }
    Require-Properties $manifest.profile @()
    Require-Properties $manifest.qualification @()
    if ($manifest.files -isnot [System.Array]) { throw 'Package files must be an array.' }
    $payload = [System.Collections.Generic.Dictionary[string,object]]::new([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($file in $manifest.files) {
        Require-Properties $file @('path', 'role', 'bytes', 'sha256', 'source')
        Assert-RelativePath $file.path
        if ($file.path -ieq 'build-manifest.json' -or $file.path -ieq 'SHA256SUMS.txt' -or $payload.ContainsKey($file.path)) {
            throw 'Reserved or duplicate package file path.'
        }
        if ($file.role -isnot [string] -or [string]::IsNullOrWhiteSpace($file.role) -or $null -eq $file.source -or
            ($file.bytes -isnot [int] -and $file.bytes -isnot [long]) -or $file.bytes -lt 0 -or
            $file.sha256 -isnot [string] -or $file.sha256 -notmatch '^[0-9a-fA-F]{64}$') { throw 'Invalid package file metadata.' }
        Require-Properties $file.source @('path', 'sha256')
        if ($file.source.path -isnot [string] -or [string]::IsNullOrWhiteSpace($file.source.path) -or
            $file.source.sha256 -isnot [string] -or $file.source.sha256 -notmatch '^[0-9a-fA-F]{64}$' -or
            $file.source.sha256.ToLowerInvariant() -cne $file.sha256.ToLowerInvariant()) {
            throw 'Invalid same-byte source provenance; source paths are not opened.'
        }
        if (-not $actual.ContainsKey($file.path) -or $actual[$file.path].Path -cne $file.path) { throw ('Missing or case-mismatched payload: ' + $file.path) }
        $item = $actual[$file.path].File
        if ($item.Length -ne $file.bytes -or (Get-FileSha256 $item.FullName) -cne $file.sha256.ToLowerInvariant()) {
            throw ('Payload size or SHA256 mismatch: ' + $file.path)
        }
        $payload.Add($file.path, $file)
        $result.payloadBytes += $item.Length
    }
    if ($actual.Count -ne $payload.Count + 2) { throw 'Package contains unlisted files.' }
    $sumsText = Read-StrictUtf8 $actual['SHA256SUMS.txt'].File.FullName
    if ($sumsText.Contains("`r") -or -not $sumsText.EndsWith("`n")) { throw 'SHA256SUMS must contain LF-terminated lines.' }
    $sums = [System.Collections.Generic.Dictionary[string,string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($line in $sumsText.Substring(0, $sumsText.Length - 1).Split("`n")) {
        if ($line -cnotmatch '^([0-9a-f]{64})  (.+)$') { throw 'Invalid SHA256SUMS line.' }
        $digest = $Matches[1]; $path = $Matches[2]
        Assert-RelativePath $path
        if ($sums.ContainsKey($path)) { throw 'Duplicate SHA256SUMS path.' }
        if ($path -ceq 'build-manifest.json') { $expected = $manifestSha }
        elseif ($payload.ContainsKey($path) -and $payload[$path].path -ceq $path) { $expected = $payload[$path].sha256.ToLowerInvariant() }
        else { throw 'SHA256SUMS contains an unlisted path.' }
        if ($digest -cne $expected) { throw ('SHA256SUMS mismatch: ' + $path) }
        $sums.Add($path, $digest)
    }
    if ($sums.Count -ne $payload.Count + 1) { throw 'SHA256SUMS does not cover every payload and manifest.' }
    $result.payloadFiles = $payload.Count
    $result.verified = $true
    $exitCode = 0
}
catch { $result.error = $_.Exception.Message }

if ($ReportPath) {
    try {
        $reportFile = [System.IO.Path]::GetFullPath($ReportPath)
        $prefix = $rootPath.TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
        if ($reportFile.Equals($rootPath, [System.StringComparison]::OrdinalIgnoreCase) -or
            $reportFile.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) { throw 'ReportPath must be outside the package.' }
        $reportDirectory = Get-Item -LiteralPath ([System.IO.Path]::GetDirectoryName($reportFile)) -Force
        if ($reportDirectory -isnot [System.IO.DirectoryInfo]) { throw 'ReportPath parent must be an existing directory.' }
        Assert-OrdinaryDirectory $reportDirectory
        $bytes = [System.Text.UTF8Encoding]::new($false).GetBytes(($result | ConvertTo-Json -Depth 8) + "`n")
        $stream = [System.IO.File]::Open($reportFile, [System.IO.FileMode]::CreateNew, [System.IO.FileAccess]::Write)
        try { $stream.Write($bytes, 0, $bytes.Length) } finally { $stream.Dispose() }
    }
    catch { $result.error = 'Cannot write external verification report: ' + $_.Exception.Message; $exitCode = 1 }
}
if ($exitCode -eq 0) {
    [Console]::WriteLine(('Package integrity verified: {0} payload files. Consistency only; no publisher trust or plugin approval.' -f $result.payloadFiles))
}
else { [Console]::Error.WriteLine(('Package verification failed: ' + $result.error)) }
exit $exitCode
