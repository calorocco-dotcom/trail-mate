"""Real native services share one RNS instance and one browser packet gateway.

Creates isolated local records only. No physical USB, RF or public publication.
Requires site dependencies and a Playwright browser (optional channel environment).
"""
import http.server
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import threading
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools/geocaching'))
import RNS
from directory_service import load_identity
from directory_store import DirectoryStore, packed


def free_port():
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0))
        return listener.getsockname()[1]


class SharedDirectoryNetworkTests(unittest.TestCase):
    def test_both_protocols_and_author_signatures_through_one_shared_gateway(self):
        retained = ROOT/'.codex-build/hostlink-codec/shared-directory-network'
        retained.mkdir(parents=True, exist_ok=True)
        (retained/'NOTES.txt').write_text('Local shared RNS owner + track client + packet gateway acceptance. Current production Python services and browser clients. Isolated signatures, no USB/RF/public publication. Retain compact evidence, remove reproducible build after acceptance.')
        children = []
        flags = getattr(subprocess, 'CREATE_NO_WINDOW', 0)
        with tempfile.TemporaryDirectory() as work:
            state = Path(work)
            rns = state/'rns'
            rns.mkdir()
            port, shared, control = free_port(), free_port(), free_port()
            config = ('[reticulum]\nshare_instance = Yes\nshared_instance_type = tcp\n'
                      f'shared_instance_port = {shared}\ninstance_control_port = {control}\n'
                      'enable_transport = Yes\n[interfaces]\n[[Browser gateway]]\n'
                      f'enabled = Yes\ntype = TCPServerInterface\nlisten_ip = 127.0.0.1\nlisten_port = {port}\n')
            (rns/'config').write_text(config)
            geo_state = state/'geocaching'
            geo_state.mkdir()
            identity = load_identity(geo_state)
            destination = RNS.Destination.hash_from_name_and_identity('lxmf.delivery', identity)
            author = RNS.Identity()
            record = [1, author.get_public_key(), os.urandom(16), 1, None, 0,
                      300000000, 1200000000, 'Shared gateway local cache', '', '', 2, 2, 0, 100, 100]
            raw = packed(record)
            store = DirectoryStore(geo_state/'directory.sqlite', destination)
            try:
                reply = store.handle(os.urandom(16), packed([1,0,1,os.urandom(16),8192,
                                     [[raw,author.sign(b'trailmate.geocache/sign/v1\0'+raw)]]]))
                self.assertIsNotNone(reply)
            finally:
                store.close()

            class Page(http.server.BaseHTTPRequestHandler):
                def do_GET(self):
                    self.send_response(200)
                    self.send_header('Content-Type','text/html')
                    self.end_headers()
                    self.wfile.write(b'<!doctype html><meta charset="utf-8"><title>Shared gateway acceptance</title>')
                def log_message(self, *_):
                    pass

            web = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Page)
            web_thread = threading.Thread(target=web.serve_forever, daemon=True)
            web_thread.start()
            origin = f'http://127.0.0.1:{web.server_port}'

            def start(label, arguments):
                log_path = retained/(label+'.log')
                handle = log_path.open('w', encoding='utf-8')
                child = subprocess.Popen([sys.executable, *arguments], cwd=ROOT,
                                         stdout=handle, stderr=subprocess.STDOUT, creationflags=flags)
                children.append((child,handle))
                deadline = time.monotonic()+25
                while child.poll() is None and time.monotonic()<deadline:
                    for line in log_path.read_text(encoding='utf-8').splitlines():
                        try:
                            event = json.loads(line)
                        except ValueError:
                            continue
                        if event.get('event') in ('ready','bridge_ready'):
                            return event
                    time.sleep(.1)
                self.fail(label+' did not start: '+log_path.read_text(encoding='utf-8'))

            try:
                geo = start('geocaching',['tools/geocaching/directory_service.py','--state',str(geo_state),'--rns-config',str(rns),'--startup-delay','0','--run-seconds','180'])
                tracks = start('tracks',['-m','tools.tracks.directory_service','--state',str(state/'tracks'),'--rns-config',str(rns),'--run-seconds','180'])
                self.assertNotEqual(geo['delivery'], tracks['delivery'])
                bridge = start('bridge',['tools/geocaching/packet_bridge.py','--tcp-port',str(port),'--port','0','--origin',origin,'--run-seconds','180'])
                # Native publisher transfers a real KML and signed manifest to
                # the track process through the primary owner's TCP interface.
                publisher = state/'publisher-rns'
                publisher.mkdir()
                (publisher/'config').write_text('[reticulum]\nshare_instance = No\n[interfaces]\n[[Owner]]\nenabled = Yes\ntype = TCPClientInterface\ntarget_host = 127.0.0.1\n'+f'target_port = {port}\n')
                published = subprocess.run([sys.executable,'-m','tools.tracks.network_probe','--state',str(state/'publisher'),'--rns-config',str(publisher),'--delivery',tracks['delivery']],cwd=ROOT,capture_output=True,text=True,timeout=70,creationflags=flags)
                (retained/'publication.log').write_text(published.stdout+published.stderr)
                self.assertEqual(published.returncode,0,published.stdout+published.stderr)
                reports = {}
                for kind, ready in (('geocaching',geo),('tracks',tracks)):
                    environment = {**os.environ,'TRAIL_MATE_LIVE_PROBE_WORK':str(retained/'probe')}
                    probe = subprocess.run(['node','tools/tracks/browser_network_probe.mjs',f'ws://127.0.0.1:{bridge["port"]}/',origin+'/',kind,'45',ready['discovery']],cwd=ROOT,env=environment,capture_output=True,text=True,timeout=65,creationflags=flags)
                    (retained/(kind+'-browser.log')).write_text(probe.stdout+probe.stderr)
                    self.assertEqual(probe.returncode,0,probe.stdout+probe.stderr)
                    report = json.loads((retained/'probe'/('result-'+kind+'.json')).read_text())
                    self.assertTrue(report['discoveryComplete'])
                    self.assertEqual(len(report['directories']),1)
                    self.assertEqual(report['directories'][0]['delivery'],ready['delivery'])
                    self.assertTrue(report['query']['authenticated'])
                    self.assertEqual(report['query']['count'],1)
                    self.assertTrue(report['query']['first']['authorSignatureVerified'])
                    self.assertTrue(report['closed'])
                    self.assertFalse(report['onlineAfterClose'])
                    reports[kind] = report
                self.assertEqual((rns/'config').read_text(),config)
                (retained/'result.json').write_text(json.dumps({'passed':True,'sharedRns':True,'singleGateway':True,'physicalUsb':False,'publicNetwork':False,'reports':reports},indent=2))
            finally:
                for child, handle in reversed(children):
                    if child.poll() is None:
                        child.terminate()
                    child.wait(timeout=10)
                    handle.close()
                web.shutdown()
                web.server_close()
                web_thread.join(timeout=5)


if __name__ == '__main__':
    unittest.main()
