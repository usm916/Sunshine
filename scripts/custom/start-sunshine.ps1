<#
@file scripts/custom/start-sunshine.ps1
@brief Start any Windows build with persistent per-user configuration.

.SYNOPSIS
Reuses Sunshine Custom credentials, pairings and settings across builds.
.PARAMETER ExecutablePath
Sunshine executable to run. Defaults to the current package in this checkout.
.PARAMETER ConfigDirectory
Shared configuration directory, independent of the selected build.
.PARAMETER RequireAdministrator
Request administrator privileges before starting Sunshine; keep the original shared configuration path.
.PARAMETER UseScheduledTask
Prefer a matching LocalSystem service, then the legacy elevated task, before direct launch.
.PARAMETER PrepareOnly
Prepare the shared directory and return launch details without starting Sunshine.
.PARAMETER SunshineArguments
Additional native Sunshine arguments, such as --version or port=48189.
#>
[CmdletBinding()]
param(
    [string]$ExecutablePath,
    [string]$ConfigDirectory = (Join-Path $env:LOCALAPPDATA 'SunshineCustom\config'),
    [switch]$RequireAdministrator,
    [switch]$UseScheduledTask,
    [switch]$PrepareOnly,
    [string[]]$SunshineArguments = @()
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'scheduled-task.ps1')
. (Join-Path $PSScriptRoot 'service.ps1')

<#
@brief Check whether the launcher currently has an elevated Windows token.
@return True when the current process is an administrator.
#>
function Test-SunshineAdministrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

<#
@brief Relaunch the selected build through Windows UAC without shell-interpreting user arguments.
@param ScriptPath Absolute launcher script path.
@param ExecutablePath Resolved native executable path.
@param ConfigDirectory Original user's absolute shared configuration directory.
@param SunshineArguments Additional native Sunshine arguments.
@return Exit code returned by the elevated launcher.
#>
function Invoke-SunshineElevation {
    param(
        [string]$ScriptPath,
        [string]$ExecutablePath,
        [string]$ConfigDirectory,
        [string[]]$SunshineArguments = @()
    )
    $payload = @{
        ScriptPath = $ScriptPath
        ExecutablePath = $ExecutablePath
        ConfigDirectory = $ConfigDirectory
        SunshineArguments = @($SunshineArguments)
    } | ConvertTo-Json -Depth 4 -Compress
    $encodedPayload = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($payload))
    $bootstrap = '$ErrorActionPreference = ''Stop''; $launch = ConvertFrom-Json ([Text.Encoding]::UTF8.GetString([Convert]::FromBase64String(''' + $encodedPayload + '''))); & $launch.ScriptPath -RequireAdministrator -ExecutablePath $launch.ExecutablePath -ConfigDirectory $launch.ConfigDirectory -SunshineArguments @($launch.SunshineArguments); exit $LASTEXITCODE'
    $encodedCommand = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($bootstrap))
    $powershellPath = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
    $child = Start-Process -FilePath $powershellPath -Verb RunAs -WindowStyle Hidden -ArgumentList @('-NoProfile', '-EncodedCommand', $encodedCommand) -Wait -PassThru -ErrorAction Stop
    return $child.ExitCode
}
if ([string]::IsNullOrWhiteSpace($ExecutablePath)) {
    $ExecutablePath = Join-Path $PSScriptRoot '..\..\cmake-build-custom\package\sunshine.exe'
    if (!(Test-Path -LiteralPath $ExecutablePath -PathType Leaf)) {
        $ExecutablePath = Join-Path $PSScriptRoot '..\..\cmake-build-custom\package-cursor-focus\sunshine.exe'
    }
}
$executable = (Resolve-Path -LiteralPath $ExecutablePath).ProviderPath
if (!(Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw 'ExecutablePath must refer to a Sunshine executable file.'
}
$shared = [System.IO.Path]::GetFullPath($ConfigDirectory)
if ($UseScheduledTask -and !$PrepareOnly -and $SunshineArguments.Count -eq 0 -and (Start-SunshineMatchingService $executable $shared)) {
    Write-Host "Sunshine Custom LocalSystem service requested."
    return
}
if ($UseScheduledTask -and !$PrepareOnly -and $SunshineArguments.Count -eq 0 -and (Start-SunshineMatchingTask $executable $shared)) {
    Write-Host "Sunshine Custom task requested: $(Get-SunshineTaskName)"
    return
}
if ($RequireAdministrator -and !$PrepareOnly -and !(Test-SunshineAdministrator)) {
    # Resolve the original user's paths before UAC can change the process identity.
    $result = Invoke-SunshineElevation -ScriptPath $PSCommandPath -ExecutablePath $executable -ConfigDirectory $shared -SunshineArguments $SunshineArguments
    exit $result
}
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
$arguments = @(Get-SunshineSharedArguments $shared) + $SunshineArguments
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
