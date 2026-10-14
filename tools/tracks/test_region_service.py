import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


class RegionServiceTests(unittest.TestCase):
    def test_optional_pack_startup_and_pair_validation_without_external_interfaces(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            config = root/'rns'
            config.mkdir()
            (config/'config').write_text('[reticulum]\nshare_instance = No\nenable_transport = No\n[interfaces]\n')
            pack = root/'regions.json'
            pack.write_text(json.dumps({"type":"FeatureCollection","features":[{"type":"Feature","id":"root","properties":{"name":"Root"},"geometry":{"type":"Polygon","coordinates":[[[0,0],[1,0],[1,1],[0,1],[0,0]]]}}]}))
            base = [sys.executable,'-m','tools.tracks.directory_service','--state',str(root/'state'),'--rns-config',str(config),'--run-seconds','0.3']
            missing = subprocess.run(base+['--regions-file',str(pack)],capture_output=True,text=True,timeout=30)
            self.assertNotEqual(missing.returncode,0)
            self.assertIn('must be supplied together',missing.stderr)
            self.assertFalse((root/'state').exists())
            ready = subprocess.run(base+['--regions-file',str(pack),'--regions-namespace','example'],capture_output=True,text=True,timeout=30)
            self.assertEqual(ready.returncode,0,ready.stderr)
            self.assertTrue(any(line.startswith('{') for line in ready.stdout.splitlines()))
            self.assertTrue((root/'state'/'identity').is_file())


if __name__ == '__main__':
    unittest.main()
