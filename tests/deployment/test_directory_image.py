"""Import the exact Docker COPY source closure outside the repository."""
import json
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
DEPLOY = ROOT / 'tools/geocaching/deploy'


class DirectoryImageTests(unittest.TestCase):
    def test_actual_runtime_modules_import_from_staged_image_sources(self):
        dockerfile = (DEPLOY / 'Dockerfile').read_text()
        allowed = {line[1:] for line in (DEPLOY / 'Dockerfile.dockerignore').read_text().splitlines()
                   if line.startswith('!') and not line.endswith('/')}
        with tempfile.TemporaryDirectory() as work:
            stage = Path(work)
            copied = []
            for line in dockerfile.splitlines():
                if not line.startswith('COPY '):
                    continue
                fields = shlex.split(line)[1:]
                destination = fields[-1]
                self.assertTrue(destination.startswith('/app/'))
                directory = stage / destination.removeprefix('/app/')
                directory.mkdir(parents=True, exist_ok=True)
                for source in fields[:-1]:
                    self.assertIn(source, allowed, f'{source} excluded from Docker context')
                    shutil.copyfile(ROOT / source, directory / Path(source).name)
                    copied.append(source)
            script = """import sys,json
sys.path.insert(0, sys.argv[1])
import directory_service, packet_bridge, reply_queue
from tools.tracks.directory_service import TrackDirectoryService
from tools.tracks.application import TrackApplication
from tools.tracks.region_catalog import RegionCatalog
from tools.tracks.attachment_index import match_index
print(json.dumps({'geocaching':directory_service.__file__,'tracks':sys.modules['tools.tracks.directory_service'].__file__,'reply':reply_queue.__file__}))
"""
            result = subprocess.run([sys.executable, '-I', '-c', script, str(stage)],
                                    cwd=stage, capture_output=True, text=True,
                                    creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            self.assertEqual(result.returncode, 0, result.stderr)
            modules = json.loads(result.stdout)
            for file in modules.values():
                self.assertTrue(Path(file).is_relative_to(stage), file)
            self.assertIn('tools/geocaching/reply_queue.py', copied)
            self.assertIn('tools/tracks/directory_service.py', copied)
            for arguments in ([str(stage/'directory_service.py'),'--help'],
                              ['-m','tools.tracks.directory_service','--help']):
                check = subprocess.run([sys.executable, *arguments], cwd=stage,
                                       capture_output=True, text=True,
                                       creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
                self.assertEqual(check.returncode, 0, check.stderr)
                self.assertIn('--rns-config', check.stdout)


if __name__ == '__main__':
    unittest.main()
