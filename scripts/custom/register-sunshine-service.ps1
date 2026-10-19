<#
@file scripts/custom/register-sunshine-service.ps1
@brief Register the dedicated manual LocalSystem service once, preserving existing SunshineService.
.PARAMETER ExecutablePath
Fixed host executable in the prepared package.
.PARAMETER ConfigDirectory
Original user's existing shared settings directory.
.PARAMETER OwnerSid
Account granted only service query/start/stop rights, preserved across UAC.
.PARAMETER PrepareOnly
Return the concrete registration plan without UAC or service writes.
#>
[CmdletBinding()]
param(
    [string]$ExecutablePath,
    [string]$ConfigDirectory = (Join-Path $env:LOCALAPPDATA 'SunshineCustom\config'),
    [string]$OwnerSid = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value,
    [switch]$PrepareOnly
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'scheduled-task.ps1')
. (Join-Path $PSScriptRoot 'service.ps1')
if ([string]::IsNullOrWhiteSpace($ExecutablePath)) { $ExecutablePath = Join-Path $PSScriptRoot '..\..\cmake-build-custom\package\sunshine.exe' }
$exe = [IO.Path]::GetFullPath($ExecutablePath)
$shared = [IO.Path]::GetFullPath($ConfigDirectory)
$sid = [Security.Principal.SecurityIdentifier]::new($OwnerSid)
if ($sid.Value -ne [Security.Principal.WindowsIdentity]::GetCurrent().User.Value) { throw 'Register with the original signed-in administrator account.' }
$command = Get-SunshineServiceCommand $exe $shared
if ($PrepareOnly) {
    [pscustomobject]@{Name=(Get-SunshineCustomServiceName);BinaryPathName=$command;Account='LocalSystem';StartupType='Manual';OwnerSid=$sid.Value;OwnerRights='QueryConfig,QueryStatus,Start,Stop,Interrogate,ReadControl';Description='Sunshine Custom LocalSystem launcher v1'}
    return
}
foreach ($path in @($exe, (Join-Path (Split-Path $exe -Parent) 'tools\sunshinesvc.exe'), (Join-Path $shared 'sunshine.conf'))) {
    if (!(Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required existing file is missing: $path" }
}
$metadataPath = Join-Path (Split-Path $exe -Parent) 'build-info.json'
if (!(Test-Path -LiteralPath $metadataPath -PathType Leaf)) { throw 'Deploy a verified service-capable package before registration.' }
$metadata = Get-Content -LiteralPath $metadataPath -Raw | ConvertFrom-Json
$wrapper = Join-Path (Split-Path $exe -Parent) 'tools\sunshinesvc.exe'
if (!$metadata.system_service_capable -or !$metadata.sunshine_service_wrapper_sha256 -or (Get-FileHash -LiteralPath $wrapper).Hash -ine $metadata.sunshine_service_wrapper_sha256) { throw 'Deploy a verified service-capable package before registration.' }
$existing = Get-SunshineCustomService
if ($existing) {
    if (!(Test-SunshineCustomService $existing $exe $shared)) { throw 'An incompatible custom service already exists; it was left intact.' }
    [pscustomobject]@{Name=$existing.Name;Status='AlreadyRegistered'}
    return
}
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
if (!([Security.Principal.WindowsPrincipal]::new($identity)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Invoke-SunshineServiceRegistrationElevation -ScriptPath $PSCommandPath -ExecutablePath $exe -ConfigDirectory $shared -OwnerSid $sid.Value
    [pscustomobject]@{Name=(Get-SunshineCustomServiceName);Status='Registered';StartupType='Manual';Account='LocalSystem'}
    return
}
$name = Get-SunshineCustomServiceName
New-Service -Name $name -BinaryPathName $command -DisplayName 'Sunshine Custom' -Description 'Sunshine Custom LocalSystem launcher v1' -StartupType Manual -ErrorAction Stop | Out-Null
# Add only fixed-service control rights; never grant configuration or DACL modification.
$sc = Join-Path $env:SystemRoot 'System32\sc.exe'
$security = @(& $sc sdshow $name)
if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect the new service security descriptor.' }
$sddl = (@($security | ForEach-Object { $_.Trim() } | Where-Object { $_ -match '^[DOGS]:' }) -join '')
if (!$sddl) { throw 'Cannot read the new service security descriptor.' }
$dacl = Add-SunshineServiceControlAccess $sddl $sid.Value
$null = & $sc sdset $name $dacl
if ($LASTEXITCODE -ne 0) { throw 'Cannot grant the owner limited service control rights.' }
$registered = Get-SunshineCustomService
if (!(Test-SunshineCustomService $registered $exe $shared) -or $registered.StartMode -ne 'Manual') { throw 'Registered service verification failed.' }
[pscustomobject]@{Name=$name;Status='Registered';StartupType='Manual';Account=$registered.StartName}
