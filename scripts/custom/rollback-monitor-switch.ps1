<#
@file scripts/custom/rollback-monitor-switch.ps1
@brief Restore the pinned 20px package saved before automatic monitor switching was added.
.PARAMETER RepositoryRoot
Workspace containing the checkpoint and existing custom service deployment helper.
.PARAMETER CheckOnly
Validate the complete checkpoint without stopping or changing anything.
#>
[CmdletBinding()]
param(
    [string]$RepositoryRoot,
    [switch]$CheckOnly
)
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($RepositoryRoot)) { $RepositoryRoot = Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) '..\..' }
<#
@brief Compute a package hash using only .NET, including in minimal PowerShell environments.
@param Path Exact file to read.
@return Uppercase SHA-256 string.
#>
function Get-CheckpointHash([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($algorithm.ComputeHash($stream)).Replace('-', '') }
    finally { $algorithm.Dispose(); $stream.Dispose() }
}
$root = [IO.Path]::GetFullPath($RepositoryRoot).TrimEnd('\')
$build = Join-Path $root 'cmake-build-custom'
$checkpoint = Get-Content -LiteralPath (Join-Path $build 'checkpoint-crop20.json') -Raw | ConvertFrom-Json
if ($checkpoint.crop_margin_pixels -ne 20 -or $checkpoint.monitor_switch_supported) { throw 'Checkpoint is not the single-monitor 20px baseline.' }
$stage = [IO.Path]::GetFullPath((Join-Path $build $checkpoint.package))
$runner = [IO.Path]::GetFullPath((Join-Path $build $checkpoint.runner))
if (!$stage.StartsWith($build+'\',[StringComparison]::OrdinalIgnoreCase) -or !$runner.StartsWith($build+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Checkpoint escaped the build directory.' }
if ((Get-CheckpointHash $runner) -ine $checkpoint.runner_sha256) { throw 'Restore helper differs from the pinned checkpoint.' }
if (@(Get-ChildItem -LiteralPath $stage -File -Recurse).Count -ne @($checkpoint.files).Count) { throw 'Checkpoint file count changed.' }
foreach ($entry in $checkpoint.files) {
    $path = [IO.Path]::GetFullPath((Join-Path $stage $entry.path))
    if (!$path.StartsWith($stage+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Checkpoint file escaped its package.' }
    if ((Get-CheckpointHash $path) -ine $entry.sha256) { throw "Checkpoint file changed: $($entry.path)" }
}
if ($CheckOnly) {
    [pscustomobject]@{ Status='Validated'; CropMarginPixels=20; MonitorSwitch=$false; Files=@($checkpoint.files).Count; Package=$stage }
    return
}
# Load the complete Windows PowerShell utility module before invoking its script functions.
Import-Module (Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Utility\Microsoft.PowerShell.Utility.psd1') -Force -ErrorAction Stop
# The verified service deployment helper stops only the dedicated custom service, verifies
# file release, replaces the complete package and restarts it, preserving IPv6 forwarding.
& $runner
if (!$?) { throw 'The checkpoint deployment failed; inspect its recovery report.' }
Write-Output 'Restored the 20px package without automatic monitor switching. Reconnect Moonlight.'
