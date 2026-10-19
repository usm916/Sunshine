"""@file test_sunshine_task.py
@brief Verify fixed task definitions and launcher control without registering real tasks.
"""
import ctypes
import json
import subprocess
import unittest
from pathlib import Path
from test_shared_config import SharedConfigTests, quote, ROOT

LIBRARY = ROOT / "scripts/custom/scheduled-task.ps1"
REGISTER = ROOT / "scripts/custom/register-sunshine-task.ps1"
START = ROOT / "scripts/custom/start-sunshine.ps1"


class SunshineTaskTests(unittest.TestCase):
    """@brief Run real PowerShell helpers with isolated scheduler process boundaries."""

    setUp = SharedConfigTests.setUp
    build = SharedConfigTests.build

    def ps(self, body):
        """@brief Execute Unicode fixture scripts and return their JSON result."""
        path = self.root / "task test.ps1"
        path.write_text("$ErrorActionPreference='Stop'\n"
                        "$OutputEncoding=[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)\n"
                        ". " + quote(LIBRARY) + "\n" + body, encoding="utf-8-sig")
        result = subprocess.run(["powershell.exe", "-NoProfile", "-File", str(path)],
                                capture_output=True, encoding="utf-8", timeout=35)
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(result.stdout)

    def fixture(self):
        """@brief Construct task metadata pinned to a disposable executable and settings."""
        exe = self.build("日本語 build's folder")
        shared = self.root / "shared settings"
        body = "$exe=" + quote(exe) + ";$shared=" + quote(shared) + "\n"
        body += r"""
$task=[pscustomobject]@{
    TaskName=(Get-SunshineTaskName); State='Running'; Description='Sunshine Custom interactive launcher v2'
    Settings=[pscustomobject]@{Enabled=$true}
    Principal=[pscustomobject]@{UserId=[Security.Principal.WindowsIdentity]::GetCurrent().User.Value;LogonType='Interactive';RunLevel='Highest'}
    Actions=@([pscustomobject]@{Execute=(Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe');WorkingDirectory=(Split-Path $exe -Parent);Arguments=(Get-SunshineSupervisorArguments $exe $shared)})
}
"""
        return body

    def test_real_definition_is_interactive_highest_on_demand_without_time_limit(self):
        """@brief Inspect real Windows task objects without creating persistent scheduler state."""
        exe = self.build("task definition")
        shared = self.root / "not created"
        actual = self.ps("$d=& " + quote(REGISTER) + " -PrepareOnly -ExecutablePath " + quote(exe) +
                         " -ConfigDirectory " + quote(shared) + r"""
@{Name=$d.TaskName;Execute=$d.Actions[0].Execute;Arguments=$d.Actions[0].Arguments;
Work=$d.Actions[0].WorkingDirectory;Logon=[string]$d.Principal.LogonType;Level=[string]$d.Principal.RunLevel;
Limit=$d.Settings.ExecutionTimeLimit;Instances=[string]$d.Settings.MultipleInstances;
BatteryStart=$d.Settings.DisallowStartIfOnBatteries;BatteryStop=$d.Settings.StopIfGoingOnBatteries;
Demand=$d.Settings.AllowDemandStart;Triggers=@($d.Triggers|Where-Object{$null -ne $_}).Count} | ConvertTo-Json
""")
        self.assertTrue(actual["Name"].startswith("SunshineCustom-S-1-"))
        self.assertTrue(actual["Execute"].lower().endswith("windows\\system32\\windowspowershell\\v1.0\\powershell.exe"))
        self.assertEqual(actual["Work"], str(exe.parent))
        self.assertEqual(actual["Logon"], "Interactive")
        self.assertEqual(actual["Level"], "Highest")
        self.assertIn(actual["Limit"], ("PT0S", "P0D"))
        self.assertEqual(actual["Instances"], "IgnoreNew")
        self.assertFalse(actual["BatteryStart"])
        self.assertFalse(actual["BatteryStop"])
        self.assertTrue(actual["Demand"])
        self.assertEqual(actual["Triggers"], 0)
        self.assertFalse(shared.exists())

    def test_default_registration_resolves_paths_under_file_invocation(self):
        """@brief Resolve PSScriptRoot after parameter binding in Windows PowerShell 5."""
        result = subprocess.run(["powershell.exe", "-NoProfile", "-File", str(REGISTER), "-PrepareOnly"],
                                capture_output=True, encoding="utf-8", timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("SunshineCustom-", result.stdout)

    def test_native_argument_quoting_round_trips_without_shell_evaluation(self):
        """@brief Exercise the Windows parser for quotes, slashes and literal shell syntax."""
        values = ["", "simple", "with spaces", 'a"b', 'a\\"b', "C:\\trailing\\", "C:\\two\\\\",
                  "日本語 'single'", "$(throw 'no'); & exit 9"]
        body = "$values=@(" + ",".join(quote(v) for v in values) + ")\n"
        body += "$line='sunshine.exe '+(($values|ForEach-Object{ConvertTo-SunshineTaskArgument $_}) -join ' ');@{Line=$line}|ConvertTo-Json"
        command = self.ps(body)["Line"]
        shell = ctypes.WinDLL("shell32", use_last_error=True)
        shell.CommandLineToArgvW.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_int)]
        shell.CommandLineToArgvW.restype = ctypes.POINTER(ctypes.c_wchar_p)
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.LocalFree.argtypes = [ctypes.c_void_p]
        count = ctypes.c_int()
        result = shell.CommandLineToArgvW(command, ctypes.byref(count))
        try:
            self.assertEqual([result[i] for i in range(1, count.value)], values)
        finally:
            kernel.LocalFree(result)

    def test_matching_requires_owner_action_arguments_and_elevated_interactive_account(self):
        """@brief Reject altered task metadata while accepting SID and account-name ownership."""
        changes = ["$task=$null", "$task.Description='unrelated'", "$task.Settings.Enabled=$false",
                   "$task.Actions=@()", "$task.Actions=@($task.Actions[0],$task.Actions[0])",
                   "$task.Principal.LogonType='Password'", "$task.Principal.RunLevel='Limited'",
                   "$task.Principal.UserId='does-not-exist-sunshine-account'",
                   "$task.Principal.UserId='NT AUTHORITY\\SYSTEM'",
                   "$task.Actions[0].Execute='C:\\different.exe'",
                   "$task.Actions[0].WorkingDirectory='C:\\elsewhere'", "$task.Actions[0].Arguments='other'",
                   "$task.Principal.UserId=[Security.Principal.WindowsIdentity]::GetCurrent().Name", ""]
        # Rebuild the fixture for each case inside one PowerShell process.
        fixture = self.fixture()
        body = "$results=@()\n"
        for change in changes:
            body += fixture + change + "\n$results+=Test-SunshineRegisteredTask $task $exe $shared\n"
        body += "ConvertTo-Json -InputObject $results"
        self.assertEqual(self.ps(body), [False] * 12 + [True, True])

    def test_matching_task_starts_without_uac_and_missing_task_returns_false(self):
        """@brief Start only the current account's named, matching native task."""
        actual = self.ps(self.fixture() + r"""
$script:starts=@()
function Get-ScheduledTask {param($TaskPath,$ErrorAction) @([pscustomobject]@{TaskName='unrelated'},$script:task)}
function Start-ScheduledTask {param($TaskName,$TaskPath,$ErrorAction) $script:starts+=@{Name=$TaskName;Path=$TaskPath}}
function Start-Process {throw 'UAC must not be requested'}
$first=Start-SunshineMatchingTask $exe $shared
$script:task=$null
$second=Start-SunshineMatchingTask $exe $shared
@{First=$first;Second=$second;Starts=@($script:starts)}|ConvertTo-Json -Depth 4
""")
        self.assertTrue(actual["First"])
        self.assertFalse(actual["Second"])
        self.assertEqual(len(actual["Starts"]), 1)
        self.assertEqual(actual["Starts"][0]["Path"], "\\")

    def test_control_start_stop_restart_and_status_never_touch_unrelated_processes(self):
        """@brief Exercise task-only control and wait for stop before restart."""
        actual = self.ps(self.fixture() + r"""
$script:events=@()
function Get-ScheduledTask {param($TaskPath,$ErrorAction) $script:task}
function Start-ScheduledTask {param($TaskName,$TaskPath,$ErrorAction) $script:events+='Start';$script:task.State='Running'}
function Stop-ScheduledTask {param($TaskName,$TaskPath,$ErrorAction) $script:events+='Stop';$script:task.State='Ready'}
function Stop-Process {throw 'Unrelated process control is forbidden'}
function Start-Process {throw 'UAC must not be requested'}
$null=Invoke-SunshineTaskControl Status $exe $shared
$null=Invoke-SunshineTaskControl Restart $exe $shared
$null=Invoke-SunshineTaskControl Stop $exe $shared
$null=Invoke-SunshineTaskControl Stop $exe $shared
$null=Invoke-SunshineTaskControl Start $exe $shared
@{Events=@($script:events);State=$task.State}|ConvertTo-Json
""")
        self.assertEqual(actual["Events"], ["Stop", "Start", "Stop", "Start"])
        self.assertEqual(actual["State"], "Running")

    def test_control_rejects_mismatched_task_and_propagates_scheduler_errors(self):
        """@brief Preserve existing tasks and surface scheduler access failures."""
        actual = self.ps(self.fixture() + r"""
function Get-ScheduledTask {param($TaskPath,$ErrorAction) $script:task}
$task.Actions[0].Arguments='different'
$rejected=$false
try {$null=Invoke-SunshineTaskControl Restart $exe $shared} catch {$rejected=$true}
function Get-ScheduledTask {throw 'scheduler denied'}
try {$null=Get-SunshineRegisteredTask} catch {$errorText=$_.Exception.Message}
@{Rejected=$rejected;Error=$errorText}|ConvertTo-Json
""")
        self.assertTrue(actual["Rejected"])
        self.assertEqual(actual["Error"], "scheduler denied")

    def test_restart_does_not_start_another_host_when_stop_times_out(self):
        """@brief Bound failed stopping to ten seconds and prevent a duplicate start."""
        actual = self.ps(self.fixture() + r"""
function Get-ScheduledTask {param($TaskPath,$ErrorAction) $script:task}
function Stop-ScheduledTask {param($TaskName,$TaskPath,$ErrorAction)}
function Start-ScheduledTask {throw 'Must not start a second host'}
try {$null=Invoke-SunshineTaskControl Restart $exe $shared} catch {@{Error=$_.Exception.Message}|ConvertTo-Json}
""")
        self.assertIn("did not stop within ten seconds", actual["Error"])

    def test_start_launcher_reuses_task_before_preparing_store_or_requesting_uac(self):
        """@brief Run the actual launcher with a task-boundary mock and no native process."""
        # Define the task provider mock before the launcher imports its real helpers.
        actual = self.ps(self.fixture() + r"""
$global:taskFixture=$task
function Get-ScheduledTask {param($TaskPath,$ErrorAction) $global:taskFixture}
function Start-ScheduledTask {param($TaskName,$TaskPath,$ErrorAction) $global:startedTaskName=$TaskName}
function Start-Process {throw 'UAC must not be requested'}
""" + "& " + quote(START) + " -ExecutablePath $exe -ConfigDirectory $shared -RequireAdministrator -UseScheduledTask 6>$null | Out-Null\n" +
                         "@{Started=$global:startedTaskName;Created=Test-Path -LiteralPath $shared}|ConvertTo-Json")
        self.assertTrue(actual["Started"].startswith("SunshineCustom-"))
        self.assertFalse(actual["Created"])

    def test_windows_job_keeps_restarted_child_and_kills_it_on_close(self):
        """@brief Exercise real inherited Windows job membership across a native self-restart."""
        exe = self.root / 'restart fixture.exe'
        marker = self.root / 'child pid.txt'
        source = r'''
using System;using System.Diagnostics;using System.IO;using System.Threading;
public static class RestartFixture {
 public static void Main(string[] args) {
  if(args.Length==1) {
   Process.Start(new ProcessStartInfo(Process.GetCurrentProcess().MainModule.FileName,"\""+args[0]+"\" child"){UseShellExecute=false});
  } else {File.WriteAllText(args[0],Process.GetCurrentProcess().Id.ToString());Thread.Sleep(30000);}
 }
}
'''
        actual = self.ps("Add-Type -Path " + quote(ROOT / 'scripts/custom/task-job.cs') + "\n" +
                         "Add-Type -TypeDefinition " + quote(source) + " -OutputAssembly " + quote(exe) + " -OutputType ConsoleApplication\n" +
                         "$exe=" + quote(exe) + ";$marker=" + quote(marker) + r'''
$job=[SunshineTaskJob]::new()
try {
 $initial=$job.Start($exe,(ConvertTo-SunshineTaskArgument $exe)+' '+(ConvertTo-SunshineTaskArgument $marker),(Split-Path $exe -Parent))
 $deadline=[DateTime]::UtcNow.AddSeconds(10)
 while(!(Test-Path -LiteralPath $marker)) {if([DateTime]::UtcNow -gt $deadline){throw 'Child fixture did not start'};Start-Sleep -Milliseconds 50}
 $child=Get-Process -Id ([int](Get-Content -LiteralPath $marker))
 $rootExited=$true
 try {$parent=Get-Process -Id $initial;$rootExited=$parent.WaitForExit(5000)} catch [Microsoft.PowerShell.Commands.ProcessCommandException] {}
 $active=$job.ActiveProcesses
 $job.Dispose()
 $terminated=$child.WaitForExit(5000)
 @{RootExited=$rootExited;ActiveAfterRootExit=$active;ChildTerminated=$terminated}|ConvertTo-Json
} finally {$job.Dispose()}
''')
        self.assertTrue(actual['RootExited'])
        self.assertGreaterEqual(actual['ActiveAfterRootExit'], 1, actual)
        self.assertTrue(actual['ChildTerminated'])

    def test_empty_job_wait_and_failed_launch_release_handles(self):
        """@brief Reject failed native creation and use after disposal without leaving processes."""
        actual = self.ps("Add-Type -Path " + quote(ROOT / 'scripts/custom/task-job.cs') + r'''
$job=[SunshineTaskJob]::new()
$job.Wait()
$empty=$job.ActiveProcesses
$failure=$false
try {$null=$job.Start('D:\data\code\sunshineCustom\cmake-build-custom\missing-task-fixture.exe','missing','D:\data\code\sunshineCustom')} catch {$failure=$true}
$job.Dispose();$job.Dispose()
$disposed=$false
try {$null=$job.get_ActiveProcesses()} catch {$disposed=$true}
$startDisposed=$false
try {$null=$job.Start('missing','missing','C:\')} catch {$startDisposed=$true}
@{Empty=$empty;Failure=$failure;Disposed=$disposed;StartDisposed=$startDisposed}|ConvertTo-Json
''')
        self.assertEqual(actual['Empty'], 0)
        self.assertTrue(actual['Failure'])
        self.assertTrue(actual['Disposed'])
        self.assertTrue(actual['StartDisposed'])

    def test_task_supervisor_preserves_every_shared_native_argument(self):
        """@brief Run the real supervisor against a native recorder with Unicode and spaced paths."""
        exe = self.root / 'argument recorder.exe'
        shared = self.root / 'shared settings'
        shared.mkdir()
        source = 'using System.IO;using System.Text;public static class ArgRecorder {public static void Main(string[] args){File.WriteAllLines(args[0]+".arguments",args,Encoding.UTF8);}}'
        self.ps("Add-Type -TypeDefinition " + quote(source) + " -OutputAssembly " + quote(exe) + " -OutputType ConsoleApplication\n" +
                "& " + quote(ROOT / 'scripts/custom/run-sunshine-task.ps1') + " -ExecutablePath " + quote(exe) + " -ConfigDirectory " + quote(shared) + "\n@{Finished=$true}|ConvertTo-Json")
        arguments = (shared / 'sunshine.conf.arguments').read_text(encoding='utf-8-sig').splitlines()
        self.assertEqual(arguments, [str(shared / 'sunshine.conf'),
            'file_state=' + str(shared / 'sunshine_state.json'),
            'credentials_file=' + str(shared / 'sunshine_state.json'),
            'pkey=' + str(shared / 'credentials/cakey.pem'),
            'cert=' + str(shared / 'credentials/cacert.pem'),
            'file_apps=' + str(shared / 'apps.json'),
            'log_path=' + str(shared / 'sunshine.log')])

    def test_registration_rejects_a_package_without_supervisor_helpers_before_uac(self):
        """@brief Require the complete pinned supervisor before creating an elevated task."""
        exe = self.build('incomplete package')
        actual = self.ps("function Start-Process {throw 'UAC must not be requested'}\n" +
                         "try { & " + quote(REGISTER) + " -ExecutablePath " + quote(exe) + " } catch {@{Error=$_.Exception.Message}|ConvertTo-Json}")
        self.assertIn('missing the supervised task helpers', actual['Error'])

    def test_registration_rejects_missing_executable_and_another_owner(self):
        """@brief Reject invalid registration before any task mutation or UAC."""
        actual = self.ps("$errors=@()\n" +
                         "try { & " + quote(REGISTER) + " -PrepareOnly -ExecutablePath " + quote(self.root / 'missing.exe') + " } catch {$errors+=$_.Exception.Message}\n" +
                         "try { & " + quote(REGISTER) + " -PrepareOnly -OwnerSid 'different' } catch {$errors+=$_.Exception.Message}\n" +
                         "try { & " + quote(REGISTER) + " -PrepareOnly -Remove } catch {$errors+=$_.Exception.Message}\n" +
                         "ConvertTo-Json -InputObject $errors")
        self.assertIn("does not exist", actual[0])
        self.assertIn("same signed-in", actual[1])
        self.assertIn("cannot be combined", actual[2])


del SharedConfigTests

if __name__ == "__main__":
    unittest.main(verbosity=2)
