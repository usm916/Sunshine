<#
@file scripts/custom/service.ps1
@brief Match and control only the fixed custom LocalSystem service; never alter the installed service.
#>
<#
@brief Return the dedicated service identity, separate from SunshineService.
@return Fixed custom service name.
#>
function Get-SunshineCustomServiceName { return 'SunshineCustomService' }
<#
@brief Elevate fixed service registration and report the child's original error to the caller.
@param ScriptPath Absolute registration script path.
@param ExecutablePath Fixed host executable path.
@param ConfigDirectory Original user's persistent settings directory.
@param OwnerSid Original account receiving limited service control rights.
#>
function Invoke-SunshineServiceRegistrationElevation {
    param([string]$ScriptPath, [string]$ExecutablePath, [string]$ConfigDirectory, [string]$OwnerSid)
    $errorLog = [IO.Path]::GetTempFileName()
    try {
        $payload = @{Script=$ScriptPath;Executable=$ExecutablePath;Config=$ConfigDirectory;Owner=$OwnerSid;ErrorLog=$errorLog}|ConvertTo-Json -Compress
        $encoded = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($payload))
        $bootstrap = '$ErrorActionPreference=''Stop''; $p=ConvertFrom-Json ([Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('''+$encoded+'''))); try { & $p.Script -ExecutablePath $p.Executable -ConfigDirectory $p.Config -OwnerSid $p.Owner; exit 0 } catch { $_ | Out-String | Set-Content -LiteralPath $p.ErrorLog -Encoding UTF8; exit 1 }'
        $child = Start-Process -FilePath (Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe') -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-EncodedCommand',[Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($bootstrap))) -Verb RunAs -WindowStyle Hidden -Wait -PassThru
        if ($child.ExitCode -ne 0) {
            $details = [IO.File]::ReadAllText($errorLog).Trim()
            if ([string]::IsNullOrWhiteSpace($details)) { $details = 'The elevated process did not return an error report.' }
            throw "Service registration failed with exit code $($child.ExitCode).`n$details"
        }
    }
    finally {
        Remove-Item -LiteralPath $errorLog -Force -ErrorAction SilentlyContinue
    }
}
<#
@brief Encode the fixed wrapper and original user's configuration into SCM's native command line.
@param ExecutablePath Absolute Sunshine executable in the selected package.
@param ConfigDirectory Original user's persistent settings directory.
@return Native service command line without a command shell.
#>
function Get-SunshineServiceCommand {
    param([string]$ExecutablePath, [string]$ConfigDirectory)
    $wrapper = Join-Path (Split-Path $ExecutablePath -Parent) 'tools\sunshinesvc.exe'
    return (ConvertTo-SunshineTaskArgument $wrapper) + ' --service-name ' + (Get-SunshineCustomServiceName) + ' --config-dir ' + (ConvertTo-SunshineTaskArgument $ConfigDirectory)
}
<#
@brief Query only the dedicated custom service.
@return CIM service metadata, or null if not installed.
#>
function Get-SunshineCustomService {
    return Get-CimInstance Win32_Service -Filter "Name='SunshineCustomService'" -ErrorAction Stop
}
<#
@brief Reject a different executable, account, command line or service identity before control.
@param Service Metadata to inspect.
@param ExecutablePath Fixed host path.
@param ConfigDirectory Fixed persistent store.
@return True for the registered custom LocalSystem wrapper only.
#>
function Test-SunshineCustomService {
    param($Service, [string]$ExecutablePath, [string]$ConfigDirectory)
    return $null -ne $Service -and $Service.Name -ceq (Get-SunshineCustomServiceName) -and $Service.StartName -in @('LocalSystem', 'NT AUTHORITY\SYSTEM') -and $Service.PathName -ceq (Get-SunshineServiceCommand $ExecutablePath $ConfigDirectory) -and $Service.Description -ceq 'Sunshine Custom LocalSystem launcher v1'
}
<#
@brief Wait for SCM state without requesting elevation or changing the service definition.
@param State Required Running or Stopped state.
#>
function Wait-SunshineCustomService {
    param([ValidateSet('Running', 'Stopped')][string]$State)
    Add-Type -AssemblyName System.ServiceProcess
    $controller = Get-Service -Name (Get-SunshineCustomServiceName) -ErrorAction Stop
    $controller.WaitForStatus([System.ServiceProcess.ServiceControllerStatus]::$State, [TimeSpan]::FromSeconds(45))
}
<#
@brief Control the exactly matching custom service with the account's limited start/stop rights.
@param Action Requested start, stop, restart or status action.
@param ExecutablePath Fixed host path.
@param ConfigDirectory Fixed original user's configuration.
@return Current custom service metadata.
#>
function Invoke-SunshineServiceControl {
    param([ValidateSet('Start', 'Stop', 'Restart', 'Status')][string]$Action, [string]$ExecutablePath, [string]$ConfigDirectory)
    $service = Get-SunshineCustomService
    if (!(Test-SunshineCustomService $service $ExecutablePath $ConfigDirectory)) { throw 'The custom service does not match this package and shared configuration.' }
    if ($Action -in @('Start', 'Restart')) {
        $installed = Get-Service -Name SunshineService -ErrorAction SilentlyContinue
        if ($installed -and $installed.Status -ne 'Stopped') { throw 'The installed SunshineService is running. Stop it before starting the custom host; its startup setting is left unchanged.' }
    }
    if ($Action -in @('Stop', 'Restart') -and $service.State -ne 'Stopped') {
        Stop-Service -Name (Get-SunshineCustomServiceName) -ErrorAction Stop
        Wait-SunshineCustomService Stopped
    }
    if ($Action -in @('Start', 'Restart')) {
        Start-Service -Name (Get-SunshineCustomServiceName) -ErrorAction Stop
        Wait-SunshineCustomService Running
    }
    return Get-SunshineCustomService
}
<#
@brief Prefer an already registered custom service for the normal launcher.
@param ExecutablePath Requested host path.
@param ConfigDirectory Requested shared settings path.
@return True when the matching service was started, false when legacy launch should be used.
#>
function Start-SunshineMatchingService {
    param([string]$ExecutablePath, [string]$ConfigDirectory)
    if (!(Test-SunshineCustomService (Get-SunshineCustomService) $ExecutablePath $ConfigDirectory)) { return $false }
    $null = Invoke-SunshineServiceControl Start $ExecutablePath $ConfigDirectory
    return $true
}

<#
@brief Grant fixed service query/start/stop rights while preserving its existing DACL and leaving its SACL untouched.
@param Descriptor Existing SCM security descriptor, which may include owner, group and audit sections.
@param OwnerSid Original account SID.
@return DACL-only SDDL for sc.exe sdset; audit, owner and group sections are not requested for modification.
#>
function Add-SunshineServiceControlAccess {
    param([string]$Descriptor, [string]$OwnerSid)
    $security = [Security.AccessControl.RawSecurityDescriptor]::new($Descriptor)
    if ($null -eq $security.DiscretionaryAcl) { throw 'The service has no explicit DACL; inspect it before changing permissions.' }
    $sid = [Security.Principal.SecurityIdentifier]::new($OwnerSid)
    # QueryConfig | QueryStatus | Start | Stop | Interrogate | ReadControl.
    $ace = [Security.AccessControl.CommonAce]::new([Security.AccessControl.AceFlags]::None, [Security.AccessControl.AceQualifier]::AccessAllowed, 0x200B5, $sid, $false, $null)
    $index = $security.DiscretionaryAcl.Count
    for ($i=0; $i -lt $security.DiscretionaryAcl.Count; $i++) {
        if (([int]$security.DiscretionaryAcl[$i].AceFlags -band [int][Security.AccessControl.AceFlags]::Inherited) -ne 0) { $index=$i; break }
    }
    $security.DiscretionaryAcl.InsertAce($index, $ace)
    return $security.GetSddlForm([Security.AccessControl.AccessControlSections]::Access)
}
