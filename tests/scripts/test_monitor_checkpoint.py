"""@file test_monitor_checkpoint.py
@brief Exercise checkpoint validation without changing any services or user configuration.
"""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[2] / 'scripts/custom/rollback-monitor-switch.ps1'

class MonitorCheckpointTests(unittest.TestCase):
    """Validate the user-facing restore command against isolated pinned packages."""
    def setUp(self):
        """Create a fake package and a runner that must never execute during validation."""
        self.temp = tempfile.TemporaryDirectory(prefix='sunshine checkpoint ')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.build = self.root / 'cmake-build-custom'
        self.stage = self.build / 'package-crop-margin-20'
        self.stage.mkdir(parents=True)
        self.asset = self.stage / 'asset.txt'
        self.asset.write_text('baseline', encoding='utf-8')
        self.runner = self.build / 'restore.ps1'
        self.runner.write_text("throw 'must not run during validation'", encoding='utf-8')
        self.checkpoint = {'crop_margin_pixels':20, 'monitor_switch_supported':False, 'package':self.stage.name,
                           'runner':self.runner.name, 'runner_sha256':hashlib.sha256(self.runner.read_bytes()).hexdigest(),
                           'files':[{'path':self.asset.name,'sha256':hashlib.sha256(self.asset.read_bytes()).hexdigest()}]}
        self.save()
    def save(self):
        """Write the current isolated checkpoint metadata."""
        (self.build / 'checkpoint-crop20.json').write_text(json.dumps(self.checkpoint), encoding='utf-8')
    def check(self):
        """Run only the non-mutating validation branch of Windows PowerShell."""
        env=os.environ.copy()
        env.pop('PSModulePath',None)
        return subprocess.run(['powershell.exe','-NoProfile','-File',str(SCRIPT),'-RepositoryRoot',str(self.root),'-CheckOnly'],capture_output=True,text=True,encoding='utf-8',errors='replace',env=env,timeout=30)
    def test_valid_checkpoint_is_read_only(self):
        """A valid package passes without running its helper or changing the files."""
        result=self.check()
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertIn('Validated',result.stdout)
        self.assertEqual(self.asset.read_text(encoding='utf-8'),'baseline')
    def test_rejects_modified_package_file(self):
        """Changed package bytes are rejected before a service can be stopped."""
        self.asset.write_text('changed',encoding='utf-8')
        result=self.check();self.assertNotEqual(result.returncode,0);self.assertIn('Checkpoint file changed',result.stderr)
    def test_rejects_modified_restore_helper(self):
        """The executable restore script is pinned as well as the package."""
        self.runner.write_text('changed',encoding='utf-8')
        result=self.check();self.assertNotEqual(result.returncode,0);self.assertIn('Restore helper differs',result.stderr)
    def test_rejects_escape_and_extra_files(self):
        """Neither directory traversal nor extra package files are accepted."""
        self.checkpoint['files'][0]['path']='../outside.txt';self.save()
        result=self.check();self.assertNotEqual(result.returncode,0);self.assertIn('escaped its package',result.stderr)
        self.checkpoint['files'][0]['path']='asset.txt';self.save();(self.stage/'extra.txt').write_text('extra',encoding='utf-8')
        result=self.check();self.assertNotEqual(result.returncode,0);self.assertIn('file count changed',result.stderr)
    def test_rejects_wrong_baseline_and_root_escape(self):
        """Only the intended 20px checkpoint under the build root is accepted."""
        self.checkpoint['crop_margin_pixels']=50;self.save()
        result=self.check();self.assertNotEqual(result.returncode,0);self.assertIn('not the single-monitor',result.stderr)
        self.checkpoint['crop_margin_pixels']=20;self.checkpoint['package']='../outside';self.save()
        result=self.check();self.assertNotEqual(result.returncode,0);self.assertIn('escaped the build directory',result.stderr)

if __name__ == '__main__':
    unittest.main()
