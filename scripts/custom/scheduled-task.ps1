<#
@file scripts/custom/scheduled-task.ps1
@brief Fixed, per-user elevated Sunshine task definitions and control helpers.
#>

<#
@brief Obtain the task name scoped to the current Windows account.
@return A stable task name containing the account SID.
#>
function Get-SunshineTaskName {
    return 'SunshineCustom-' + [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
}

<#
@brief Quote one literal Windows native argument, including trailing backslashes.
@param Value Argument passed directly to sunshine.exe, without a command shell.
@return Double-quoted command-line argument.
#>
function ConvertTo-SunshineTaskArgument {
    param([AllowEmptyString()][string]$Value)
    $escaped = [regex]::Replace($Value, '(\\*)"', '$1$1\"')
    $escaped = [regex]::Replace($escaped, '(\\+)$', '$1$1')
    return '"' + $escaped + '"'
}

<#
@brief Build persistent native arguments without reading credential contents.
@param ConfigDirectory Absolute shared settings directory.
@return Native argument array used by both direct and scheduled launchers.
#>
function Get-SunshineSharedArguments {
    param([string]$ConfigDirectory)
    return @(
        (Join-Path $ConfigDirectory 'sunshine.conf'),
        "file_state=$(Join-Path $ConfigDirectory 'sunshine_state.json')",
        "credentials_file=$(Join-Path $ConfigDirectory 'sunshine_state.json')",
        "pkey=$(Join-Path $ConfigDirectory 'credentials\cakey.pem')",
        "cert=$(Join-Path $ConfigDirectory 'credentials\cacert.pem')",
        "file_apps=$(Join-Path $ConfigDirectory 'apps.json')",
        "log_path=$(Join-Path $ConfigDirectory 'sunshine.log')"
    )
}

<#
@brief Encode the fixed native task arguments without shell expansion.
@param ConfigDirectory Absolute shared settings directory.
@return Command line consumed by the registered native executable.
#>
function Get-SunshineTaskArguments {
    param([string]$ConfigDirectory)
    return ((Get-SunshineSharedArguments $ConfigDirectory | ForEach-Object { ConvertTo-SunshineTaskArgument $_ }) -join ' ')
}

<#
@brief Pin the supervisor script, host and settings into the registered PowerShell action.
@param ExecutablePath Registered native host path.
@param ConfigDirectory Registered settings directory.
@return Fixed action arguments without a dynamic request channel.
#>
function Get-SunshineSupervisorArguments {
    param([string]$ExecutablePath,[string]$ConfigDirectory)
    $runner=Join-Path (Split-Path $ExecutablePath -Parent) 'scripts\custom\run-sunshine-task.ps1'
    return '-NoProfile -NonInteractive -WindowStyle Hidden -File '+(ConvertTo-SunshineTaskArgument $runner)+' -ExecutablePath '+(ConvertTo-SunshineTaskArgument $ExecutablePath)+' -ConfigDirectory '+(ConvertTo-SunshineTaskArgument $ConfigDirectory)
}

<#
@brief Read only this account's named task; propagate scheduler access failures.
@return Registered task, or null when no matching name exists.
#>
function Get-SunshineRegisteredTask {
    return Get-ScheduledTask -TaskPath '\' -ErrorAction Stop | Where-Object TaskName -eq (Get-SunshineTaskName) | Select-Object -First 1
}

<#
@brief Check ownership and the fixed native action before reusing elevated execution.
@param Task Candidate registered task.
@param ExecutablePath Absolute pinned Sunshine executable.
@param ConfigDirectory Absolute shared settings directory.
@return True only for an enabled, interactive, highest-privilege task with identical arguments.
#>
function Test-SunshineRegisteredTask {
    param($Task, [string]$ExecutablePath, [string]$ConfigDirectory)
    if (!$Task -or $Task.Description -cne 'Sunshine Custom interactive launcher v2' -or !$Task.Settings.Enabled) { return $false }
    $actions = @($Task.Actions)
    if ($actions.Count -ne 1 -or $Task.Principal.LogonType -ne 'Interactive' -or $Task.Principal.RunLevel -ne 'Highest') { return $false }
    $sid = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
    $owner = $Task.Principal.UserId
    if ($owner -ne $sid) {
        try { $owner = ([Security.Principal.NTAccount]::new($owner)).Translate([Security.Principal.SecurityIdentifier]).Value }
        catch { return $false }
    }
    return $owner -eq $sid -and $actions[0].Execute -ieq (Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe') -and $actions[0].WorkingDirectory -ieq (Split-Path $ExecutablePath -Parent) -and $actions[0].Arguments -ceq (Get-SunshineSupervisorArguments $ExecutablePath $ConfigDirectory)
}

<#
@brief Construct an on-demand task for the signed-in account without storing a password.
@param ExecutablePath Absolute pinned Sunshine executable.
@param ConfigDirectory Absolute shared settings directory.
@return Task definition with no time limit, no triggers and duplicate starts ignored.
#>
function New-SunshineTaskDefinition {
    param([string]$ExecutablePath, [string]$ConfigDirectory)
    $action = New-ScheduledTaskAction -Execute (Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe') -Argument (Get-SunshineSupervisorArguments $ExecutablePath $ConfigDirectory) -WorkingDirectory (Split-Path $ExecutablePath -Parent)
    $principal = New-ScheduledTaskPrincipal -UserId ([Security.Principal.WindowsIdentity]::GetCurrent().User.Value) -LogonType Interactive -RunLevel Highest
    $settings = New-ScheduledTaskSettingsSet -ExecutionTimeLimit ([TimeSpan]::Zero) -MultipleInstances IgnoreNew -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
    return New-ScheduledTask -Action $action -Principal $principal -Settings $settings -Description 'Sunshine Custom interactive launcher v2'
}

<#
@brief Start an already registered, exactly matching Sunshine task.
@param ExecutablePath Absolute requested executable.
@param ConfigDirectory Absolute requested settings directory.
@return True when the scheduler accepted the request; false when direct launch is required.
#>
function Start-SunshineMatchingTask {
    param([string]$ExecutablePath, [string]$ConfigDirectory)
    $task = Get-SunshineRegisteredTask
    if (!(Test-SunshineRegisteredTask $task $ExecutablePath $ConfigDirectory)) { return $false }
    Start-ScheduledTask -TaskName (Get-SunshineTaskName) -TaskPath '\' -ErrorAction Stop
    return $true
}

<#
@brief Stop or restart only the validated registered Sunshine task.
@param Action Requested operation, without changing the task definition.
@param ExecutablePath Absolute pinned executable, which may be temporarily absent during an update.
@param ConfigDirectory Absolute shared settings directory.
@return Registered task after the requested operation.
#>
function Invoke-SunshineTaskControl {
    param([ValidateSet('Start', 'Stop', 'Restart', 'Status')][string]$Action, [string]$ExecutablePath, [string]$ConfigDirectory)
    $task = Get-SunshineRegisteredTask
    if (!(Test-SunshineRegisteredTask $task $ExecutablePath $ConfigDirectory)) { throw 'No matching Sunshine Custom task. Register it for this executable and shared settings first.' }
    $name = Get-SunshineTaskName
    if ($Action -in @('Stop', 'Restart') -and $task.State -eq 'Running') {
        Stop-ScheduledTask -TaskName $name -TaskPath '\' -ErrorAction Stop
        $deadline = [DateTime]::UtcNow.AddSeconds(10)
        do {
            $task = Get-SunshineRegisteredTask
            if ($task.State -ne 'Running') { break }
            if ([DateTime]::UtcNow -ge $deadline) { throw 'Sunshine task did not stop within ten seconds.' }
            Start-Sleep -Milliseconds 100
        } while ($true)
    }
    if ($Action -in @('Start', 'Restart')) {
        Start-ScheduledTask -TaskName $name -TaskPath '\' -ErrorAction Stop
    }
    return Get-SunshineRegisteredTask
}
