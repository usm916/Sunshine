<#
@file scripts/custom/control-sunshine-task.ps1
@brief Start, stop, restart or inspect the fixed elevated task without repeated UAC.
.PARAMETER Action
Requested operation; only the validated account task is controlled.
.PARAMETER ExecutablePath
Pinned executable used when registering the task.
.PARAMETER ConfigDirectory
Shared settings directory used when registering the task.
#>
[CmdletBinding()]
param(
    [ValidateSet('Start', 'Stop', 'Restart', 'Status')][string]$Action = 'Status',
    [string]$ExecutablePath,
    [string]$ConfigDirectory = (Join-Path $env:LOCALAPPDATA 'SunshineCustom\config')
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'scheduled-task.ps1')
. (Join-Path $PSScriptRoot 'service.ps1')
if ([string]::IsNullOrWhiteSpace($ExecutablePath)) {
    $ExecutablePath = Join-Path $PSScriptRoot '..\..\cmake-build-custom\package\sunshine.exe'
}
$registeredService = Get-SunshineCustomService
if ($registeredService) {
    $service = Invoke-SunshineServiceControl $Action ([IO.Path]::GetFullPath($ExecutablePath)) ([IO.Path]::GetFullPath($ConfigDirectory))
    [pscustomobject]@{ServiceName=$service.Name;State=$service.State;Account=$service.StartName;ExecutablePath=[IO.Path]::GetFullPath($ExecutablePath)}
    return
}
$task = Invoke-SunshineTaskControl $Action ([IO.Path]::GetFullPath($ExecutablePath)) ([IO.Path]::GetFullPath($ConfigDirectory))
[pscustomobject]@{ TaskName=$task.TaskName; State=$task.State; ExecutablePath=[IO.Path]::GetFullPath($ExecutablePath) }
