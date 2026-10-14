"""Two production RNS directory processes, interrupted file, offline source, browser."""
import hashlib
import base64
from contextlib import closing
import json
import os
from pathlib import Path
import socket
import sqlite3
import subprocess
import sys
import sysconfig
import tempfile
import time
import unittest
import RNS
from tools.tracks.blob_store import BlobStore
from tools.tracks.directory_index import DirectoryIndex
from tools.tracks.directory_service import load_identity
from tools.tracks.protocol import packed,unpacked,verify_manifest
from tools.tracks.attachment_index import encode_index

ROOT=Path(__file__).resolve().parents[2]


def free_port():
    with socket.socket() as listener:listener.bind(('127.0.0.1',0));return listener.getsockname()[1]


class ReplicationNetworkTests(unittest.TestCase):
    def test_native_restart_copy_and_fresh_browser_after_source_offline(self):
        retained=ROOT/'.codex-build/hostlink-codec/track-replication-network'
        retained.mkdir(parents=True,exist_ok=True)
        (retained/'NOTES.txt').write_text('Two local native track services, interrupted copy restart, source stopped and new browser reading archived replica. Current production sources. Isolated synthetic author only; no physical device, RF or public publication. Retain compact result/logs until acceptance.')
        children=[];flags=getattr(subprocess,'CREATE_NO_WINDOW',0)
        # Windows venv python.exe is a redirector: terminating that PID can
        # leave the real interpreter serving sockets. Own the actual process.
        service_python=sys._base_executable if os.name=='nt' else sys.executable
        service_environment={**os.environ,'TRAIL_MATE_REPLICATION_DEBUG':'1'}
        if os.name=='nt':service_environment['PYTHONPATH']=sysconfig.get_path('purelib')
        with tempfile.TemporaryDirectory() as work:
            root=Path(work);source_state=root/'source';source_state.mkdir()
            source_identity=load_identity(source_state)
            data=b'<kml xmlns="http://www.opengis.net/kml/2.2"><Placemark><Point><coordinates>0,0</coordinates></Point></Placemark></kml>'+b'\n'*16384
            digest=hashlib.sha256(data).digest();author=RNS.Identity();blobs=BlobStore(source_state/'track-data');index=DirectoryIndex(source_state/'track-data',blobs)
            upload=blobs.begin(author.hash,digest,len(data))
            for offset in range(0,len(data),1024):blobs.append(author.hash,upload['token'],offset,data[offset:offset+1024])
            blobs.finish(author.hash,upload['token']);versions=[]
            record=[1,author.get_public_key(),os.urandom(16),0,1,None,0,100,100,'Native replica track','',[0,0,0,0],[0,None,None,1],[digest,len(data),'application/vnd.google-earth.kml+xml',1]]
            for revision in range(1,4):
                record[4]=revision;record[5]=verify_manifest(versions[-1])['revision_hash'] if versions else None;record[8]=99+revision;record[6]=int(revision==3)
                raw=packed(record);signed=[raw,author.sign(b'TrailMate.Track.Manifest.v1\0'+raw)];index.publish(author.hash,signed,record[5]);versions.append(signed)
            # A separate v2 object exercises the signed index and real PNG in
            # addition to the v1 history above; it shares the immutable KML.
            png=base64.b64decode('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+a7GoAAAAASUVORK5CYII=')
            def put_extra(content):
                file_hash=hashlib.sha256(content).digest();upload=blobs.begin(author.hash,file_hash,len(content))
                if upload['token']:
                    for offset in range(0,len(content),1024):blobs.append(author.hash,upload['token'],offset,content[offset:offset+1024])
                    blobs.finish(author.hash,upload['token'])
                return file_hash
            png_hash=put_extra(png)
            raw_index=encode_index([1,digest,len(data),[['icon',png_hash,len(png),'image/png','icon.png',0,1]]])
            index_hash=put_extra(raw_index)
            record=[2,author.get_public_key(),os.urandom(16),0,1,None,0,100,100,'Native replica package','',[0,0,0,0],[0,None,None,1],[digest,len(data),'application/vnd.google-earth.kml+xml',1],[0,[],0,0,0,0,0,None],[index_hash,len(raw_index),1,1]]
            package_versions=[]
            for revision in range(1,4):
                record[4]=revision;record[5]=verify_manifest(package_versions[-1])['revision_hash'] if package_versions else None;record[8]=99+revision;record[6]=int(revision==3)
                raw=packed(record);signed=[raw,author.sign(b'TrailMate.Track.Manifest.v2\0'+raw)];index.publish(author.hash,signed,record[5]);package_versions.append(signed)
            fork_record=[2,author.get_public_key(),os.urandom(16),0,1,None,0,100,100,'Fork root','',[0,0,0,0],[0,None,None,1],[digest,len(data),'application/vnd.google-earth.kml+xml',1],[0,[],0,0,0,0,0,None],[index_hash,len(raw_index),1,1]]
            raw=packed(fork_record);fork_root=[raw,author.sign(b'TrailMate.Track.Manifest.v2\0'+raw)];index.import_replica(author.hash,fork_root)
            fork_id=verify_manifest(fork_root)['object_id']
            for title in ('Fork left','Fork right'):
                fork_record[4]=2;fork_record[5]=verify_manifest(fork_root)['revision_hash'];fork_record[8]=101;fork_record[9]=title
                raw=packed(fork_record);index.import_replica(author.hash,[raw,author.sign(b'TrailMate.Track.Manifest.v2\0'+raw)])
            index.close();blobs.close();object_id=verify_manifest(package_versions[0])['object_id'];latest=verify_manifest(package_versions[-1])['revision_hash']
            source_port,replica_port=free_port(),free_port();source_config=root/'source-rns';source_config.mkdir();replica_config=root/'replica-rns';replica_config.mkdir()
            base='[reticulum]\nshare_instance = No\nenable_transport = Yes\n[interfaces]\n'
            (source_config/'config').write_text(base+f'[[Source]]\nenabled = Yes\ntype = TCPServerInterface\nlisten_ip = 127.0.0.1\nlisten_port = {source_port}\n')
            (replica_config/'config').write_text(base+f'[[Source uplink]]\nenabled = Yes\ntype = TCPClientInterface\ntarget_host = 127.0.0.1\ntarget_port = {source_port}\n[[Readers]]\nenabled = Yes\ntype = TCPServerInterface\nlisten_ip = 127.0.0.1\nlisten_port = {replica_port}\n')

            def start(label,args):
                log=retained/(label+'.log');handle=log.open('w',encoding='utf-8');child=subprocess.Popen([service_python,*args],cwd=ROOT,stdout=handle,stderr=subprocess.STDOUT,creationflags=flags,env=service_environment);children.append((child,handle))
                until=time.monotonic()+25
                while child.poll()is None and time.monotonic()<until:
                    for line in log.read_text(encoding='utf-8').splitlines():
                        try:event=json.loads(line)
                        except ValueError:continue
                        if event.get('event')in('ready','bridge_ready'):return child,event
                    time.sleep(.1)
                self.fail(label+' not ready: '+log.read_text(encoding='utf-8'))

            def read_sql(path,statement):
                if not path.is_file():return None
                try:
                    with closing(sqlite3.connect('file:'+path.as_posix()+'?mode=ro',uri=True,timeout=1)) as db:return db.execute(statement).fetchone()
                except sqlite3.Error:return None

            try:
                source,source_ready=start('source',['-m','tools.tracks.directory_service','--state',str(source_state),'--rns-config',str(source_config),'--run-seconds','300'])
                replica_state=root/'replica';args=['-m','tools.tracks.directory_service','--state',str(replica_state),'--rns-config',str(replica_config),'--replica-seed',source_ready['discovery'],'--run-seconds','300']
                replica,replica_ready=start('replica-first',args)
                uploads=replica_state/'track-data'/'blobs.sqlite'
                until=time.monotonic()+70;prefix=None
                while replica.poll()is None and time.monotonic()<until:
                    prefix=read_sql(uploads,'SELECT offset FROM uploads LIMIT 1')
                    if prefix and 0<prefix[0]<len(data):break
                    time.sleep(.05)
                self.assertTrue(prefix and 0<prefix[0]<len(data),'No partial copy: '+(retained/'replica-first.log').read_text())
                replica.terminate();replica.wait(timeout=10)
                replica,restarted=start('replica-restarted',args)
                for field in ('delivery','discovery','epoch'):self.assertEqual(restarted[field],replica_ready[field])
                database=replica_state/'track-data'/'tracks.sqlite';until=time.monotonic()+140;head=None
                while replica.poll()is None and time.monotonic()<until:
                    head=read_sql(database,'SELECT count(*) FROM versions')
                    if head and head[0]==9:break
                    time.sleep(.1)
                peer_diagnostic=read_sql(database,'SELECT state,due,scheduled FROM track_replica_peers LIMIT 1')
                if peer_diagnostic:
                    decoded=__import__('msgpack').unpackb(peer_diagnostic[0],raw=False)
                    pending=unpacked(decoded['pending']) if decoded['pending'] else None
                    diagnostic={key:decoded[key] for key in ('after','high','item','deadline','caps_until','file','health','failures','diagnostic')}
                    diagnostic.update(due=peer_diagnostic[1],scheduled=peer_diagnostic[2],wallNow=int(time.time()),pendingOperation=pending[2] if pending else None,pendingRequest=pending[3].hex() if pending else None)
                    (retained/'peer-diagnostic.json').write_text(json.dumps(diagnostic,indent=2))
                self.assertEqual(head,(9,),(retained/'replica-restarted.log').read_text())
                peer=read_sql(database,'SELECT state FROM track_replica_peers LIMIT 1')
                self.assertIsNotNone(peer)
                # Import may finish one service tick before page checkpoint.
                until=time.monotonic()+5
                while time.monotonic()<until:
                    peer=read_sql(database,'SELECT state FROM track_replica_peers LIMIT 1')
                    state=__import__('msgpack').unpackb(peer[0],raw=False)
                    if state['after']==9:break
                    time.sleep(.1)
                self.assertEqual(state['after'],9)
                multi_port=free_port()
                multi_bridge,multi_connection=start('multi-bridge',['tools/geocaching/packet_bridge.py','--tcp-port',str(replica_port),'--port','0','--origin',f'http://127.0.0.1:{multi_port}','--run-seconds','90'])
                multi=subprocess.run(['node','tests/site/track-multidirectory-network.browser.mjs',f'ws://127.0.0.1:{multi_connection["port"]}/',replica_ready['discovery'],source_ready['discovery'],object_id.hex(),replica_ready['delivery'],source_ready['delivery']],cwd=ROOT,env={**os.environ,'TRAIL_MATE_MULTI_PAGE_PORT':str(multi_port)},capture_output=True,text=True,encoding='utf-8',timeout=75,creationflags=flags)
                (retained/'multidirectory-browser.log').write_text(multi.stdout+multi.stderr,encoding='utf-8')
                self.assertEqual(multi.returncode,0,multi.stdout+multi.stderr)
                multi_bridge.terminate();multi_bridge.wait(timeout=10)
                source.terminate();source.wait(timeout=10)
                # Source TCP is gone before this fresh offline-reader browser connects.
                with socket.socket() as socket_test:self.assertNotEqual(socket_test.connect_ex(('127.0.0.1',source_port)),0)
                reader_port=free_port()
                bridge,connection=start('bridge',['tools/geocaching/packet_bridge.py','--tcp-port',str(replica_port),'--port','0','--origin',f'http://127.0.0.1:{reader_port}','--run-seconds','90'])
                reader_env={**os.environ,'TRAIL_MATE_REPLICA_PAGE_PORT':str(reader_port)}
                browser=subprocess.run(['node','tests/site/track-replica-reader.browser.mjs',f'ws://127.0.0.1:{connection["port"]}/',replica_ready['discovery'],object_id.hex(),latest.hex(),digest.hex(),str(len(data)),fork_id.hex()],cwd=ROOT,env=reader_env,capture_output=True,text=True,encoding='utf-8',timeout=75,creationflags=flags)
                (retained/'browser.log').write_text(browser.stdout+browser.stderr,encoding='utf-8')
                self.assertEqual(browser.returncode,0,browser.stdout+browser.stderr)
                (retained/'result.json').write_text(json.dumps({'passed':True,'nativeReplication':True,'interruptedPrefix':prefix[0],'sourceOffline':True,'checkpoint':9,'versions':9,'manifestVersions':[1,2],'requiredPackage':True,'authorForkCopied':True,'forkObjectId':fork_id.hex(),'pngHash':png_hash.hex(),'indexHash':index_hash.hex(),'fileHash':digest.hex(),'fileBytes':len(data),'author':author.get_public_key().hex(),'physicalUsb':False,'publicNetwork':False},indent=2))
            finally:
                for child,handle in reversed(children):
                    if child.poll()is None:child.terminate()
                    child.wait(timeout=10);handle.close()


if __name__=='__main__':unittest.main()
