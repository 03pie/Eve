[CmdletBinding()]
param([Parameter(Mandatory=$true)][ValidatePattern('^[0-9a-f]{64}$')][string]$ExpectedManifestSha256)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath($PSScriptRoot)
$manifestPath=Join-Path $root 'reader-manifest.json'
if ((Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $ExpectedManifestSha256) { throw 'Manifest external SHA differs.' }
$manifest=Get-Content -LiteralPath $manifestPath -Encoding UTF8 -Raw | ConvertFrom-Json
if ($manifest.schema -ne 'cordis.reader.package/1') { throw 'Unsupported reader manifest.' }
$expected=@{}
foreach ($row in $manifest.files) {
    $name=[string]$row.path
    if (!$name -or $name.Contains('\') -or $name.Contains(':') -or $name.StartsWith('/') -or $name -match '[\x00-\x1f<>"|?*]') { throw 'Unsafe file path.' }
    foreach ($part in $name.Split('/')) { if (!$part -or $part -in @('.','..') -or $part -match '[. ]$') { throw 'Unsafe path component.' } }
    if ($expected.ContainsKey($name) -or $name -in @('reader-manifest.json','SHA256SUMS.txt')) { throw 'Duplicate or reserved manifest path.' }
    $expected.Add($name,$row)
}
$observed=@{}
$pending=New-Object 'System.Collections.Generic.Stack[string]'
$pending.Push($root)
while ($pending.Count) {
    $directory=$pending.Pop()
    $item=Get-Item -LiteralPath $directory -Force
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Linked directory is not permitted.' }
    foreach ($item in Get-ChildItem -LiteralPath $directory -Force) {
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Linked entry is not permitted.' }
        if ($item.PSIsContainer) { $pending.Push($item.FullName); continue }
        $name=$item.FullName.Substring($root.Length+1).Replace('\','/')
        if ($name -in @('reader-manifest.json','SHA256SUMS.txt')) { continue }
        if (!$expected.ContainsKey($name) -or $observed.ContainsKey($name)) { throw 'Unexpected file.' }
        $row=$expected[$name]
        if ($item.Length -ne $row.bytes -or (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash.ToLowerInvariant() -ne $row.sha256) { throw "Changed file: $name" }
        $observed.Add($name,$true)
    }
}
if ($observed.Count -ne $expected.Count) { throw 'Missing package files.' }
@{schema='cordis.reader.package-verification/1';passed=$true;manifestSha256=$ExpectedManifestSha256;files=$observed.Count;payloadExecuted=$false} | ConvertTo-Json -Compress
