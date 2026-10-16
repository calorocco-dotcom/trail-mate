"""Real RNS/LXMF/bridge/browser against isolated readonly SQLite, then recovery."""
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import sysconfig
import tempfile
import time
import unittest

ROOT=Path(__file__).resolve().parents[2]


def free_port():
    with socket.socket() as listener:
        listener.bind(('127.0.0.1',0))
        return listener.getsockname()[1]


def serve(state,config,marker):
    from tools.tracks.directory_service import TrackDirectoryService
    from tools.tracks.protocol import unpacked
    Path(state).mkdir(parents=True,exist_ok=True)
    service=TrackDirectoryService(Path(state),Path(config))
    service.blobs.db.execute('PRAGMA query_only=ON')
    original=service.application.handle
    def handle(source,raw):
        # The fault/recovery seam lives only in this test worker. Network
        # decoding, crypto, transport and production dispatch are not replaced.
        if Path(marker).exists():service.blobs.db.execute('PRAGMA query_only=OFF')
        response=original(source,raw)
        request=unpacked(raw)
        if request[2]==4:
            print(json.dumps({'event':'storage_test_request','operation':4,
                              'request':request[3].hex(),'status':unpacked(response)[4]}),flush=True)
        return response
    service.application.handle=handle
    service.run(80)


class StorageNetworkTests(unittest.TestCase):
    def test_authenticated_browser_readonly_failures_and_original_upload_recovery(self):
        retained=ROOT/'.codex-build/hostlink-codec/track-storage-network'
        retained.mkdir(parents=True,exist_ok=True)
        (retained/'NOTES.txt').write_text('Actual native local directory/LXMF/packet bridge/Edge against test-only SQLite readonly injection, marker restores writes. Original request recovery; no device or public publication. Current sources; compact evidence retained until acceptance.')
        flags=getattr(subprocess,'CREATE_NO_WINDOW',0)
        executable=sys._base_executable if os.name=='nt' else sys.executable
        environment=dict(os.environ)
        if os.name=='nt':environment['PYTHONPATH']=sysconfig.get_path('purelib')
        children=[]
        with tempfile.TemporaryDirectory() as work:
            root=Path(work)
            config=root/'rns';config.mkdir()
            port,page_port=free_port(),free_port()
            (config/'config').write_text('[reticulum]\nshare_instance = No\nenable_transport = Yes\n[interfaces]\n[[Readers]]\nenabled = Yes\ntype = TCPServerInterface\nlisten_ip = 127.0.0.1\nlisten_port = '+str(port)+'\n')
            def start(label,args,ready_event):
                log=retained/(label+'.log');handle=log.open('w',encoding='utf-8')
                child=subprocess.Popen([executable,*args],cwd=ROOT,stdout=handle,stderr=subprocess.STDOUT,creationflags=flags,env=environment)
                children.append((child,handle))
                until=time.monotonic()+15
                while child.poll()is None and time.monotonic()<until:
                    for line in log.read_text(encoding='utf-8').splitlines():
                        try:event=json.loads(line)
                        except ValueError:continue
                        if event.get('event')==ready_event:return child,event
                    time.sleep(.1)
                self.fail(label+' not ready: '+log.read_text(encoding='utf-8'))
            try:
                service,ready=start('service',['-m','tools.tracks.test_storage_network','serve',str(root/'service'),str(config),str(root/'recover')],'ready')
                _,bridge=start('bridge',['tools/geocaching/packet_bridge.py','--tcp-port',str(port),'--port','0','--origin',f'http://127.0.0.1:{page_port}','--run-seconds','80'],'bridge_ready')
                browser=subprocess.run(['node','tests/site/track-storage-network.browser.mjs',f'ws://127.0.0.1:{bridge["port"]}/',ready['discovery'],str(root/'recover')],cwd=ROOT,env={**os.environ,'TRAIL_MATE_STORAGE_PAGE_PORT':str(page_port)},capture_output=True,text=True,timeout=60,creationflags=flags)
                (retained/'browser.log').write_text(browser.stdout+browser.stderr)
                self.assertEqual(browser.returncode,0,browser.stdout+browser.stderr)
                self.assertIsNone(service.poll(),'Directory exited during recoverable storage error')
                trace=[]
                for line in (retained/'service.log').read_text().splitlines():
                    try:event=json.loads(line)
                    except ValueError:continue
                    if event.get('event')=='storage_test_request':trace.append(event)
                self.assertGreaterEqual(len(trace),5)
                self.assertEqual({row['request'] for row in trace},{'11'*16})
                self.assertEqual([row['status'] for row in trace[:3]],[503,503,503])
                self.assertEqual([row['status'] for row in trace[-2:]],[200,200])
                (retained/'result.json').write_text(json.dumps({'passed':True,'actualNativeStorageFault':True,'fixedRequestId':True,'directoryStayedRunning':True,'failedRequests':3,'recoveredRequests':2,'physicalUsb':False,'publicNetwork':False},indent=2))
            finally:
                for child,handle in reversed(children):
                    if child.poll()is None:child.terminate()
                    child.wait(timeout=10)
                    handle.close()


if __name__=='__main__':
    if len(sys.argv)>1 and sys.argv[1]=='serve':serve(*sys.argv[2:])
    else:unittest.main()
