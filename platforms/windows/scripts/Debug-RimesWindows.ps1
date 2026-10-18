#requires -Version 5.1
[CmdletBinding()]
param(
    [ValidateSet('x64', 'x86')]
    [string]$Architecture = 'x64',

    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',

    [string]$ArtifactDirectory,
    [switch]$Registered,
    [switch]$Json
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$nativeRoot = Join-Path $repoRoot 'platforms\windows\native'
$checks = New-Object System.Collections.Generic.List[object]
$expectedProfileGuid = '{CD791B35-640F-4F1C-A3D3-624099E15ACB}'
$expectedClsid = '{0B2C570B-9811-45DF-989B-EA306281F6B4}'

function Add-DiagnosticCheck {
    param(
        [Parameter(Mandatory = $true)] [string]$Name,
        [Parameter(Mandatory = $true)] [ValidateSet('PASS', 'WARN', 'FAIL')] [string]$Status,
        [Parameter(Mandatory = $true)] [string]$Detail
    )

    [void]$checks.Add([pscustomobject]@{
        Name = $Name
        Status = $Status
        Detail = $Detail
    })
}

function Test-File {
    param(
        [Parameter(Mandatory = $true)] [string]$Path,
        [Parameter(Mandatory = $true)] [string]$Name,
        [switch]$Required
    )

    if (Test-Path -LiteralPath $Path -PathType Leaf) {
        Add-DiagnosticCheck $Name 'PASS' $Path
        return $true
    }

    Add-DiagnosticCheck $Name $(if ($Required) { 'FAIL' } else { 'WARN' }) "Missing: $Path"
    return $false
}

function Get-ExpectedLanguageBarIconIndex {
    $registrarPath = Join-Path $nativeRoot 'src\registrar\main.cpp'
    $source = Get-Content -LiteralPath $registrarPath -Raw
    $match = [regex]::Match($source, 'kLanguageProfileIconIndex\s*=\s*(\d+)')
    if (-not $match.Success) {
        throw "Cannot find kLanguageProfileIconIndex in $registrarPath"
    }
    return [int]$match.Groups[1].Value
}

function Get-RimesLanguageProfileRegistryKey {
    $tipRoots = @(
        'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\CTF\TIP',
        'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\Microsoft\CTF\TIP'
    )

    foreach ($tipRoot in $tipRoots) {
        $clsidPath = Join-Path $tipRoot $expectedClsid
        $languageRoot = Join-Path $clsidPath 'LanguageProfile'
        if (-not (Test-Path -LiteralPath $languageRoot -PathType Container)) {
            continue
        }

        foreach ($key in @(Get-ChildItem -LiteralPath $languageRoot -Recurse -ErrorAction SilentlyContinue)) {
            if ($key.PSChildName.Equals($expectedProfileGuid, [System.StringComparison]::OrdinalIgnoreCase)) {
                return $key
            }
        }
    }

    return $null
}

function Get-RegistryValue {
    param(
        [Parameter(Mandatory = $true)] [object]$Properties,
        [Parameter(Mandatory = $true)] [string[]]$Names
    )

    foreach ($name in $Names) {
        $property = $Properties.PSObject.Properties[$name]
        if ($null -ne $property) {
            return $property.Value
        }
    }
    return $null
}

$rcPath = Join-Path $nativeRoot 'src\product_version.rc.in'
$registrarPath = Join-Path $nativeRoot 'src\registrar\main.cpp'
$iconHeaderPath = Join-Path $nativeRoot 'src\ui\product_icon.h'
$rc = Get-Content -LiteralPath $rcPath -Raw
$registrar = Get-Content -LiteralPath $registrarPath -Raw
$iconHeader = Get-Content -LiteralPath $iconHeaderPath -Raw
$expectedIconIndex = Get-ExpectedLanguageBarIconIndex

$fullMascotPosition = $rc.IndexOf('IDI_RIMES ICON')
$blackHornPosition = $rc.IndexOf('IDI_RIMES_HORN_BLACK ICON')
$whiteHornPosition = $rc.IndexOf('IDI_RIMES_HORN_WHITE ICON')
$contrastHornPosition = $rc.IndexOf('IDI_RIMES_HORN_CONTRAST ICON')
if ($fullMascotPosition -ge 0 -and $blackHornPosition -gt $fullMascotPosition -and
    $whiteHornPosition -gt $blackHornPosition -and $contrastHornPosition -gt $whiteHornPosition) {
    Add-DiagnosticCheck 'resource icon order' 'PASS' 'mascot, black horn, white horn, contrast horn'
} else {
    Add-DiagnosticCheck 'resource icon order' 'FAIL' 'product_version.rc.in icon groups are not in the expected order'
}

if ($expectedIconIndex -eq 3 -and
    $registrar -match 'static_cast<ULONG>\(dll_path\.size\(\)\),\s*kLanguageProfileIconIndex\)') {
    Add-DiagnosticCheck 'TSF language-bar wiring' 'PASS' "AddLanguageProfile selects icon index $expectedIconIndex"
} else {
    Add-DiagnosticCheck 'TSF language-bar wiring' 'FAIL' 'AddLanguageProfile must select the contrast horn at index 3'
}

foreach ($icon in @(
    @{ Name = 'full mascot ICO'; Path = Join-Path $nativeRoot 'resources\rimes.ico' },
    @{ Name = 'black horn ICO'; Path = Join-Path $nativeRoot 'resources\rhino-horn-black.ico' },
    @{ Name = 'white horn ICO'; Path = Join-Path $nativeRoot 'resources\rhino-horn-white.ico' },
    @{ Name = 'contrast horn ICO'; Path = Join-Path $nativeRoot 'resources\rhino-horn-contrast.ico' }
)) {
    [void](Test-File -Path $icon.Path -Name $icon.Name -Required)
}

if ($iconHeader -match '#define\s+IDI_RIMES_HORN_BLACK\s+(\d+)' -and
    $iconHeader -match '#define\s+IDI_RIMES_HORN_WHITE\s+(\d+)') {
    Add-DiagnosticCheck 'horn resource identifiers' 'PASS' 'black and white horn resource IDs are present'
} else {
    Add-DiagnosticCheck 'horn resource identifiers' 'FAIL' 'horn resource IDs are missing from product_icon.h'
}

if ([string]::IsNullOrWhiteSpace($ArtifactDirectory)) {
    $ArtifactDirectory = Join-Path $nativeRoot "out\build\windows-$Architecture\$Configuration"
}
$artifactDirectoryExists = Test-Path -LiteralPath $ArtifactDirectory -PathType Container
if ($artifactDirectoryExists) {
    Add-DiagnosticCheck 'artifact directory' 'PASS' ([System.IO.Path]::GetFullPath($ArtifactDirectory))
    $tsfPath = Join-Path $ArtifactDirectory 'RimesTsf.dll'
    $registrarBinary = Join-Path $ArtifactDirectory 'RimesRegistrar.exe'
    [void](Test-File -Path $tsfPath -Name 'RimesTsf.dll')
    $hasRegistrar = Test-File -Path $registrarBinary -Name 'RimesRegistrar.exe'

    if ($hasRegistrar) {
        $metadataOutput = @(& $registrarBinary metadata 2>&1)
        $metadataExitCode = $LASTEXITCODE
        if ($metadataExitCode -eq 0) {
            try {
                $metadata = ($metadataOutput -join "`n") | ConvertFrom-Json
                if ([string]$metadata.textService.dll -eq 'RimesTsf.dll') {
                    Add-DiagnosticCheck 'registrar metadata' 'PASS' 'compiled registrar reports RimesTsf.dll'
                } else {
                    Add-DiagnosticCheck 'registrar metadata' 'FAIL' 'compiled registrar reports an unexpected TSF DLL'
                }
                if ([string]$metadata.architecture -eq $Architecture) {
                    Add-DiagnosticCheck 'registrar architecture' 'PASS' $Architecture
                } else {
                    Add-DiagnosticCheck 'registrar architecture' 'FAIL' "Expected $Architecture, got $($metadata.architecture)"
                }
                $compiledIconIndex = $metadata.textService.PSObject.Properties['iconIndex']
                if ($null -ne $compiledIconIndex -and [int]$compiledIconIndex.Value -eq $expectedIconIndex) {
                    Add-DiagnosticCheck 'compiled language-bar icon index' 'PASS' "IconIndex=$($compiledIconIndex.Value)"
                } else {
                    Add-DiagnosticCheck 'compiled language-bar icon index' 'FAIL' 'Registrar is stale or selects the wrong icon; rebuild RimesRegistrar'
                }
            } catch {
                Add-DiagnosticCheck 'registrar metadata' 'FAIL' "invalid metadata JSON: $($_.Exception.Message)"
            }
        } else {
            Add-DiagnosticCheck 'registrar metadata' 'WARN' "RimesRegistrar metadata exited with $metadataExitCode"
        }
    }
} else {
    Add-DiagnosticCheck 'artifact directory' 'WARN' "Build output not found: $ArtifactDirectory"
}

if ($Registered) {
    $diagnosticRegistrar = Join-Path $ArtifactDirectory 'RimesRegistrar.exe'
    if (Test-Path -LiteralPath $diagnosticRegistrar -PathType Leaf) {
        $diagnosticOutput = @(& $diagnosticRegistrar diagnose 2>&1)
        if ($LASTEXITCODE -eq 0) {
            try {
                $capabilities = ($diagnosticOutput -join "`n") | ConvertFrom-Json
                foreach ($name in @('comRegistered', 'processorRegistered', 'profileRegistered', 'profileEnabled')) {
                    $value = $capabilities.$name
                    Add-DiagnosticCheck "TSF $name" $(if ($value -eq $true) { 'PASS' } else { 'WARN' }) "$value"
                }
                foreach ($name in @('keyboard', 'displayAttributes', 'immersive', 'uiElement', 'systemTray', 'inputModeCompartment')) {
                    $value = $capabilities.categories.$name
                    Add-DiagnosticCheck "host capability: $name" $(if ($value -eq $true) { 'PASS' } else { 'WARN' }) $(if ($value) { 'Registered (not a runtime compatibility guarantee)' } else { 'Not registered; do not claim this capability without its implementation' })
                }
            } catch {
                Add-DiagnosticCheck 'TSF host capabilities' 'WARN' "Invalid diagnostic response: $($_.Exception.Message)"
            }
        } else {
            Add-DiagnosticCheck 'TSF host capabilities' 'WARN' 'Cannot query capabilities; rebuild the registrar with diagnose support'
        }
    }
    $registryView = if ($Architecture -eq 'x86') { [Microsoft.Win32.RegistryView]::Registry32 } else { [Microsoft.Win32.RegistryView]::Registry64 }
    $machineRegistry = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine, $registryView)
    try {
        $comKey = $machineRegistry.OpenSubKey("SOFTWARE\Classes\CLSID\$expectedClsid\InprocServer32")
        if ($null -ne $comKey) {
            try { $registeredDll = [string]$comKey.GetValue('') } finally { $comKey.Dispose() }
            if (Test-Path -LiteralPath $registeredDll -PathType Leaf) {
                $signature = Get-AuthenticodeSignature -LiteralPath $registeredDll
                Add-DiagnosticCheck 'registered DLL signature' $(if ($signature.Status -eq 'Valid') { 'PASS' } else { 'WARN' }) "$($signature.Status): $registeredDll"
            } else {
                Add-DiagnosticCheck 'registered COM DLL' 'FAIL' "Missing: $registeredDll"
            }
        } else {
            Add-DiagnosticCheck 'registered COM DLL' 'FAIL' "No $Architecture COM registration"
        }
    } finally { $machineRegistry.Dispose() }
    $registryProvider = Get-PSProvider -PSProvider Registry -ErrorAction SilentlyContinue
    if ($null -eq $registryProvider) {
        Add-DiagnosticCheck 'registered TSF profile' 'FAIL' 'Registry provider is unavailable; run -Registered on Windows'
    } else {
        $profileKey = Get-RimesLanguageProfileRegistryKey
        if ($null -eq $profileKey) {
            Add-DiagnosticCheck 'registered TSF profile' 'FAIL' 'RIMES language profile was not found in HKLM CTF/TIP'
        } else {
            $profileProperties = Get-ItemProperty -LiteralPath $profileKey.PSPath
            $iconFile = [string](Get-RegistryValue -Properties $profileProperties -Names @('IconFile', 'IconPath'))
            $iconIndexValue = Get-RegistryValue -Properties $profileProperties -Names @('IconIndex', 'IconIndexValue')
            Add-DiagnosticCheck 'registered TSF profile' 'PASS' $profileKey.PSPath

            if ($null -ne $iconIndexValue -and [int]$iconIndexValue -eq $expectedIconIndex) {
                Add-DiagnosticCheck 'registered language-bar icon index' 'PASS' "IconIndex=$iconIndexValue"
            } else {
                Add-DiagnosticCheck 'registered language-bar icon index' 'FAIL' "Expected $expectedIconIndex, got '$iconIndexValue'"
            }

            if ([string]::IsNullOrWhiteSpace($iconFile)) {
                Add-DiagnosticCheck 'registered language-bar icon file' 'FAIL' 'IconFile is empty'
            } elseif (Test-Path -LiteralPath $iconFile -PathType Leaf) {
                Add-DiagnosticCheck 'registered language-bar icon file' 'PASS' $iconFile
            } else {
                Add-DiagnosticCheck 'registered language-bar icon file' 'WARN' "IconFile does not exist: $iconFile"
            }
        }
    }
}

$failures = @($checks | Where-Object Status -eq 'FAIL')
$warnings = @($checks | Where-Object Status -eq 'WARN')
$checkArray = $checks.ToArray()
$result = [pscustomobject]@{
    Tool = 'Debug-RimesWindows'
    Architecture = $Architecture
    Configuration = $Configuration
    ArtifactDirectory = [System.IO.Path]::GetFullPath($ArtifactDirectory)
    RegisteredCheckRequested = [bool]$Registered
    Passed = $failures.Count -eq 0
    FailureCount = $failures.Count
    WarningCount = $warnings.Count
    Checks = $checkArray
}

if ($Json) {
    $result | ConvertTo-Json -Depth 5
} else {
    Write-Output ("RIMES Windows diagnostics: {0}/{1} ({2})" -f $Architecture, $Configuration, $(if (-not $result.Passed) { 'FAIL' } elseif ($warnings.Count) { 'PASS WITH WARNINGS' } else { 'PASS' }))
    foreach ($check in $checks) {
        Write-Output ('[{0}] {1}: {2}' -f $check.Status, $check.Name, $check.Detail)
    }
    if ($warnings.Count -gt 0) {
        Write-Output "Warnings: $($warnings.Count)"
    }
}

if (-not $result.Passed) {
    exit 1
}
