"""Bounded native startup test without external network interfaces."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


class ServiceTests(unittest.TestCase):
    def test_native_startup_and_identity_restart(self):
        with tempfile.TemporaryDirectory() as work:
            root=Path(work)
            config=root/'rns'
            config.mkdir()
            (config/'config').write_text('[reticulum]\nshare_instance = No\nenable_transport = No\n[interfaces]\n')
            state=root/'state'
            command=[sys.executable,'-m','tools.tracks.directory_service','--state',str(state),'--rns-config',str(config),'--run-seconds','0.3']
            first=subprocess.run(command,capture_output=True,text=True,timeout=30)
            self.assertEqual(first.returncode,0,first.stderr)
            first_ready=[json.loads(line) for line in first.stdout.splitlines() if line.startswith('{')][0]
            second=subprocess.run(command,capture_output=True,text=True,timeout=30)
            self.assertEqual(second.returncode,0,second.stderr)
            second_ready=[json.loads(line) for line in second.stdout.splitlines() if line.startswith('{')][0]
            self.assertEqual(first_ready,second_ready)
            self.assertEqual((config/'config').read_text(),'[reticulum]\nshare_instance = No\nenable_transport = No\n[interfaces]\n')
