#requires -Version 5.1
[CmdletBinding()]
param([Parameter(Mandatory)][string]$Commit, [Parameter(Mandatory)][string]$OutputDirectory)
Set-StrictMode -Version 3.0
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Use a fresh package output directory' }
$actual = (& git -C $repo rev-parse HEAD | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or $actual -ne $Commit) { throw 'Package checkout does not match build commit' }
$work = Join-Path $env:RUNNER_TEMP ('rimes-package-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work -Force | Out-Null
function Run-Native([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}

# OpenCC tables are portable data, built from the same locked source as mobile.
$lock = Get-Content -LiteralPath "$repo/platforms/ios/dependencies.lock.json" -Raw | ConvertFrom-Json
$opencc = Join-Path $work 'opencc'
Run-Native 'git' @('init', $opencc)
Run-Native 'git' @('-C', $opencc, 'fetch', '--depth', '1', 'https://github.com/BYVoid/OpenCC.git', $lock.submodules.opencc)
Run-Native 'git' @('-C', $opencc, 'checkout', '--detach', 'FETCH_HEAD')
$openccRevision = (& git -C $opencc rev-parse HEAD | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or $openccRevision -ne $lock.submodules.opencc) { throw 'OpenCC source mismatch' }
$openccPrefix = Join-Path $work 'opencc-install'
Run-Native 'cmake' @('-S', $opencc, '-B', "$work/opencc-build", '-A', 'x64', "-DCMAKE_INSTALL_PREFIX=$openccPrefix", '-DENABLE_GTEST=OFF', '-DBUILD_DOCUMENTATION=OFF', '-DBUILD_SHARED_LIBS=OFF')
Run-Native 'cmake' @('--build', "$work/opencc-build", '--config', 'Release', '--target', 'install', '--parallel', '4')
$shared = Join-Path $work 'shared'
Run-Native 'python' @("$PSScriptRoot/prepare-native-data.py", 'stage', '--opencc-data', "$openccPrefix/share/opencc", '--opencc-license', "$opencc/LICENSE", '--opencc-revision', $openccRevision, '--output', $shared)
$runtime = & "$PSScriptRoot/Fetch-RimesLibrime.ps1" -Architecture x64 -OutputDirectory "$work/librime"
foreach ($arch in @('x64', 'x86')) {
    Invoke-WebRequest -UseBasicParsing -Uri "https://aka.ms/vs/17/release/vc_redist.$arch.exe" -OutFile "$work/vc_redist.$arch.exe"
}
$package = & "$PSScriptRoot/New-RimesNativePackage.ps1" -Commit $Commit -SharedData $shared -RimeDll $runtime.DllPath -OutputDirectory $OutputDirectory -VCRedistX64 "$work/vc_redist.x64.exe" -VCRedistX86 "$work/vc_redist.x86.exe"
$broker = Join-Path $package.Staging 'x64/RimesBroker.exe'
# Use the exact production payload, independently of the E2E Broker artifact.
Run-Native $broker @('--scheme-import-smoke', "$work/text-import")
$fixture = Join-Path $work 'wanxiang-original.zip'
Run-Native 'python' @("$repo/scripts/input-schemes/fetch_fixture.py", $fixture)
$expected = ([string][char]0x4f60) + [char]0x597d
Run-Native $broker @('--scheme-import-smoke', "$work/fullpin-import", $fixture, 'wanxiang', 'nihao', $expected)
Run-Native $broker @('--scheme-import-smoke', "$work/t9-import", $fixture, 'wanxiang_t9', '64426', $expected)
$info = [ordered]@{
    version = $package.Version; commit = $Commit; platform = 'Windows x64 with x64/x86 TSF'
    signing = 'unsigned'; bundledSchemeImport = 'passed'; physicalDesktopAcceptance = 'separate'
    fixtureSHA256 = (Get-FileHash -LiteralPath $fixture -Algorithm SHA256).Hash.ToLowerInvariant()
}
$utf8 = New-Object System.Text.UTF8Encoding($false)
[IO.File]::WriteAllText("$OutputDirectory/BUILD-INFO.json", ($info | ConvertTo-Json -Depth 4), $utf8)
$checksums = @(Get-ChildItem -LiteralPath $OutputDirectory -File | Where-Object { $_.Extension -in @('.exe', '.zip', '.json') } | Sort-Object Name | ForEach-Object {
    (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() + '  ' + $_.Name
})
[IO.File]::WriteAllText("$OutputDirectory/SHA256SUMS", ($checksums -join "`n") + "`n", $utf8)
Write-Host "Packaged $($package.Version) from $Commit; bundled scheme imports passed."
