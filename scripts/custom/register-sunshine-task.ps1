<#
@file scripts/custom/register-sunshine-task.ps1
@brief Register an on-demand elevated Sunshine task once through Windows UAC.
.PARAMETER ExecutablePath
Fixed Sunshine executable; defaults to this checkout's package.
.PARAMETER ConfigDirectory
Original user's persistent configuration directory.
.PARAMETER PrepareOnly
Inspect the real task definition without registration or UAC.
.PARAMETER Remove
Remove only the matching per-user task, requesting UAC when necessary.
.PARAMETER OwnerSid
Original account SID, preserved across UAC and checked against the elevated account.
#>
[CmdletBinding()]
param(
    [string]$ExecutablePath,
    [string]$ConfigDirectory = (Join-Path $env:LOCALAPPDATA 'SunshineCustom\config'),
    [switch]$PrepareOnly,
    [switch]$Remove,
    [string]$OwnerSid = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'scheduled-task.ps1')
if ([string]::IsNullOrWhiteSpace($ExecutablePath)) {
    $ExecutablePath = Join-Path $PSScriptRoot '..\..\cmake-build-custom\package\sunshine.exe'
}
$executable = [IO.Path]::GetFullPath($ExecutablePath)
$shared = [IO.Path]::GetFullPath($ConfigDirectory)
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
if ($identity.User.Value -ne $OwnerSid) { throw 'Register this task with the same signed-in administrator account, not another account.' }
if (!$Remove -and !(Test-Path -LiteralPath $executable -PathType Leaf)) { throw 'Sunshine executable does not exist.' }
$taskName = Get-SunshineTaskName
if ($PrepareOnly) {
    if ($Remove) { throw 'PrepareOnly and Remove cannot be combined.' }
    $task = New-SunshineTaskDefinition $executable $shared
    [pscustomobject]@{ TaskName=$taskName; Actions=$task.Actions; Principal=$task.Principal; Settings=$task.Settings; Triggers=@($task.Triggers); Description=$task.Description }
    return
}
if (!$Remove) {
    $support = Join-Path (Split-Path $executable -Parent) 'scripts\custom'
    foreach ($file in @('run-sunshine-task.ps1', 'scheduled-task.ps1', 'task-job.cs')) {
        if (!(Test-Path -LiteralPath (Join-Path $support $file) -PathType Leaf)) { throw 'The selected package is missing the supervised task helpers. Deploy scripts/custom with this build before registration.' }
    }
}
$existing = Get-SunshineRegisteredTask
if ($existing -and !(Test-SunshineRegisteredTask $existing $executable $shared)) { throw 'An incompatible task already uses this account task name. Leave it intact and remove or inspect it explicitly.' }
if ($existing -and !$Remove) {
    [pscustomobject]@{ TaskName=$taskName; Status='AlreadyRegistered' }
    return
}
if (!$existing -and $Remove) {
    [pscustomobject]@{ TaskName=$taskName; Status='NotRegistered' }
    return
}
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (!$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    $payload = @{ ScriptPath=$PSCommandPath; ExecutablePath=$executable; ConfigDirectory=$shared; OwnerSid=$OwnerSid; Remove=$Remove.IsPresent } | ConvertTo-Json -Compress
    $encodedPayload = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($payload))
    $bootstrap = '$ErrorActionPreference = ''Stop''; $p = ConvertFrom-Json ([Text.Encoding]::UTF8.GetString([Convert]::FromBase64String(''' + $encodedPayload + '''))); & $p.ScriptPath -ExecutablePath $p.ExecutablePath -ConfigDirectory $p.ConfigDirectory -OwnerSid $p.OwnerSid -Remove:$p.Remove; exit $LASTEXITCODE'
    $encodedCommand = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($bootstrap))
    $child = Start-Process -FilePath (Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe') -Verb RunAs -WindowStyle Hidden -ArgumentList @('-NoProfile', '-EncodedCommand', $encodedCommand) -Wait -PassThru -ErrorAction Stop
    exit $child.ExitCode
}
if ($Remove) {
    if ($existing.State -eq 'Running') { throw 'Stop the Sunshine task before removing it.' }
    Unregister-ScheduledTask -TaskName $taskName -TaskPath '\' -Confirm:$false -ErrorAction Stop
    [pscustomobject]@{ TaskName=$taskName; Status='Removed' }
    return
}
# Prepare the original user's store once, using the same migration policy as direct launch.
$prepared = & (Join-Path $PSScriptRoot 'start-sunshine.ps1') -ExecutablePath $executable -ConfigDirectory $shared -PrepareOnly
$definition = New-SunshineTaskDefinition $prepared.ExecutablePath $prepared.ConfigDirectory
Register-ScheduledTask -TaskName $taskName -TaskPath '\' -InputObject $definition -ErrorAction Stop | Out-Null
$registered = Get-SunshineRegisteredTask
if (!(Test-SunshineRegisteredTask $registered $executable $shared)) { throw 'Registered task verification failed.' }
[pscustomobject]@{ TaskName=$taskName; Status='Registered' }
