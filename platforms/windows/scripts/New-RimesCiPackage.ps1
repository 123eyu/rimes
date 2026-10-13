#requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$OutputDirectory,
    [Parameter(Mandatory)][string]$Version
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = 'Stop'
$windowsRoot = Split-Path -Parent $PSScriptRoot
$repo = [IO.Path]::GetFullPath((Join-Path $windowsRoot '../..'))
$commit = (Get-Content -LiteralPath "$repo/SOURCE-COMMIT.txt" -Raw).Trim()
$snapshot = (Get-Content -LiteralPath "$repo/SOURCE-SNAPSHOT.txt" -Raw).Trim()
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Use a new package work directory' }
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null

# Reuse only immutable OpenCC tables and Microsoft-signed redistributables.
# Product binaries, scripts, schemes and dictionaries come from this checkout.
$baselineHash = '9b5711237c3c551f2a68b8de6cfe5d36fa079b1307c7a7cc4b77720876958d23'
$baselineUrl = 'https://github.com/scholay/rimes/releases/download/windows-v1.1.1/RIMES-Windows-1.1.1.zip'
$baselineZip = Join-Path $OutputDirectory 'dependencies.zip'
Invoke-WebRequest -Uri $baselineUrl -OutFile $baselineZip -UseBasicParsing
if ((Get-FileHash -LiteralPath $baselineZip -Algorithm SHA256).Hash.ToLowerInvariant() -ne $baselineHash) {
    throw 'Dependency archive checksum failed'
}
$baseline = Join-Path $OutputDirectory 'dependencies'
Expand-Archive -LiteralPath $baselineZip -DestinationPath $baseline
$dataManifest = Get-Content -LiteralPath "$baseline/x64/shared/NATIVE-DATA-MANIFEST.json" -Raw -Encoding UTF8 | ConvertFrom-Json
$shared = Join-Path $OutputDirectory 'shared'
python "$PSScriptRoot/prepare-native-data.py" stage --output $shared `
    --opencc-data "$baseline/x64/shared/opencc" `
    --opencc-license "$baseline/x64/shared/LICENSE-OpenCC.txt" `
    --opencc-revision $dataManifest.opencc.revision
if ($LASTEXITCODE -ne 0) { throw 'Complete product data staging failed' }

$runtimes = @{}
foreach ($arch in @('x64', 'x86')) {
    $bin = Join-Path $windowsRoot "native/out/build/windows-$arch/Release"
    $identity = Get-Content -LiteralPath "$bin/build-identity.json" -Raw | ConvertFrom-Json
    if ($identity.commit -ne $commit -or $identity.sourceSnapshot -ne $snapshot) {
        throw "Architecture source identity mismatch: $arch"
    }
    $runtime = & "$PSScriptRoot/Fetch-RimesLibrime.ps1" -Architecture $arch -OutputDirectory "$OutputDirectory/librime-$arch"
    $runtimes[$arch] = $runtime
    & "$bin/RimesProductProbe.exe" $runtime.DllPath $shared "$OutputDirectory/product-user-$arch" "$OutputDirectory/product-logs-$arch"
    if ($LASTEXITCODE -ne 0) { throw "Full product scheme probe failed: $arch" }
}

$package = & "$PSScriptRoot/New-RimesNativePackage.ps1" -Commit $commit -Version $Version `
    -SharedData $shared -RimeDll $runtimes['x64'].DllPath -OutputDirectory "$OutputDirectory/package" `
    -VCRedistX64 "$baseline/runtimes/vc_redist.x64.exe" -VCRedistX86 "$baseline/runtimes/vc_redist.x86.exe"
$delivery = Join-Path $OutputDirectory 'delivery'
New-Item -ItemType Directory -Path $delivery | Out-Null
$verificationPath = Join-Path $delivery 'SETUP-VERIFICATION.json'
$verify = Start-Process -FilePath $package.Setup.Path -ArgumentList @('--verify-only', ('"' + $verificationPath + '"')) -Wait -PassThru
if ($verify.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $verificationPath)) {
    throw 'Final EXE payload verification failed'
}
$verification = Get-Content -LiteralPath $verificationPath -Raw | ConvertFrom-Json
if ($verification.setupSHA256 -ne $package.Setup.SHA256 -or $verification.payloadSHA256 -ne $package.SHA256) {
    throw 'Final EXE verification returned a different artifact'
}

# Test the production Broker and runtime actually selected for the installer.
$broker = Join-Path $package.Staging 'x64/RimesBroker.exe'
& $broker --scheme-import-smoke "$OutputDirectory/packaged-text-import"
if ($LASTEXITCODE -ne 0) { throw 'Packaged text-table import failed' }
$fixture = Join-Path $OutputDirectory 'wanxiang-original.zip'
python "$repo/scripts/input-schemes/fetch_fixture.py" $fixture
if ($LASTEXITCODE -ne 0) { throw 'Original Wanxiang archive checksum failed' }
$expected = ([string][char]0x4f60) + [char]0x597d
foreach ($case in @(@('wanxiang', 'nihao'), @('wanxiang_t9', '64426'))) {
    & $broker --scheme-import-smoke "$OutputDirectory/packaged-$($case[0])" $fixture $case[0] $case[1] $expected
    if ($LASTEXITCODE -ne 0) { throw "Packaged scheme import failed: $($case[0])" }
}

Copy-Item -LiteralPath $package.Setup.Path, $package.Archive, "$repo/SOURCE-FILES.json" -Destination $delivery
Copy-Item -LiteralPath "$repo/docs/portable-scheme-import.md" -Destination "$delivery/SCHEME-IMPORT.md"
$artifacts = [ordered]@{}
$sums = @()
foreach ($path in @($package.Setup.Path, $package.Archive)) {
    $file = Get-Item -LiteralPath $path
    $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    $artifacts[$file.Name] = [ordered]@{sha256=$hash; bytes=$file.Length}
    $sums += "$hash  $($file.Name)"
}
[IO.File]::WriteAllLines("$delivery/SHA256SUMS", $sums, [Text.UTF8Encoding]::new($false))
[ordered]@{
    platform='windows'; version=$Version; sourceCommit=$commit; sourceSnapshot=$snapshot
    sourceManifest='SOURCE-FILES.json'; architectures=@('x64','x86'); brokerArchitecture='x64'; signing='unsigned'
    workflowRun="$env:GITHUB_SERVER_URL/$env:GITHUB_REPOSITORY/actions/runs/$env:GITHUB_RUN_ID"
    dependencies=[ordered]@{archive=$baselineUrl; sha256=$baselineHash; reused=@('OpenCC tables','Microsoft VC++ redistributables')}
    artifacts=$artifacts
    validation=[ordered]@{dualArchitectureCtest='passed'; dualArchitectureTsf='passed'; fullProductData='passed'; finalSetupPayload='passed'; packagedTextImport='passed'; packagedWanxiangPinyin='passed'; packagedWanxiangT9='passed'; desktopAcceptance='pending on maintainer machine'}
} | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath "$delivery/BUILD-INFO.json" -Encoding UTF8
Write-Host "Verified installer delivery: $delivery"
