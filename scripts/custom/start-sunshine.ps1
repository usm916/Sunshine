<#
@file scripts/custom/start-sunshine.ps1
@brief Start any Windows build with persistent per-user configuration.

.SYNOPSIS
Reuses Sunshine Custom credentials, pairings and settings across builds.
.PARAMETER ExecutablePath
Sunshine executable to run. Defaults to the cursor-focus package in this checkout.
.PARAMETER ConfigDirectory
Shared configuration directory, independent of the selected build.
.PARAMETER PrepareOnly
Prepare the shared directory and return launch details without starting Sunshine.
.PARAMETER SunshineArguments
Additional native Sunshine arguments, such as --version or port=48189.
#>
[CmdletBinding()]
param(
    [string]$ExecutablePath,
    [string]$ConfigDirectory = (Join-Path $env:LOCALAPPDATA 'SunshineCustom\config'),
    [switch]$PrepareOnly,
    [string[]]$SunshineArguments = @()
)
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($ExecutablePath)) {
    $ExecutablePath = Join-Path $PSScriptRoot '..\..\cmake-build-custom\package-cursor-focus\sunshine.exe'
}
$executable = (Resolve-Path -LiteralPath $ExecutablePath).ProviderPath
if (!(Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw 'ExecutablePath must refer to a Sunshine executable file.'
}
$shared = [System.IO.Path]::GetFullPath($ConfigDirectory)
$source = Join-Path (Split-Path $executable -Parent) 'config'
$migratedFrom = $null
if (Test-Path -LiteralPath $shared) {
    if (!(Test-Path -LiteralPath $shared -PathType Container)) {
        throw 'ConfigDirectory must refer to a directory.'
    }
}
else {
    $parent = Split-Path $shared -Parent
    New-Item -ItemType Directory -Path $parent -Force | Out-Null
    if (Test-Path -LiteralPath $source -PathType Container) {
        Copy-Item -LiteralPath $source -Destination $shared -Recurse
        $migratedFrom = $source
    }
    else {
        New-Item -ItemType Directory -Path $shared | Out-Null
    }
}
$configFile = Join-Path $shared 'sunshine.conf'
if (!(Test-Path -LiteralPath $configFile)) {
    New-Item -ItemType File -Path $configFile | Out-Null
}
$arguments = @(
    $configFile,
    "file_state=$(Join-Path $shared 'sunshine_state.json')",
    "credentials_file=$(Join-Path $shared 'sunshine_state.json')",
    "pkey=$(Join-Path $shared 'credentials\cakey.pem')",
    "cert=$(Join-Path $shared 'credentials\cacert.pem')",
    "file_apps=$(Join-Path $shared 'apps.json')",
    "log_path=$(Join-Path $shared 'sunshine.log')"
) + $SunshineArguments
if ($PrepareOnly) {
    [pscustomobject]@{
        ExecutablePath = $executable
        ConfigDirectory = $shared
        MigratedFrom = $migratedFrom
        Arguments = $arguments
    }
    return
}
Write-Host "Shared Sunshine configuration: $shared"
Push-Location (Split-Path $executable -Parent)
try {
    & $executable @arguments
    $result = $LASTEXITCODE
}
finally {
    Pop-Location
}
exit $result
