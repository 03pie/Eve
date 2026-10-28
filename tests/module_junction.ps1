param(
    [Parameter(Mandatory=$true)][string]$InspectExe,
    [Parameter(Mandatory=$true)][string]$TestRoot
)
$ErrorActionPreference = 'Stop'
$testBase = [System.IO.Path]::GetFullPath($TestRoot)
$fixture = Join-Path $testBase ([guid]::NewGuid().ToString('N'))
$pluginRoot = Join-Path $fixture 'plugins'
$outside = Join-Path $fixture 'plugins-other'
$escape = Join-Path $pluginRoot 'escape'
$rootAlias = Join-Path $fixture 'root-alias'
$insideAlias = Join-Path $pluginRoot 'inside-alias'
function Check-Inspect([string]$Root, [string]$Manifest, [int]$Expected) {
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $output = & $InspectExe $Root $Manifest 2>&1
    $code = $LASTEXITCODE
    $ErrorActionPreference = $previous
    if ($code -ne $Expected) { throw "Expected exit $Expected, got $code : $output" }
    if ($Expected -eq 1 -and "$output" -notmatch 'module.path') { throw "Wrong failure stage: $output" }
}
try {
    New-Item -ItemType Directory -Path $pluginRoot,$outside -Force | Out-Null
    $payload = [System.Text.Encoding]::UTF8.GetBytes('abc')
    [System.IO.File]::WriteAllBytes((Join-Path $outside 'example.dll'), $payload)
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try { $hash = [System.BitConverter]::ToString($sha.ComputeHash($payload)).Replace('-','').ToLowerInvariant() }
    finally { $sha.Dispose() }
    $manifest = @{manifestVersion=1;moduleId='org.cordis.example';moduleVersion='1.0.0';loader='c-abi';library='example.dll';sha256=$hash;platform='windows-x64';abi=@{major=1;minor=0};plugins=@('org.cordis.notes');unloadPolicy='resident'} | ConvertTo-Json -Depth 4
    [System.IO.File]::WriteAllText((Join-Path $outside 'module.json'),$manifest,[System.Text.UTF8Encoding]::new($false))
    New-Item -ItemType Junction -Path $escape -Target $outside | Out-Null
    Check-Inspect $pluginRoot 'escape/module.json' 1
    Copy-Item -LiteralPath (Join-Path $outside 'example.dll') -Destination $pluginRoot
    Copy-Item -LiteralPath (Join-Path $outside 'module.json') -Destination $pluginRoot
    New-Item -ItemType Junction -Path $rootAlias -Target $pluginRoot | Out-Null
    Check-Inspect $rootAlias 'module.json' 0
    New-Item -ItemType Junction -Path $insideAlias -Target $pluginRoot | Out-Null
    Check-Inspect $pluginRoot 'inside-alias/module.json' 0
    Write-Output 'Outside junction rejected; root alias and contained junction accepted.'
} finally {
    # Remove links themselves first; never recursively traverse a junction during cleanup.
    foreach ($link in @($insideAlias,$escape,$rootAlias)) {
        if ([System.IO.Directory]::Exists($link)) { [System.IO.Directory]::Delete($link) }
    }
    $resolved = [System.IO.Path]::GetFullPath($fixture)
    if (-not $resolved.StartsWith($testBase.TrimEnd('\')+'\',[System.StringComparison]::OrdinalIgnoreCase)) {
        throw 'Refusing cleanup outside the test root'
    }
    if (Test-Path -LiteralPath $resolved) { Remove-Item -LiteralPath $resolved -Recurse -Force }
}
