param(
    [Parameter(Mandatory = $true)]
    [string]$Version,

    [Parameter(Mandatory = $true)]
    [string]$InputExe,

    [Parameter(Mandatory = $true)]
    [string]$OutputExe
)

$ErrorActionPreference = "Stop"

function Resolve-Wix {
    $command = Get-Command wix.exe -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $candidates = @(
        "C:\Program Files\WiX Toolset v6\bin\wix.exe",
        "C:\Program Files (x86)\WiX Toolset v6\bin\wix.exe",
        "C:\ProgramData\chocolatey\bin\wix.exe",
        "$env:USERPROFILE\.dotnet\tools\wix.exe"
    )

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }

    throw "wix.exe not found. Install WiX Toolset v6 on the Windows runner."
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$installerWxs = Join-Path $PSScriptRoot "windows-installer.wxs"
$bundleWxs = Join-Path $PSScriptRoot "windows-bundle.wxs"
$iconPath = Join-Path $repoRoot "resources\app-icons\windows\cupuacu.ico"
$wix = Resolve-Wix

$resolvedInputExe = [System.IO.Path]::GetFullPath($InputExe)
$resolvedOutputExe = [System.IO.Path]::GetFullPath($OutputExe)
$outputDir = Split-Path -Parent $resolvedOutputExe
$intermediateDir = Join-Path $repoRoot "build\windows-wix"
$resolvedIntermediateDir = [System.IO.Path]::GetFullPath($intermediateDir)
$msiPath = Join-Path $resolvedIntermediateDir "Cupuacu-$Version-x64.msi"
$wixVersion = (& $wix --version).Trim()
$wixVersionCore = ($wixVersion -split '[\+\-]', 2)[0]
$wixMajorVersion = [int](($wixVersionCore -split '\.')[0])

if (-not (Test-Path $resolvedInputExe)) {
    throw "Input executable not found: $resolvedInputExe"
}

if ($wixMajorVersion -lt 6) {
    throw "WiX Toolset v6 or newer is required. Found: $wixVersion"
}

foreach ($path in @($outputDir, $resolvedIntermediateDir)) {
    if (-not (Test-Path $path)) {
        New-Item -ItemType Directory -Path $path | Out-Null
    }
}

$commonArgs = @(
    "build",
    "-arch", "x64",
    "-d", "Version=$Version",
    "-d", "AppIcon=$iconPath"
)

$bootstrapperExtension = "WixToolset.BootstrapperApplications.wixext/$wixVersionCore"

& $wix extension add --global $bootstrapperExtension

if ($LASTEXITCODE -ne 0) {
    throw "WiX failed to add the bootstrapper extension: $bootstrapperExtension"
}

& $wix @commonArgs `
    "-o" $msiPath `
    "-d" "AppExe=$resolvedInputExe" `
    $installerWxs

if ($LASTEXITCODE -ne 0) {
    throw "WiX failed to build MSI package."
}

& $wix @commonArgs `
    "-ext", $bootstrapperExtension `
    "-o" $resolvedOutputExe `
    "-d" "MsiPath=$msiPath" `
    $bundleWxs

if ($LASTEXITCODE -ne 0) {
    throw "WiX failed to build bootstrapper."
}
