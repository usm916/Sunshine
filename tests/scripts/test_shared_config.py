"""@file test_shared_config.py
@brief Verify persistent configuration migration and native launch arguments.
"""
import base64
import json
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts/custom/start-sunshine.ps1"
WORK = ROOT / "cmake-build-shared-config-tests"

def quote(value):
    """@brief Quote a literal PowerShell string, including embedded apostrophes."""
    return "'" + str(value).replace("'", "''") + "'"

class SharedConfigTests(unittest.TestCase):
    """@brief Exercise migration using independent, disposable build directories."""

    def setUp(self):
        """@brief Create a fixture root confined to the workspace build directory."""
        WORK.mkdir(exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(dir=WORK)
        self.root = Path(self.temp.name).resolve()
        self.root.relative_to(WORK.resolve())
        self.addCleanup(self.temp.cleanup)

    def build(self, name, configured=True):
        """@brief Create a fake build whose settings can be migrated without launching."""
        folder = self.root / name
        folder.mkdir()
        exe = folder / "sunshine.exe"
        exe.write_bytes(b"prepare-only test fixture")
        if configured:
            config = folder / "config"
            (config / "credentials").mkdir(parents=True)
            for relative, content in {
                "sunshine.conf": "cursor_focus = enabled\n",
                "sunshine_state.json": '{"username":"test","password":"hash","salt":"salt","root":{"uniqueid":"host","devices":[]}}',
                "apps.json": '{"env":{},"apps":[]}',
                "credentials/cakey.pem": "test-private-key",
                "credentials/cacert.pem": "test-certificate",
            }.items():
                (config / relative).write_text(content, encoding="utf-8")
        return exe

    def invoke(self, exe, shared, prepare=True, extra=None, require_admin=False):
        """@brief Run the actual launcher and optionally parse its prepared command."""
        command = "$OutputEncoding = [Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false); "
        command += "& " + quote(SCRIPT)
        if exe is not None:
            command += " -ExecutablePath " + quote(exe)
        command += " -ConfigDirectory " + quote(shared)
        if require_admin:
            command += " -RequireAdministrator"
        if prepare:
            command += " -PrepareOnly"
        if extra:
            command += " -SunshineArguments " + quote(extra)
        if prepare:
            command += " | ConvertTo-Json -Depth 5"
        return subprocess.run(
            ["powershell.exe", "-NoProfile", "-Command", command],
            capture_output=True, encoding="utf-8", timeout=30,
        )

    def prepared(self, exe, shared):
        """@brief Require successful preparation and decode its non-secret metadata."""
        result = self.invoke(exe, shared)
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(result.stdout)

    def test_initial_migration_preserves_identity_and_settings(self):
        """@brief Preserve credentials, server identity, certificate and applications."""
        exe = self.build("first build")
        shared = self.root / "shared"
        details = self.prepared(exe, shared)
        self.assertEqual(details["MigratedFrom"], str(exe.parent / "config"))
        for source in (exe.parent / "config").rglob("*"):
            if source.is_file():
                self.assertEqual(source.read_bytes(), (shared / source.relative_to(exe.parent / "config")).read_bytes())

    def test_second_build_reuses_shared_files_without_overwriting(self):
        """@brief Ignore replacement build settings once the shared store exists."""
        first = self.build("first")
        second = self.build("second")
        shared = self.root / "shared"
        self.prepared(first, shared)
        before = {str(p.relative_to(shared)): p.read_bytes() for p in shared.rglob("*") if p.is_file()}
        (second.parent / "config/sunshine_state.json").write_text("different identity", encoding="utf-8")
        details = self.prepared(second, shared)
        self.assertIsNone(details["MigratedFrom"])
        after = {str(p.relative_to(shared)): p.read_bytes() for p in shared.rglob("*") if p.is_file()}
        self.assertEqual(before, after)
        self.assertEqual(details["ExecutablePath"], str(second))

    def test_all_persistent_paths_point_to_shared_store(self):
        """@brief Direct authentication, pairing, keys, apps and logs to one store."""
        exe = self.build("日本語 build's folder")
        shared = self.root / "共有 settings"
        details = self.prepared(exe, shared)
        arguments = details["Arguments"]
        self.assertEqual(arguments[0], str(shared / "sunshine.conf"))
        expected = {
            "file_state": "sunshine_state.json", "credentials_file": "sunshine_state.json",
            "pkey": "credentials/cakey.pem", "cert": "credentials/cacert.pem",
            "file_apps": "apps.json", "log_path": "sunshine.log",
        }
        self.assertEqual(dict(a.split("=", 1) for a in arguments[1:]),
                         {k: str(shared / v) for k, v in expected.items()})

    def test_empty_build_prepares_initial_configuration(self):
        """@brief Permit an initial setup when the build has no prior settings."""
        shared = self.root / "shared"
        details = self.prepared(self.build("empty", configured=False), shared)
        self.assertIsNone(details["MigratedFrom"])
        self.assertEqual((shared / "sunshine.conf").read_bytes(), b"")

    def test_existing_empty_store_is_not_reseeded(self):
        """@brief Respect an explicitly created shared store and create its config."""
        shared = self.root / "shared"
        shared.mkdir()
        details = self.prepared(self.build("build"), shared)
        self.assertIsNone(details["MigratedFrom"])
        self.assertFalse((shared / "sunshine_state.json").exists())
        self.assertTrue((shared / "sunshine.conf").is_file())

    def test_missing_executable_does_not_create_store(self):
        """@brief Reject a nonexistent build before creating persistent files."""
        shared = self.root / "shared"
        result = self.invoke(self.root / "missing.exe", shared)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(shared.exists())

    def test_directory_as_executable_is_rejected(self):
        """@brief Reject a directory in place of a native executable."""
        exe = self.build("build")
        shared = self.root / "shared"
        self.assertNotEqual(self.invoke(exe.parent, shared).returncode, 0)
        self.assertFalse(shared.exists())

    def test_file_as_configuration_directory_is_rejected(self):
        """@brief Leave an existing file intact when a directory was expected."""
        shared = self.root / "not-a-directory"
        shared.write_bytes(b"keep")
        self.assertNotEqual(self.invoke(self.build("build"), shared).returncode, 0)
        self.assertEqual(shared.read_bytes(), b"keep")

    def test_default_executable_works_when_not_supplied(self):
        """@brief Resolve the default after PowerShell initializes the script directory."""
        shared = self.root / "default shared"
        shared.mkdir()
        details = self.prepared(None, shared)
        self.assertEqual(details["ExecutablePath"], str(ROOT / "cmake-build-custom/package/sunshine.exe"))
        self.assertIsNone(details["MigratedFrom"])

    def test_native_version_launch_preserves_shared_identity(self):
        """@brief Run the real native binary through the launcher without starting servers."""
        exe = ROOT / "cmake-build-custom/package-cursor-focus/sunshine.exe"
        self.assertTrue(exe.is_file())
        shared = self.root / "native shared"
        shared.mkdir()
        (shared / "sunshine.conf").write_text("", encoding="utf-8")
        state = shared / "sunshine_state.json"
        state.write_bytes(b'{"username":"test","password":"hash","salt":"salt","root":{"uniqueid":"host","devices":[]}}')
        before = state.read_bytes()
        result = self.invoke(exe, shared, prepare=False, extra="--version")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Sunshine", result.stdout)
        self.assertEqual(state.read_bytes(), before)


    def elevation_command(self, launcher, exe, shared, arguments, cancelled=False):
        """@brief Exercise the real elevation helper while replacing only Windows process creation."""
        mock = (
            "throw [System.ComponentModel.Win32Exception]::new(1223)"
            if cancelled else
            "$script:captured = @{FilePath=$FilePath; Verb=$Verb; WindowStyle=$WindowStyle; "
            "ArgumentList=$ArgumentList; Wait=$Wait.IsPresent; PassThru=$PassThru.IsPresent}; "
            "[pscustomobject]@{ExitCode=37}"
        )
        command = "$OutputEncoding = [Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false); "
        command += "$ast = [System.Management.Automation.Language.Parser]::ParseFile(" + quote(SCRIPT) + ",[ref]$null,[ref]$null); "
        command += "$ast.FindAll({param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst]},$false) | ForEach-Object {Invoke-Expression $_.Extent.Text}; "
        command += "function Start-Process {param($FilePath,$Verb,$WindowStyle,$ArgumentList,[switch]$Wait,[switch]$PassThru,$ErrorAction) " + mock + "}; "
        command += "try {$code = Invoke-SunshineElevation -ScriptPath " + quote(launcher)
        command += " -ExecutablePath " + quote(exe) + " -ConfigDirectory " + quote(shared)
        command += " -SunshineArguments @(" + ",".join(quote(a) for a in arguments) + "); "
        command += "@{ExitCode=$code; Launch=$script:captured} | ConvertTo-Json -Depth 5} catch {@{Cancelled=$true; Error=$_.Exception.Message} | ConvertTo-Json}"
        result = subprocess.run(["powershell.exe", "-NoProfile", "-Command", command],
                                capture_output=True, encoding="utf-8", timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(result.stdout)

    def test_prepare_only_does_not_elevate_or_change_shared_path(self):
        """@brief Allow noninteractive inspection even when CMD requests administrator rights."""
        exe = self.build("administrator build")
        shared = self.root / "original user settings"
        result = self.invoke(exe, shared, require_admin=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)["ConfigDirectory"], str(shared))

    def test_elevation_preserves_literal_arguments_and_requests_windows_uac(self):
        """@brief Preserve Unicode, quoting and empty argument arrays through the encoded elevated bootstrap."""
        exe = self.build("日本語 build's folder")
        shared = self.root / "original user's settings"
        recorder = self.root / "record launcher.ps1"
        recorder.write_text(
            "param([switch]$RequireAdministrator,[string]$ExecutablePath,[string]$ConfigDirectory,[string[]]$SunshineArguments=@())\n"
            "@{Admin=$RequireAdministrator.IsPresent; ExecutablePath=$ExecutablePath; ConfigDirectory=$ConfigDirectory; Arguments=@($SunshineArguments)} | ConvertTo-Json -Depth 4\n",
            encoding="utf-8-sig")
        for arguments in [[], ["port=48189", "value=日本語 'quotes' \"double\"", "literal=$(throw 'must not execute'); & exit 9"]]:
            with self.subTest(arguments=arguments):
                details = self.elevation_command(recorder, exe, shared, arguments)
                self.assertEqual(details["ExitCode"], 37)
                launch = details["Launch"]
                self.assertEqual(launch["Verb"], "RunAs")
                self.assertEqual(launch["WindowStyle"], "Hidden")
                self.assertTrue(launch["Wait"])
                self.assertTrue(launch["PassThru"])
                self.assertEqual(launch["ArgumentList"][:2], ["-NoProfile", "-EncodedCommand"])
                bootstrap = base64.b64decode(launch["ArgumentList"][2]).decode("utf-16-le")
                payload = json.loads(base64.b64decode(re.search(r"FromBase64String\('([^']+)'\)", bootstrap)[1]).decode("utf-8"))
                self.assertEqual(payload["SunshineArguments"], arguments)
                result = subprocess.run(["powershell.exe", "-NoProfile", "-EncodedCommand", launch["ArgumentList"][2]],
                                        capture_output=True, encoding="utf-8", timeout=30)
                self.assertEqual(result.returncode, 0, result.stderr)
                actual = json.loads(result.stdout)
                self.assertTrue(actual["Admin"])
                self.assertEqual(actual["ExecutablePath"], str(exe))
                self.assertEqual(actual["ConfigDirectory"], str(shared))
                self.assertEqual(actual["Arguments"], arguments)
                self.assertFalse(shared.exists())

    def test_elevated_bootstrap_propagates_native_failure_exit_code(self):
        """@brief Preserve native failure status instead of reporting an elevated launch as successful."""
        recorder = self.root / "failure.ps1"
        recorder.write_text("param([switch]$RequireAdministrator,[string]$ExecutablePath,[string]$ConfigDirectory,[string[]]$SunshineArguments)\nexit 7\n", encoding="utf-8-sig")
        details = self.elevation_command(recorder, self.build("build"), self.root / "shared", [])
        result = subprocess.run(["powershell.exe", "-NoProfile", "-EncodedCommand", details["Launch"]["ArgumentList"][2]], capture_output=True, timeout=30)
        self.assertEqual(result.returncode, 7, result.stderr)

    def test_cancelled_elevation_does_not_launch_or_create_shared_store(self):
        """@brief Propagate a declined UAC request without silently running Sunshine unelevated."""
        shared = self.root / "not created"
        details = self.elevation_command(SCRIPT, self.build("build"), shared, [], cancelled=True)
        self.assertTrue(details["Cancelled"])
        self.assertFalse(shared.exists())

if __name__ == "__main__":
    unittest.main(verbosity=2)
