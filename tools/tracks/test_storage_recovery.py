"""Real SQLite/immutable files under readonly, lock, full and checkpoint faults.

Replication transport and the monotonic clock are controlled. This is not a
physical power-loss or native network test; those remain separate acceptance.
"""
from contextlib import redirect_stdout
import errno
import hashlib
import io
import json
from pathlib import Path
import sqlite3
import tempfile
import unittest
from unittest.mock import patch
import RNS
from tools.tracks.application import TrackApplication
from tools.tracks.blob_store import BlobStore
from tools.tracks.directory_index import DirectoryIndex
from tools.tracks.directory_service import TrackDirectoryService
from tools.tracks.protocol import packed,unpacked,verify_manifest
from tools.tracks import test_replication as replica_fixture


class StorageRecoveryTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.author,self.directory=RNS.Identity(),RNS.Identity()
        self.blobs=BlobStore(self.temp.name)
        self.index=DirectoryIndex(self.temp.name,self.blobs)
        self.addCleanup(self.blobs.close)
        self.addCleanup(self.index.close)
        self.app=TrackApplication(self.directory,self.blobs,self.index)
        self.data=b'<kml xmlns="http://www.opengis.net/kml/2.2"><Placemark><Point><coordinates>0,0</coordinates></Point></Placemark></kml>'
        self.digest=hashlib.sha256(self.data).digest()

    def request(self,operation,body,request_id=b'r'*16):
        reply=unpacked(self.app.handle(self.author.hash,packed([1,0,operation,request_id,512,body])))
        self.assertEqual(reply[:4],[1,1,operation,request_id])
        return reply

    def signed(self):
        upload=self.blobs.begin(self.author.hash,self.digest,len(self.data))
        if upload['token']:
            self.blobs.append(self.author.hash,upload['token'],0,self.data)
            self.blobs.finish(self.author.hash,upload['token'])
        record=[1,self.author.get_public_key(),b'n'*16,0,1,None,0,100,100,'Stored','',[0,0,0,0],[0,None,None,1],[self.digest,len(self.data),'application/vnd.google-earth.kml+xml',1]]
        raw=packed(record)
        return [raw,self.author.sign(b'TrailMate.Track.Manifest.v1\0'+raw)]

    def fixture(self):
        fixture=replica_fixture.ReplicationTests('test_three_versions_file_and_archive_checkpoint_after_complete_import')
        fixture.setUp()
        self.addCleanup(fixture.tearDown)
        return fixture

    def gate(self,scheduler):
        # Test the production storage guard without creating global RNS state.
        service=TrackDirectoryService.__new__(TrackDirectoryService)
        service.storage_retry={}
        service.replication=scheduler
        return service

    def test_readonly_upload_returns_retryable_response_without_empty_file_leaks(self):
        self.blobs.db.execute('PRAGMA query_only=ON')
        for _ in range(3):
            reply=self.request(4,[self.digest,len(self.data)])
            self.assertEqual(reply[4:],[503,['storage unavailable']])
            self.assertEqual(list(self.blobs.uploads.iterdir()),[])
            self.assertEqual(self.blobs.db.execute('SELECT count(*) FROM uploads').fetchone()[0],0)
        # Read-only capabilities continue to work during the failed write.
        self.assertEqual(self.request(0,[])[4],200)
        self.blobs.db.execute('PRAGMA query_only=OFF')
        ready=self.request(4,[self.digest,len(self.data)])
        self.assertEqual(ready[4],200)
        self.assertEqual(self.request(4,[self.digest,len(self.data)]),ready)

    def test_readonly_finish_retains_original_upload_and_recovers_same_token(self):
        begin=self.request(4,[self.digest,len(self.data)])
        token=begin[5][0]
        self.assertEqual(self.request(5,[token,0,self.data])[4],200)
        self.blobs.db.execute('PRAGMA query_only=ON')
        self.assertEqual(self.request(6,[token])[4:],[503,['storage unavailable']])
        self.assertEqual(self.blobs.db.execute('SELECT token,offset FROM uploads').fetchone()[:],(token,len(self.data)))
        self.blobs.db.execute('PRAGMA query_only=OFF')
        sealed=self.request(6,[token])
        self.assertEqual(sealed[4],200)
        self.assertEqual(self.request(6,[token]),sealed)
        self.assertTrue(self.blobs.has_file(self.digest,len(self.data)))

    def test_initial_fsync_failure_does_not_leave_unregistered_file_or_expose_path(self):
        with patch('tools.tracks.blob_store.os.fsync',side_effect=OSError(errno.ENOSPC,'disk full at private path')):
            self.assertEqual(self.request(4,[self.digest,len(self.data)])[4:],[503,['storage unavailable']])
        self.assertEqual(list(self.blobs.uploads.iterdir()),[])
        self.assertEqual(self.blobs.db.execute('SELECT count(*) FROM uploads').fetchone()[0],0)
        self.assertEqual(self.request(4,[self.digest,len(self.data)])[4],200)

    def test_failed_sqlite_commit_rolls_back_registration_and_cleans_only_new_file(self):
        self.blobs.db.execute('PRAGMA foreign_keys=ON')
        with self.blobs.db:
            self.blobs.db.execute('CREATE TABLE permitted_tokens (token TEXT PRIMARY KEY)')
            self.blobs.db.execute('CREATE TABLE commit_guard (token TEXT REFERENCES permitted_tokens(token) DEFERRABLE INITIALLY DEFERRED)')
            self.blobs.db.execute('CREATE TRIGGER guard_registration AFTER INSERT ON uploads BEGIN INSERT INTO commit_guard VALUES(hex(NEW.token)); END')
        # The statement succeeds; SQLite rejects the real COMMIT. The service
        # must not hand out an upload token or leave a file from the rollback.
        self.assertEqual(self.request(4,[self.digest,len(self.data)])[4:],[503,['storage unavailable']])
        self.assertFalse(self.blobs.db.in_transaction)
        self.assertEqual(self.blobs.db.execute('SELECT count(*) FROM uploads').fetchone()[0],0)
        self.assertEqual(self.blobs.db.execute('SELECT count(*) FROM commit_guard').fetchone()[0],0)
        self.assertEqual(list(self.blobs.uploads.iterdir()),[])
        with self.blobs.db:self.blobs.db.execute('DROP TRIGGER guard_registration')
        self.assertEqual(self.request(4,[self.digest,len(self.data)])[4],200)

    def test_database_lock_has_bounded_response_and_original_read_recovers(self):
        signed=self.signed()
        self.index.publish(self.author.hash,signed,None)
        object_id=verify_manifest(signed)['object_id']
        self.index.db.execute('PRAGMA busy_timeout=0')
        locker=sqlite3.connect(Path(self.temp.name)/'tracks.sqlite')
        try:
            locker.execute('BEGIN EXCLUSIVE')
            self.assertEqual(self.request(2,[object_id,None])[4:],[503,['storage unavailable']])
        finally:
            locker.rollback()
            locker.close()
        read=self.request(2,[object_id,None])
        self.assertEqual(read[4],200)
        self.assertEqual(read[5],[signed,1])

    def test_actual_sqlite_full_does_not_publish_or_advance_log_and_original_id_recovers(self):
        signed=self.signed()
        with self.index.db:
            self.index.db.execute('CREATE TABLE quota_fill (payload BLOB)')
            # A real SQLite allocation fails while reserving the request. Only
            # the fault site is injected; no exception/result is mocked.
            self.index.db.execute('CREATE TRIGGER pressure_before_receipt BEFORE INSERT ON publish_receipts BEGIN INSERT INTO quota_fill VALUES(zeroblob(32768)); END')
        pages=self.index.db.execute('PRAGMA page_count').fetchone()[0]
        self.index.db.execute(f'PRAGMA max_page_count={pages}')
        with self.assertRaises(sqlite3.OperationalError) as caught:
            with self.index.db:self.index.db.execute('INSERT INTO quota_fill VALUES(zeroblob(32768))')
        self.assertEqual(caught.exception.sqlite_errorcode,sqlite3.SQLITE_FULL)
        failed=self.request(7,[signed,None])
        self.assertEqual(failed[4:],[503,['storage unavailable']])
        for table in ('versions','replication_log','publish_receipts'):
            self.assertEqual(self.index.db.execute(f'SELECT count(*) FROM {table}').fetchone()[0],0)
        self.index.db.execute('PRAGMA max_page_count=10000')
        with self.index.db:self.index.db.execute('DROP TRIGGER pressure_before_receipt')
        published=self.request(7,[signed,None])
        self.assertEqual(published[4],200)
        self.assertEqual(self.request(7,[signed,None]),published)
        self.assertEqual(self.index.db.execute('SELECT count(*) FROM versions').fetchone()[0],1)
        self.assertEqual(self.index.db.execute('SELECT count(*) FROM replication_log').fetchone()[0],1)

    def test_file_committed_before_readonly_peer_state_recovers_exact_pending_range(self):
        fixture=self.fixture()
        fixture.publish(fixture.signed())
        for _ in range(30):
            fixture.step(deliver=False)
            if not fixture.messages:continue
            _,request=fixture.messages.pop(0)
            response=fixture.app.handle(fixture.local.hash,request)
            if unpacked(request)[2]!=8:
                fixture.scheduler.received(fixture.remote_delivery,response)
                continue
            break
        else:self.fail('No pending file range')
        service=self.gate(fixture.scheduler)
        fixture.destination.db.execute('PRAGMA query_only=ON')
        operation=lambda:fixture.scheduler.received(fixture.remote_delivery,response)
        logs=io.StringIO()
        with redirect_stdout(logs),patch('tools.tracks.directory_service.time.monotonic',return_value=100):
            self.assertFalse(service.attempt_storage('replication',operation))
            for _ in range(50):self.assertFalse(service.attempt_storage('replication',operation))
        events=[json.loads(line) for line in logs.getvalue().splitlines()]
        self.assertEqual(len(events),1)
        self.assertEqual(events[0]['sqliteCode'],sqlite3.SQLITE_READONLY)
        self.assertIsNone(fixture.scheduler.active)
        self.assertEqual(fixture.state()['after'],0)
        self.assertEqual(fixture.state()['pending'],request)
        size=len(unpacked(response)[5][3])
        self.assertEqual(fixture.destination.blobs.db.execute('SELECT offset FROM uploads').fetchone()[0],size)
        fixture.destination.db.execute('PRAGMA query_only=OFF')
        with patch('tools.tracks.directory_service.time.monotonic',return_value=131):
            self.assertTrue(service.attempt_storage('replication',operation))
        self.assertEqual(service.storage_retry,{})
        self.assertEqual(fixture.destination.blobs.db.execute('SELECT offset FROM uploads').fetchone()[0],size)
        fixture.run_to_checkpoint(1)

    def test_cleanup_storage_fault_has_separate_backoff_and_recovers(self):
        fixture=self.fixture()
        upload=fixture.destination.blobs.begin(fixture.remote_delivery,hashlib.sha256(b'expired').digest(),7)
        fixture.now+=3601
        service=self.gate(fixture.scheduler)
        fixture.destination.blobs.db.execute('PRAGMA query_only=ON')
        with redirect_stdout(io.StringIO()),patch('tools.tracks.directory_service.time.monotonic',return_value=100):
            self.assertFalse(service.attempt_storage('cleanup',fixture.destination.blobs.cleanup_expired,delay=60))
            # Cleanup failures do not block the other guard or ordinary reads.
            self.assertTrue(service.attempt_storage('replication',lambda:None))
        self.assertEqual(service.storage_retry,{'cleanup':160})
        self.assertIsNotNone(fixture.destination.blobs.db.execute('SELECT 1 FROM uploads WHERE token=?',(upload['token'],)).fetchone())
        fixture.destination.blobs.db.execute('PRAGMA query_only=OFF')
        with patch('tools.tracks.directory_service.time.monotonic',return_value=161):
            self.assertTrue(service.attempt_storage('cleanup',fixture.destination.blobs.cleanup_expired,delay=60))
        self.assertEqual(fixture.destination.blobs.db.execute('SELECT count(*) FROM uploads').fetchone()[0],0)

    def test_local_version_capacity_keeps_checkpoint_and_preserves_existing_content(self):
        fixture=self.fixture()
        first=fixture.signed()
        fixture.publish(first)
        fixture.destination.max_versions=1
        fixture.run_to_checkpoint(1)
        fixture.publish(fixture.signed(first,title='Second'))
        fixture.now+=301
        for _ in range(40):fixture.step()
        state=fixture.state()
        self.assertEqual(state['after'],1)
        self.assertEqual(state['health'],'Backoff')
        self.assertEqual(fixture.destination.get_manifest(verify_manifest(first)['object_id'])['signed'],first)
        fixture.destination.max_versions=2
        fixture.now+=61
        fixture.run_to_checkpoint(2)

    def test_timed_out_peer_yields_to_another_verified_directory(self):
        fixture=self.fixture()
        other=RNS.Identity()
        other_delivery=RNS.Destination.hash_from_name_and_identity('lxmf.delivery',other)
        second=fixture.store('second')
        signed=fixture.signed()
        fixture.put(second,fixture.source.blobs._final(unpacked(signed[0])[13][0]).read_bytes())
        second.publish(fixture.author.hash,signed,None)
        app=TrackApplication(other,second.blobs,second)
        fixture.publish(signed)
        # Establish and withhold the first peer's authenticated capability reply.
        fixture.step(deliver=False)
        _,first_request=fixture.messages.pop()
        fixture.scheduler.received_announce(RNS.Destination.hash_from_name_and_identity('trailmate.track.directory',other),other,
            packed([1,'trailmate.track.v1',other_delivery,app.epoch,'Other']))
        fixture.scheduler.step(lambda public,raw:fixture.messages.append((public,raw)))
        fixture.now+=121
        for _ in range(60):
            fixture.scheduler.step(lambda public,raw:fixture.messages.append((public,raw)))
            if fixture.messages:
                public,raw=fixture.messages.pop(0)
                self.assertEqual(public,other.get_public_key())
                fixture.scheduler.received(other_delivery,app.handle(fixture.local.hash,raw))
            row=fixture.destination.db.execute('SELECT * FROM track_replica_peers WHERE delivery=?',(other_delivery,)).fetchone()
            if row and fixture.scheduler.decode(row)['after']==1:break
        else:self.fail('Second directory starved')
        self.assertEqual(fixture.state()['after'],0)
        self.assertEqual(fixture.state()['pending'],first_request)
        self.assertEqual(fixture.state()['health'],'Backoff')


if __name__=='__main__':unittest.main()
