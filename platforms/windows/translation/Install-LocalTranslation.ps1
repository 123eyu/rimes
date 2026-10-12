param([Parameter(Mandatory=$true)][string]$Root, [string]$Cache = '')
$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
Add-Type -AssemblyName System.IO.Compression.FileSystem
$Root = [IO.Path]::GetFullPath($Root)
$manifest = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'resources.lock.json') -Raw | ConvertFrom-Json
function File-Sha256([string]$path) {
    $hash = [Security.Cryptography.SHA256]::Create()
    $stream = [IO.File]::OpenRead($path)
    try { return ([BitConverter]::ToString($hash.ComputeHash($stream))).Replace('-', '') }
    finally { $stream.Dispose(); $hash.Dispose() }
}
$identity = File-Sha256 (Join-Path $PSScriptRoot 'resources.lock.json')
$ready = Join-Path $Root 'ready.txt'
if ((Test-Path -LiteralPath $ready) -and (Get-Content -LiteralPath $ready -Raw).Trim() -eq $identity) { return }
$parent = Split-Path -Parent $Root
New-Item -ItemType Directory -Path $parent -Force | Out-Null
$stage = Join-Path $parent ('install-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stage | Out-Null
function Expand-VerifiedZip([string]$archive, [string]$destination) {
    New-Item -ItemType Directory -Path $destination -Force | Out-Null
    $prefix = [IO.Path]::GetFullPath($destination).TrimEnd('\') + '\'
    $zip = [IO.Compression.ZipFile]::OpenRead($archive)
    try {
        foreach ($entry in $zip.Entries) {
            # Wheel test fixtures are not runtime dependencies; some exceed
            # Windows PowerShell 5.1's legacy MAX_PATH extraction limit.
            if ($entry.FullName -match '(^|/)tests/') { continue }
            $path = [IO.Path]::GetFullPath((Join-Path $destination $entry.FullName))
            if (!$path.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe archive path' }
            if (!$entry.Name) { continue }
            New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force | Out-Null
            [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $path, $false)
        }
    } finally { $zip.Dispose() }
}
try {
    foreach ($item in $manifest.resources) {
        $archive = Join-Path $stage $item.file
        if ($Cache) { Copy-Item -LiteralPath (Join-Path $Cache $item.file) -Destination $archive }
        else {
            $client = New-Object Net.WebClient
            try {
                $client.Headers['User-Agent'] = 'RIMES/1.1 offline translator (Mozilla/5.0)'
                $client.DownloadFile($item.url, $archive)
            } finally { $client.Dispose() }
        }
        if ((Get-Item -LiteralPath $archive).Length -ne $item.bytes -or
            (File-Sha256 $archive) -ne $item.sha256) { throw 'Translation resource checksum mismatch' }
        $destination = switch ($item.kind) {
            'python' { Join-Path $stage 'python' }
            'wheel' { Join-Path $stage 'python/site-packages' }
            'model' { Join-Path $stage 'models' }
            default { throw 'Unknown resource type' }
        }
        Expand-VerifiedZip $archive $destination
        Remove-Item -LiteralPath $archive
    }
    # Isolated embedded Python: never import packages or customizations from the host.
    Set-Content -LiteralPath (Join-Path $stage 'python/python312._pth') -Encoding ASCII -Value "python312.zip`n.`nsite-packages"
    & (Join-Path $stage 'python/python.exe') -I -W ignore::UserWarning -c 'import ctranslate2, sentencepiece, numpy'
    if ($LASTEXITCODE -ne 0) { throw 'Local translation runtime self-check failed' }
    Set-Content -LiteralPath (Join-Path $stage 'ready.txt') -Encoding ASCII -Value $identity
    if (Test-Path -LiteralPath $Root) {
        # Preserve an older/broken install for diagnosis; never erase user data.
        Move-Item -LiteralPath $Root -Destination ($Root + '.previous-' + [Guid]::NewGuid().ToString('N'))
    }
    Move-Item -LiteralPath $stage -Destination $Root
} finally {
    $resolvedStage = [IO.Path]::GetFullPath($stage)
    $allowed = [IO.Path]::GetFullPath($parent).TrimEnd('\') + '\install-'
    if ($resolvedStage.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase) -and (Test-Path -LiteralPath $resolvedStage)) {
        Remove-Item -LiteralPath $resolvedStage -Recurse -Force
    }
}
