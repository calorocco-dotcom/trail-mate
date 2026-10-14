"""Real crypto/stores with a controllable authenticated transport boundary."""
import hashlib
import os
from pathlib import Path
import tempfile
import unittest
import RNS
from tools.tracks.blob_store import BlobStore
from tools.tracks.directory_index import DirectoryIndex
from tools.tracks.application import TrackApplication
from tools.tracks.protocol import APP_TYPE,packed,unpacked,verify_manifest
from tools.tracks.replication import TrackReplicaScheduler
from tools.tracks.attachment_index import encode_index


class ReplicationTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.now=1800000000
        self.remote,self.local,self.author=RNS.Identity(),RNS.Identity(),RNS.Identity()
        self.remote_delivery=RNS.Destination.hash_from_name_and_identity('lxmf.delivery',self.remote)
        self.local_delivery=RNS.Destination.hash_from_name_and_identity('lxmf.delivery',self.local)
        self.stores=[];self.source=self.store('source');self.destination=self.store('destination')
        self.app=TrackApplication(self.remote,self.source.blobs,self.source)
        self.messages=[];self.events=[]
        self.scheduler=self.make_scheduler()
        self.announce()

    def store(self,name):
        blobs=BlobStore(Path(self.temp.name)/name,clock=lambda:self.now)
        index=DirectoryIndex(Path(self.temp.name)/name,blobs)
        self.stores.append((index,blobs));return index

    def make_scheduler(self):
        return TrackReplicaScheduler(self.destination,self.local_delivery,clock=lambda:self.now,
                                     report=lambda event,**fields:self.events.append((event,fields)))

    def tearDown(self):
        for index,blobs in self.stores:index.close();blobs.close()
        self.temp.cleanup()

    def announce(self,epoch=None):
        self.scheduler.received_announce(RNS.Destination.hash_from_name_and_identity('trailmate.track.directory',self.remote),self.remote,
            packed([1,APP_TYPE,self.remote_delivery,epoch or self.app.epoch,'Source']))

    def put(self,index,data):
        digest=hashlib.sha256(data).digest();owner=self.author.hash
        upload=index.blobs.begin(owner,digest,len(data))
        if upload['token']:
            for offset in range(upload['offset'],len(data),1024):index.blobs.append(owner,upload['token'],offset,data[offset:offset+1024])
            index.blobs.finish(owner,upload['token'])
        return digest

    def signed(self,previous=None,*,archive=False,package=False,title='Track'):
        data=b'<kml xmlns="http://www.opengis.net/kml/2.2"><Placemark><Point><coordinates>0,0</coordinates></Point></Placemark></kml>'+b'\n'*4096
        digest=self.put(self.source,data)
        record=[2 if package else 1,self.author.get_public_key(),b'n'*16,0,1,None,0,100,100,title,'',[0,0,0,0],[0,None,None,1],[digest,len(data),'application/vnd.google-earth.kml+xml',1]]
        if package:
            image=b'opaque signed resource'
            image_hash=self.put(self.source,image)
            raw_index=encode_index([1,digest,len(data),[['img',image_hash,len(image),'image/png','image.png',0,1]]])
            index_hash=self.put(self.source,raw_index)
            record.extend([[0,[],0,0,0,0,0,None],[index_hash,len(raw_index),1,1]])
        if previous:
            record=unpacked(previous[0]);record[4]+=1;record[5]=verify_manifest(previous)['revision_hash'];record[8]+=1;record[9]=title
        if archive:record[6]=1
        raw=packed(record)
        return[raw,self.author.sign(f'TrailMate.Track.Manifest.v{record[0]}\0'.encode()+raw)]

    def publish(self,signed):self.source.publish(self.author.hash,signed,unpacked(signed[0])[5])

    def step(self,deliver=True):
        self.scheduler.step(lambda public,raw:self.messages.append((public,raw)))
        if deliver and self.messages:
            public,request=self.messages.pop(0);self.assertEqual(public,self.remote.get_public_key())
            self.scheduler.received(self.remote_delivery,self.app.handle(self.local.hash,request))

    def state(self):
        row=self.destination.db.execute('SELECT * FROM track_replica_peers WHERE delivery=?',(self.remote_delivery,)).fetchone()
        return None if row is None else self.scheduler.decode(row)

    def run_to_checkpoint(self,sequence,steps=300):
        for _ in range(steps):
            self.step()
            state=self.state()
            if state and state['after']==sequence and state['page']is None:return state
        self.fail(str(self.state()))

    def test_three_versions_file_and_archive_checkpoint_after_complete_import(self):
        first=self.signed();second=self.signed(first,title='v2');last=self.signed(second,archive=True)
        for signed in (first,second,last):self.publish(signed)
        self.run_to_checkpoint(3)
        object_id=verify_manifest(first)['object_id']
        self.assertEqual(self.destination.get_manifest(object_id)['signed'],last)
        self.assertTrue(self.destination.blobs.has_file(unpacked(first[0])[13][0],unpacked(first[0])[13][1]))
        self.assertEqual(self.destination.query([-1,-1,1,1])['items'],[])
        self.assertEqual(len(self.destination.query([-1,-1,1,1],state=1)['items']),1)

    def test_required_index_resource_and_raw_bytes_copied_before_checkpoint(self):
        signed=self.signed(package=True);self.publish(signed);self.run_to_checkpoint(1)
        status=self.destination.blobs.package_status(signed)
        self.assertEqual(status[2:5],[1,1,1]);self.assertEqual(status[5][0][-1],1)
        original=unpacked(signed[0]);assets=original[15]
        self.assertEqual(self.destination.blobs.read_index(*assets[:2]),self.source.blobs.read_index(*assets[:2]))

    def test_crash_after_chunk_commit_retries_exact_request_without_advancing_checkpoint(self):
        self.publish(self.signed())
        for _ in range(20):
            self.step(deliver=False)
            if not self.messages:continue
            public,request=self.messages.pop(0);decoded=unpacked(request)
            if decoded[2]==8:
                response=self.app.handle(self.local.hash,request);body=unpacked(response)[5]
                digest,size,offset,data=body
                upload=self.destination.blobs.begin(self.remote_delivery,digest,size)
                self.destination.blobs.append(self.remote_delivery,upload['token'],offset,data)
                self.assertEqual(self.state()['after'],0)
                self.scheduler=self.make_scheduler();self.step(deliver=False)
                retried=self.messages.pop()[1];self.assertEqual(retried,request)
                self.scheduler.received(self.remote_delivery,response)
                self.run_to_checkpoint(1);return
            self.scheduler.received(self.remote_delivery,self.app.handle(self.local.hash,request))
        self.fail('No file request')

    def test_foreign_late_response_and_wrong_binding_do_not_commit(self):
        self.publish(self.signed());self.step(deliver=False)
        public,request=self.messages.pop();response=self.app.handle(self.local.hash,request)
        self.scheduler.received(os.urandom(16),response);self.assertIsNotNone(self.state()['pending'])
        wrong=unpacked(response);wrong[3]=os.urandom(16);self.scheduler.received(self.remote_delivery,packed(wrong));self.assertIsNotNone(self.state()['pending'])
        wrong=unpacked(response);wrong[5][2]=bytes(64);self.scheduler.received(self.remote_delivery,packed(wrong))
        self.assertEqual(self.state()['health'],'Quarantined');self.assertEqual(self.state()['after'],0)

    def test_bandwidth_guard_holds_file_without_blocking_identity_or_inventory(self):
        self.publish(self.signed());self.scheduler.can_transfer=lambda destination:False
        for _ in range(12):self.step()
        self.assertEqual(self.state()['health'],'WaitingForBandwidth');self.assertEqual(self.state()['after'],0)
        self.assertEqual(self.destination.db.execute('SELECT count(*) FROM versions').fetchone()[0],0)
        self.now+=61;self.scheduler.can_transfer=lambda destination:True;self.run_to_checkpoint(1)

    def test_timeout_retains_id_backoff_and_restart_recover(self):
        self.publish(self.signed());self.step(deliver=False);request=self.messages.pop()[1]
        self.now+=121;self.step(deliver=False);self.assertEqual(self.state()['health'],'Backoff')
        self.assertEqual(self.state()['pending'],request);self.now+=61
        self.scheduler=self.make_scheduler();self.step(deliver=False);self.assertEqual(self.messages.pop()[1],request)
        self.scheduler.received(self.remote_delivery,self.app.handle(self.local.hash,request));self.run_to_checkpoint(1)

    def test_epoch_change_resets_checkpoint_and_only_own_temporary_upload(self):
        signed=self.signed();self.publish(signed);self.run_to_checkpoint(1)
        digest=hashlib.sha256(b'temporary').digest()
        owned=self.destination.blobs.begin(self.remote_delivery,digest,9)
        foreign=self.destination.blobs.begin(b'x'*16,digest,9)
        new_epoch=os.urandom(16);self.announce(new_epoch);self.step(deliver=False)
        self.assertEqual(self.state()['after'],0)
        self.assertIsNone(self.destination.blobs.db.execute('SELECT 1 FROM uploads WHERE token=?',(owned['token'],)).fetchone())
        self.assertIsNotNone(self.destination.blobs.db.execute('SELECT 1 FROM uploads WHERE token=?',(foreign['token'],)).fetchone())

    def test_signed_fork_evidence_propagates_and_neither_branch_is_current(self):
        first=self.signed();left=self.signed(first,title='Left');right=self.signed(first,title='Right')
        for signed in (first,left,right):self.source.import_replica(self.local_delivery,signed)
        self.run_to_checkpoint(3)
        object_id=verify_manifest(first)['object_id']
        self.assertEqual(self.destination.conflict_proof(object_id),self.source.conflict_proof(object_id))
        with self.assertRaisesRegex(ValueError,'version conflict'):self.destination.get_manifest(object_id)

    def test_unknown_path_is_retried_without_misclassifying_as_slow(self):
        self.publish(self.signed());self.scheduler.can_transfer=lambda destination:None
        for _ in range(12):self.step()
        self.assertEqual(self.state()['health'],'WaitingForPath');self.assertEqual(self.state()['after'],0)
        self.now+=6;self.scheduler.can_transfer=lambda destination:True;self.run_to_checkpoint(1)

    def test_new_replica_progress_extends_idle_expiry_not_plain_retries(self):
        self.publish(self.signed())
        for _ in range(120):
            self.now+=600;self.step()
            state=self.state()
            if state and state['after']==1:return
        self.fail(str(self.state()))

    def test_existing_version_missing_physical_file_refills_before_checkpoint(self):
        signed=self.signed();self.publish(signed);self.run_to_checkpoint(1)
        digest=unpacked(signed[0])[13][0]
        self.destination.blobs._final(digest).unlink()
        row=self.destination.db.execute('SELECT * FROM track_replica_peers').fetchone();state=self.scheduler.decode(row)
        state.update(after=0,high=None,page=None,item=0);self.scheduler.save(row,state)
        self.run_to_checkpoint(1)
        self.assertTrue(self.destination.blobs.has_file(digest,unpacked(signed[0])[13][1]))


if __name__=='__main__':unittest.main()
