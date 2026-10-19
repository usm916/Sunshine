"""@file test_sunshine_service.py
@brief Validate dedicated service matching and control with isolated PowerShell mocks.
"""
import json
import subprocess
import unittest
from test_shared_config import SharedConfigTests, quote, ROOT


class SunshineServiceTests(unittest.TestCase):
    """@brief Exercise helper logic without registering or controlling real services."""
    setUp = SharedConfigTests.setUp
    build = SharedConfigTests.build

    def ps(self, body):
        """@brief Execute a Unicode fixture and decode its JSON output."""
        script = self.root / 'service fixture.ps1'
        script.write_text("$ErrorActionPreference='Stop'\n$OutputEncoding=[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)\n. " + quote(ROOT/'scripts/custom/scheduled-task.ps1') + "\n. " + quote(ROOT/'scripts/custom/service.ps1') + "\n" + body, encoding='utf-8-sig')
        run = subprocess.run(['powershell.exe','-NoProfile','-File',str(script)],capture_output=True,encoding='utf-8',timeout=35)
        self.assertEqual(run.returncode,0,run.stderr)
        return json.loads(run.stdout)

    def fixture(self):
        """@brief Build metadata and native arguments with Unicode paths."""
        exe = self.build("日本語 build's folder")
        shared = self.root/'共有 settings'
        return "$exe="+quote(exe)+";$shared="+quote(shared)+r'''
$script:service=[pscustomobject]@{Name='SunshineCustomService';StartName='LocalSystem';PathName=(Get-SunshineServiceCommand $exe $shared);Description='Sunshine Custom LocalSystem launcher v1';State='Running';StartMode='Manual'}
$script:events=@()
function Get-CimInstance {param($ClassName,$Filter,$ErrorAction) if($Filter -ne "Name='SunshineCustomService'"){throw 'Unexpected query'};return $script:service}
function Get-Service {param($Name,$ErrorAction) if($Name -ne 'SunshineService'){throw 'Unexpected service'};return [pscustomobject]@{Status='Stopped'}}
function Start-Service {param($Name,$ErrorAction) $script:events+='Start:'+ $Name;$script:service.State='Running'}
function Stop-Service {param($Name,$ErrorAction) $script:events+='Stop:'+ $Name;$script:service.State='Stopped'}
function Wait-SunshineCustomService {param($State) $script:events+='Wait:'+ $State}
'''

    def test_prepare_only_is_manual_system_and_never_creates_configuration(self):
        """@brief Inspect registration without writing persistent system state."""
        exe = self.build('service plan')
        shared = self.root/'not created'
        result = self.ps('$p=& '+quote(ROOT/'scripts/custom/register-sunshine-service.ps1')+' -PrepareOnly -ExecutablePath '+quote(exe)+' -ConfigDirectory '+quote(shared)+'; $p|ConvertTo-Json')
        self.assertEqual(result['Name'],'SunshineCustomService')
        self.assertEqual(result['Account'],'LocalSystem')
        self.assertEqual(result['StartupType'],'Manual')
        self.assertIn('--config-dir "'+str(shared)+'"',result['BinaryPathName'])
        self.assertNotIn('ChangeConfig',result['OwnerRights'])
        self.assertFalse(shared.exists())

    def test_matching_rejects_changed_account_identity_paths_and_description(self):
        """@brief Refuse control of an installed, unrelated or repointed service."""
        edits=['$script:service=$null',"$script:service.Name='SunshineService'","$script:service.StartName='noTempo'","$script:service.PathName='other.exe'","$script:service.Description='other'","$script:service.StartName='NT AUTHORITY\\SYSTEM'",'']
        body='$r=@()\n'
        fixture=self.fixture()
        for edit in edits:
            body+=fixture+edit+'\n$r+=Test-SunshineCustomService $script:service $exe $shared\n'
        self.assertEqual(self.ps(body+'ConvertTo-Json -InputObject $r'),[False]*5+[True,True])

    def test_control_orders_stop_wait_start_without_changing_installed_service(self):
        """@brief Verify service-only operations and idempotent stopped state."""
        result=self.ps(self.fixture()+r'''
$null=Invoke-SunshineServiceControl Status $exe $shared
$null=Invoke-SunshineServiceControl Restart $exe $shared
$null=Invoke-SunshineServiceControl Stop $exe $shared
$null=Invoke-SunshineServiceControl Stop $exe $shared
$null=Invoke-SunshineServiceControl Start $exe $shared
ConvertTo-Json -InputObject $script:events
''')
        self.assertEqual(result,['Stop:SunshineCustomService','Wait:Stopped','Start:SunshineCustomService','Wait:Running','Stop:SunshineCustomService','Wait:Stopped','Start:SunshineCustomService','Wait:Running'])

    def test_installed_service_conflict_and_mismatch_refuse_mutation(self):
        """@brief Preserve streaming if identity or port-conflict checks fail."""
        result=self.ps(self.fixture()+r'''
function Get-Service {param($Name,$ErrorAction) [pscustomobject]@{Status='Running'}}
$blocked=0
try{$null=Invoke-SunshineServiceControl Restart $exe $shared}catch{$blocked++}
$script:service.PathName='unexpected'
try{$null=Invoke-SunshineServiceControl Stop $exe $shared}catch{$blocked++}
@{Blocked=$blocked;Events=@($script:events)}|ConvertTo-Json
''')
        self.assertEqual(result,{'Blocked':2,'Events':[]})

    def test_matching_service_preferred_and_missing_service_allows_legacy(self):
        """@brief Start the matching service or permit the legacy launcher."""
        result=self.ps(self.fixture()+r'''
$first=Start-SunshineMatchingService $exe $shared
$script:service=$null
$second=Start-SunshineMatchingService $exe $shared
@{First=$first;Second=$second;Events=@($script:events)}|ConvertTo-Json
''')
        self.assertTrue(result['First'])
        self.assertFalse(result['Second'])
        self.assertEqual(result['Events'],['Start:SunshineCustomService','Wait:Running'])

    def test_registration_rejects_an_old_wrapper_before_uac_or_service_writes(self):
        """@brief Prevent standalone registration against an undeployed legacy wrapper."""
        exe=self.build('legacy package')
        tools=exe.parent/'tools'
        tools.mkdir()
        (tools/'sunshinesvc.exe').write_bytes(b'legacy wrapper')
        body="$exe="+quote(exe)+";$shared="+quote(exe.parent/'config')+r"""
$script:elevated=$false
function Start-Process {$script:elevated=$true;throw 'Unexpected elevation'}
$blocked=$false
try { & """+quote(ROOT/'scripts/custom/register-sunshine-service.ps1')+r""" -ExecutablePath $exe -ConfigDirectory $shared } catch {$blocked=$_.Exception.Message -like '*service-capable*'}
@{Blocked=$blocked;Elevated=$script:elevated}|ConvertTo-Json
"""
        self.assertEqual(self.ps(body),{'Blocked':True,'Elevated':False})

    def test_service_dacl_accepts_audit_sections_and_grants_only_control_rights(self):
        """@brief Preserve standard service permissions and avoid writing audit, owner or group sections."""
        result=self.ps(r"""
$sid=[Security.Principal.WindowsIdentity]::GetCurrent().User.Value
$inputDescriptor='O:SYG:SYD:(A;;CCDCLCSWRPWPDTLOCRSDRCWDWO;;;BA)S:(AU;FA;CCDCLCSWRPWPDTLOCRSDRCWDWO;;;WD)'
$outputDescriptor=Add-SunshineServiceControlAccess $inputDescriptor $sid
$parsed=[Security.AccessControl.RawSecurityDescriptor]::new($outputDescriptor)
$ace=$parsed.DiscretionaryAcl[$parsed.DiscretionaryAcl.Count-1]
$blocked=$false
try {$null=Add-SunshineServiceControlAccess 'O:SYG:SY' $sid}catch{$blocked=$true}
$inherited='D:(A;ID;CC;;;BA)'
$ordered=[Security.AccessControl.RawSecurityDescriptor]::new((Add-SunshineServiceControlAccess $inherited $sid))
@{Rights=$ace.AccessMask;Owner=$ace.SecurityIdentifier.Value;Expected=$sid;Count=$parsed.DiscretionaryAcl.Count;HasAudit=$outputDescriptor.Contains('S:');HasOwner=$outputDescriptor.Contains('O:');NullDaclBlocked=$blocked;ExplicitFirst=$ordered.DiscretionaryAcl[0].SecurityIdentifier.Value -eq $sid}|ConvertTo-Json
""")
        self.assertEqual(result['Rights'],0x200B5)
        self.assertEqual(result['Owner'],result['Expected'])
        self.assertEqual(result['Count'],2)
        self.assertFalse(result['HasAudit'])
        self.assertFalse(result['HasOwner'])
        self.assertTrue(result['NullDaclBlocked'])
        self.assertTrue(result['ExplicitFirst'])

    def test_scoped_service_wait_uses_expected_name_and_finite_timeout(self):
        """@brief Exercise the real wait wrapper with an inert controller."""
        result=self.ps(r'''
$script:waits=@()
function Get-Service {
 param($Name,$ErrorAction)
 if($Name -ne 'SunshineCustomService'){throw 'Wrong service'}
 $value=[pscustomobject]@{}
 $value|Add-Member ScriptMethod WaitForStatus {param($State,$Timeout) $script:waits+=@{State=[string]$State;Seconds=$Timeout.TotalSeconds}}
 return $value
}
Wait-SunshineCustomService Running
Wait-SunshineCustomService Stopped
ConvertTo-Json -InputObject $script:waits
''')
        self.assertEqual(result,[{'State':'Running','Seconds':45},{'State':'Stopped','Seconds':45}])


    def test_elevated_registration_reports_errors_and_cleans_temporary_files(self):
        """@brief Execute the real encoded child with inert registration and simulate UAC failures."""
        for mode in ('success', 'failure', 'empty', 'cancelled'):
            with self.subTest(mode=mode):
                folder = self.root / ("日本語 registration's " + mode)
                folder.mkdir()
                target = folder / 'register.ps1'
                arguments = folder / 'arguments.json'
                target.write_text(
                    'param($ExecutablePath,$ConfigDirectory,$OwnerSid)\n'
                    + '@{Executable=$ExecutablePath;Config=$ConfigDirectory;Owner=$OwnerSid}|ConvertTo-Json|Set-Content -Encoding UTF8 -LiteralPath '
                    + quote(arguments) + '\n'
                    + ('throw "登録テスト失敗"' if mode == 'failure' else 'exit 0'),
                    encoding='utf-8-sig',
                )
                exe = folder / 'sunshine.exe'
                config = folder / '共有 config'
                body = '$script:mode=' + quote(mode) + r"""
$script:report=$null
$script:options=$null
function Start-Process {
    param($FilePath,$ArgumentList,$Verb,$WindowStyle,[switch]$Wait,[switch]$PassThru)
    $bootstrap=[Text.Encoding]::Unicode.GetString([Convert]::FromBase64String($ArgumentList[-1]))
    $match=[regex]::Match($bootstrap,"FromBase64String\('([^']+)'\)")
    $payload=ConvertFrom-Json ([Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($match.Groups[1].Value)))
    $script:report=$payload.ErrorLog
    $script:options=@{Arguments=$ArgumentList[0..3];Verb=$Verb;WindowStyle=$WindowStyle;Wait=[bool]$Wait;PassThru=[bool]$PassThru}
    if($script:mode -eq 'cancelled'){throw 'UAC cancelled'}
    if($script:mode -eq 'empty'){return [pscustomobject]@{ExitCode=7}}
    & $FilePath @ArgumentList 1>$null 2>$null
    return [pscustomobject]@{ExitCode=$LASTEXITCODE}
}
$message=$null
try {
    Invoke-SunshineServiceRegistrationElevation -ScriptPath """ + quote(target) + ' -ExecutablePath ' + quote(exe) + ' -ConfigDirectory ' + quote(config) + r""" -OwnerSid 'S-1-5-21-123-456-789-1001'
} catch {$message=$_.Exception.Message}
@{Message=$message;Options=$script:options;ReportRemoved=!(Test-Path -LiteralPath $script:report)}|ConvertTo-Json -Depth 4
"""
                result = self.ps(body)
                self.assertTrue(result['ReportRemoved'])
                self.assertEqual(result['Options'], {
                    'Arguments': ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-EncodedCommand'],
                    'Verb': 'RunAs', 'WindowStyle': 'Hidden', 'Wait': True, 'PassThru': True,
                })
                if mode in ('success', 'failure'):
                    self.assertEqual(json.loads(arguments.read_text(encoding='utf-8-sig')), {
                        'Executable': str(exe), 'Config': str(config), 'Owner': 'S-1-5-21-123-456-789-1001',
                    })
                if mode == 'success':
                    self.assertIsNone(result['Message'])
                elif mode == 'failure':
                    self.assertIn('exit code 1', result['Message'])
                    self.assertIn('登録テスト失敗', result['Message'])
                elif mode == 'empty':
                    self.assertIn('exit code 7', result['Message'])
                    self.assertIn('did not return an error report', result['Message'])
                else:
                    self.assertEqual(result['Message'], 'UAC cancelled')

if __name__=='__main__':
    unittest.main()
