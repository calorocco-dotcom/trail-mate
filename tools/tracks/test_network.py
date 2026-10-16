"""Independent native processes using loopback TCP only."""
import json
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
import unittest


class NetworkTests(unittest.TestCase):
    def test_native_capabilities(self):
        with tempfile.TemporaryDirectory() as work:
            root=Path(work)
            with socket.socket() as listener:
                listener.bind(('127.0.0.1',0))
                port=listener.getsockname()[1]
            base='[reticulum]\nshare_instance = No\nenable_transport = Yes\n[interfaces]\n[[Local track test]]\nenabled = Yes\n'
            server_config=root/'server-rns';server_config.mkdir()
            client_config=root/'client-rns';client_config.mkdir()
            (server_config/'config').write_text(base+f'type = TCPServerInterface\nlisten_ip = 127.0.0.1\nlisten_port = {port}\n')
            (client_config/'config').write_text(base+f'type = TCPClientInterface\ntarget_host = 127.0.0.1\ntarget_port = {port}\n')
            log_path=root/'server.log'
            with log_path.open('w') as log:
                server=subprocess.Popen([sys.executable,'-m','tools.tracks.directory_service','--state',str(root/'server'),'--rns-config',str(server_config),'--run-seconds','90'],stdout=log,stderr=subprocess.STDOUT,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
                try:
                    ready=None;deadline=time.monotonic()+10
                    while server.poll() is None and time.monotonic()<deadline:
                        for line in log_path.read_text().splitlines():
                            if line.startswith('{'):
                                ready=json.loads(line)
                        if ready:
                            break
                        time.sleep(0.1)
                    self.assertIsNotNone(ready,log_path.read_text())
                    client=subprocess.run([sys.executable,'-m','tools.tracks.network_probe','--state',str(root/'client'),'--rns-config',str(client_config),'--delivery',ready['delivery']],capture_output=True,text=True,timeout=65,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
                    self.assertEqual(client.returncode,0,client.stdout+client.stderr+'\n'+log_path.read_text())
                    self.assertIn('round trip passed',client.stdout)
                finally:
                    if server.poll() is None:
                        server.terminate()
                    server.wait(timeout=10)
            reader_config=root/'reader-rns';reader_config.mkdir()
            (reader_config/'config').write_text((client_config/'config').read_text())
            restart_log=root/'restart.log'
            with restart_log.open('w') as log:
                server=subprocess.Popen([sys.executable,'-m','tools.tracks.directory_service','--state',str(root/'server'),'--rns-config',str(server_config),'--run-seconds','90'],stdout=log,stderr=subprocess.STDOUT,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
                try:
                    restarted=None;deadline=time.monotonic()+10
                    while server.poll() is None and time.monotonic()<deadline:
                        for line in restart_log.read_text().splitlines():
                            if line.startswith('{'):
                                restarted=json.loads(line)
                        if restarted:
                            break
                        time.sleep(0.1)
                    self.assertIsNotNone(restarted,restart_log.read_text())
                    for field in ('delivery','discovery','epoch'):
                        self.assertEqual(restarted[field],ready[field])
                    reader=subprocess.run([sys.executable,'-m','tools.tracks.network_probe','--state',str(root/'reader'),'--rns-config',str(reader_config),'--delivery',restarted['delivery'],'--read-info',str(root/'client'/'publication.json')],capture_output=True,text=True,timeout=65,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
                    self.assertEqual(reader.returncode,0,reader.stdout+reader.stderr+'\n'+restart_log.read_text())
                    self.assertIn('fresh-reader restart round trip passed',reader.stdout)
                finally:
                    if server.poll() is None:
                        server.terminate()
                    server.wait(timeout=10)
