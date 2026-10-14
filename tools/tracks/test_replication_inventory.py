"""Real signatures/SQLite/file stores: inventory, replica chain and fork safety."""
import hashlib
import os
from pathlib import Path
import tempfile
import unittest
import RNS
from tools.tracks.blob_store import BlobStore
from tools.tracks.directory_index import DirectoryIndex
from tools.tracks.application import TrackApplication
from tools.tracks.protocol import packed,unpacked,verify_manifest


class ReplicationInventoryTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.author=RNS.Identity()
        self.source=os.urandom(16)
        self.data=b'<kml xmlns="http://www.opengis.net/kml/2.2"><Placemark><Point><coordinates>0,0</coordinates></Point></Placemark></kml>'
        self.digest=hashlib.sha256(self.data).digest()
        self.stores=[]
        self.a,self.b=self.open('a'),self.open('b')

    def open(self,name,copy_file=True,max_versions=10000):
        state=Path(self.temp.name)/name
        blobs=BlobStore(state)
        if copy_file:
            upload=blobs.begin(self.source,self.digest,len(self.data))
            if upload['token']:
                for offset in range(upload['offset'],len(self.data),1024):
                    blobs.append(self.source,upload['token'],offset,self.data[offset:offset+1024])
                blobs.finish(self.source,upload['token'])
        index=DirectoryIndex(state,blobs,max_versions=max_versions)
        self.stores.append((index,blobs))
        return index

    def tearDown(self):
        for index,blobs in self.stores:
            index.close();blobs.close()
        self.temp.cleanup()

    def signed(self,previous=None,*,title='Track',archive=False,nonce=None):
        record=[1,self.author.get_public_key(),nonce or b'n'*16,0,1,None,0,100,100,title,'',[0,0,0,0],[0,None,None,1],[self.digest,len(self.data),'application/vnd.google-earth.kml+xml',1]]
        if previous:
            record=unpacked(previous[0]);record[4]+=1;record[5]=verify_manifest(previous)['revision_hash'];record[8]+=1;record[9]=title
        if archive:record[6]=1
        raw=packed(record)
        return [raw,self.author.sign(b'TrailMate.Track.Manifest.v1\0'+raw)]

    def publish(self,index,signed):
        return index.publish(self.source,signed,unpacked(signed[0])[5])

    def test_incremental_watermark_freezes_and_duplicates_do_not_advance(self):
        first=self.signed();second=self.signed(first,title='v2')
        self.publish(self.a,first);self.publish(self.a,second)
        page=self.a.replication_page(limit=1)
        self.assertEqual(page[:3],[2,1,1])
        third=self.signed(second,archive=True);self.publish(self.a,third)
        tail=self.a.replication_page(page[1],page[0],1)
        self.assertEqual(tail[:3],[2,2,0]);self.assertEqual(tail[3][0][4],0)
        delta=self.a.replication_page(tail[1],None,1)
        self.assertEqual(delta[:3],[3,3,0]);self.assertEqual(delta[3][0][4],1)
        self.publish(self.a,third);self.assertEqual(self.a.replication_page()[0],3)
        self.assertEqual(self.a.replication_page(3)[3],[])

    def test_two_stores_preserve_chain_archive_and_exact_file(self):
        versions=[self.signed()]
        versions.append(self.signed(versions[-1],title='v2'))
        versions.append(self.signed(versions[-1],archive=True))
        for signed in versions:self.publish(self.a,signed)
        inventory=self.a.replication_page()[3]
        for row in inventory:
            signed=self.a.get_manifest(row[1],row[2])['signed']
            self.assertEqual(self.b.import_replica(self.source,signed),{'duplicate':False,'conflict':False})
        object_id=verify_manifest(versions[0])['object_id']
        self.assertEqual(self.b.get_manifest(object_id)['signed'],versions[-1])
        self.assertEqual(self.b.blobs.read_chunk(self.digest,0,1024)[3],self.data)
        self.assertEqual(self.b.query([-1,-1,1,1])['items'],[])
        self.assertEqual(len(self.b.query([-1,-1,1,1],state=1)['items']),1)
        self.assertEqual(self.b.import_replica(self.source,versions[0])['duplicate'],True)
        self.assertEqual(self.b.get_manifest(object_id)['signed'],versions[-1])
        self.assertEqual(self.b.replication_page()[0],3)

    def test_missing_parent_or_content_cannot_commit_or_advance_inventory(self):
        first=self.signed();second=self.signed(first)
        with self.assertRaisesRegex(ValueError,'missing replicated parent'):self.b.import_replica(self.source,second)
        self.assertEqual(self.b.replication_page()[0],0)
        missing=self.open('missing',copy_file=False)
        with self.assertRaisesRegex(ValueError,'KML not stored'):missing.import_replica(self.source,first)
        self.assertEqual(missing.replication_page()[0],0)
        bad=[first[0],bytes(64)]
        with self.assertRaisesRegex(ValueError,'author signature'):self.b.import_replica(self.source,bad)
        self.assertEqual(self.b.replication_page()[0],0)

    def test_signed_forks_preserved_without_a_current_winner(self):
        first=self.signed();left=self.signed(first,title='Left');right=self.signed(first,title='Right')
        for index,order in ((self.a,[first,left,right]),(self.b,[first,right,left])):
            for signed in order:index.import_replica(self.source,signed)
            object_id=verify_manifest(first)['object_id']
            self.assertEqual(index.query([-1,-1,1,1])['items'],[])
            with self.assertRaisesRegex(ValueError,'author version conflict'):index.get_manifest(object_id)
            proof=index.conflict_proof(object_id)
            self.assertEqual(proof[1:],sorted([verify_manifest(left)['revision_hash'],verify_manifest(right)['revision_hash']]))
            for signed in (left,right):
                historical=index.get_manifest(object_id,verify_manifest(signed)['revision_hash'])
                self.assertEqual(historical['signed'],signed);self.assertFalse(historical['current'])
            with self.assertRaisesRegex(ValueError,'author version conflict'):self.publish(index,self.signed(left))
            self.assertEqual(index.replication_page()[0],3)
        self.assertEqual(self.a.conflict_proof(object_id),self.b.conflict_proof(object_id))

    def test_restart_and_legacy_backfill_keep_sequence_without_gaps(self):
        first=self.signed();second=self.signed(first)
        self.publish(self.a,first);self.publish(self.a,second)
        original=self.a.replication_page()
        # Simulate an old database with versions but no replication schema.
        with self.a.db:self.a.db.execute('DROP TABLE replication_log')
        self.stores.remove((self.a,self.a.blobs));self.a.close();self.a.blobs.close()
        self.a=self.open('a');self.assertEqual(self.a.replication_page(),original)
        self.stores.remove((self.a,self.a.blobs));self.a.close();self.a.blobs.close()
        self.a=self.open('a');self.assertEqual(self.a.replication_page(),original)
        third=self.signed(second);self.publish(self.a,third)
        self.assertEqual(self.a.replication_page()[3][-1][0],3)

    def test_capacity_and_corruption_fail_closed(self):
        limited=self.open('limited',max_versions=1)
        first=self.signed();limited.import_replica(self.source,first)
        with self.assertRaisesRegex(ValueError,'version capacity'):limited.import_replica(self.source,self.signed(first))
        self.assertEqual(limited.replication_page()[0],1)
        with limited.db:limited.db.execute('UPDATE replication_log SET object=?',(bytes(32),))
        with self.assertRaisesRegex(ValueError,'replication log corrupted'):limited.replication_page()

    def test_wire_inventory_and_proofs_are_opt_in_and_budgeted(self):
        directory=RNS.Identity();app=TrackApplication(directory,self.a.blobs,self.a)
        self.publish(self.a,self.signed())
        def request(op,body,budget=8192):return unpacked(app.handle(self.source,packed([1,0,op,b'r'*16,budget,body])))
        self.assertEqual(request(0,[])[5][7],list(range(9)))
        self.assertTrue({17,18}.issubset(request(0,[2])[5][7]))
        page=request(17,[0,None,1]);self.assertEqual(page[4],200);self.assertEqual(page[5][:3],[1,1,0])
        for body in ([True,None,1],[-1,None,1],[0,None,33],[1,0,1],[0,2,1]):
            if body[0] is True:continue # CMP1 rejects bool before dispatch.
            self.assertEqual(request(17,body)[4],422)
        self.assertIsNone(request(18,[verify_manifest(self.signed())['object_id']])[5])

    def test_sparse_windows_finish_at_high_and_u64_sequences_remain_exact(self):
        first=self.signed();self.publish(self.a,first)
        second=self.signed(first);self.publish(self.a,second)
        with self.a.db:self.a.db.execute('UPDATE replication_log SET sequence=? WHERE sequence=2',(4294967296,))
        self.assertEqual(self.a.replication_page(0,2,16)[:3],[2,2,0])
        page=self.a.replication_page(2,None,1)
        self.assertEqual(page[:3],[4294967296,4294967296,0])
        self.assertEqual(page[3][0][0],4294967296)


if __name__=='__main__':unittest.main()
