"""Authenticated query leases can be revoked without touching retained content."""
import os
from pathlib import Path
import tempfile
import unittest
import RNS
from directory_store import DirectoryStore, DEDUP_TTL, packed, unpacked


class QueryReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.path=Path(self.temp.name)/'directory.sqlite'
        self.now=1800000000
        self.destination,self.source=os.urandom(16),os.urandom(16)
        self.author=RNS.Identity()
        self.open()
        for index in range(3):
            record=[1,self.author.get_public_key(),os.urandom(16),1,None,0,
                    300000000,1200000000,'Lease '+str(index),'','',2,2,0,self.now,self.now]
            raw=packed(record)
            self.assertEqual(self.request(1,[[raw,self.author.sign(b'trailmate.geocache/sign/v1\0'+raw)]])[4],200)

    def open(self):
        self.store=DirectoryStore(self.path,self.destination,clock=lambda:self.now,
                                  max_snapshots=1,requests_per_minute=10000)

    def tearDown(self):
        self.store.close()
        self.temp.cleanup()

    def request(self,operation,body,source=None,request_id=None):
        result=self.store.handle(source or self.source,packed([1,0,operation,request_id or os.urandom(16),8192,body]))
        self.assertIsNotNone(result)
        return unpacked(result)

    def query(self,cursor=None,source=None,request_id=None):
        return self.request(2,[[-900000000,-1800000000,900000000,1800000000],7,None,1,cursor],source,request_id)

    def test_legacy_capabilities_unchanged_and_extension_explicit(self):
        self.assertEqual(self.request(0,[])[5][2],[0,1,2,3,4])
        self.assertEqual(self.request(0,[2])[5][2],[0,1,2,3,4,6])
        self.assertEqual(self.request(0,[1])[4],400)
        self.assertEqual(self.request(0,[2,2])[4],400)

    def test_repeated_partial_queries_can_release_their_single_capacity_slot(self):
        for _ in range(300):
            first=self.query()
            self.assertEqual(first[4],200)
            self.assertIsNotNone(first[5][2])
            self.assertEqual(self.query()[4],503)
            self.assertEqual(self.request(6,[first[5][0]])[5],[first[5][0],1])
            self.assertEqual(self.store.db.execute('SELECT COUNT(*) FROM snapshots').fetchone()[0],0)
        self.assertEqual(self.store.sequence(),3)
        self.assertEqual(self.store.db.execute('SELECT COUNT(*) FROM objects').fetchone()[0],3)

    def test_foreign_identity_cannot_release_browse_or_replication(self):
        first=self.query()
        self.assertEqual(self.request(6,[first[5][0]],source=os.urandom(16))[4],404)
        self.assertEqual(self.query(first[5][2])[4],200)
        self.assertEqual(self.request(6,[first[5][0]])[4],200)
        sync=self.request(4,[None,1])
        snapshot=self.store.db.execute('SELECT snapshot FROM cursors WHERE token=?',(sync[5][3],)).fetchone()[0]
        self.assertEqual(self.request(6,[snapshot])[4],404) # phase=0, never browsing
        self.assertEqual(self.request(4,[sync[5][3],1])[4],200)

    def test_lost_release_response_retry_and_restart_are_durable(self):
        query_id,release_id=os.urandom(16),os.urandom(16)
        first=self.query(request_id=query_id)
        continuation_id=os.urandom(16)
        second=self.query(first[5][2],request_id=continuation_id)
        release=self.request(6,[first[5][0]],request_id=release_id)
        self.store.close();self.open()
        self.assertEqual(self.request(6,[first[5][0]],request_id=release_id),release)
        self.assertEqual(self.request(6,[first[5][0]])[5],[first[5][0],0])
        self.assertEqual(self.request(6,[os.urandom(16)],request_id=release_id)[4],409)
        for cursor,request_id in ((None,query_id),(first[5][2],continuation_id)):
            result=self.query(cursor,request_id=request_id)
            self.assertEqual(result[4],410)
            self.assertEqual(result[5][0],'query_released')
        self.assertEqual(self.query(second[5][2])[4],410)
        self.assertEqual(self.query()[4],200)

    def test_malformed_release_does_not_mutate_and_expired_tombstones_reclaim(self):
        first=self.query()
        for body in ([],[bytes(15)],[bytes(17)],[first[5][0],0],['bad']):
            self.assertEqual(self.request(6,body)[4],400)
        self.assertEqual(self.query(first[5][2])[4],200)
        self.assertEqual(self.request(6,[first[5][0]])[4],200)
        self.now+=DEDUP_TTL+1
        self.store.maintenance()
        self.assertEqual(self.store.db.execute('SELECT COUNT(*) FROM query_releases').fetchone()[0],0)
        self.assertEqual(self.store.db.execute('SELECT COUNT(*) FROM objects').fetchone()[0],3)


if __name__=='__main__':
    unittest.main()
