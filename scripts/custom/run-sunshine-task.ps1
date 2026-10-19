<#
@file scripts/custom/run-sunshine-task.ps1
@brief Keep the fixed host and its native self-restarts inside one supervised Windows job.
.PARAMETER ExecutablePath
Registered absolute Sunshine executable.
.PARAMETER ConfigDirectory
Registered shared configuration directory.
#>
[CmdletBinding()]
param([Parameter(Mandatory)][string]$ExecutablePath, [Parameter(Mandatory)][string]$ConfigDirectory)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'scheduled-task.ps1')
$exe=(Resolve-Path -LiteralPath $ExecutablePath).ProviderPath
$shared=[IO.Path]::GetFullPath($ConfigDirectory)
Add-Type -Path (Join-Path $PSScriptRoot 'task-job.cs')
$job=[SunshineTaskJob]::new()
try {
    $commandLine=(ConvertTo-SunshineTaskArgument $exe)+' '+(Get-SunshineTaskArguments $shared)
    $null=$job.Start($exe,$commandLine,(Split-Path $exe -Parent))
    $job.Wait()
} finally { $job.Dispose() }
