"""Completed reads release capacity without breaking frozen pages or receipts."""
import os
from pathlib import Path
import tempfile
import unittest

import RNS

from directory_store import DirectoryStore, CURSOR_TTL, packed, unpacked, verify_cache


class SnapshotRetentionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.path = Path(self.temp.name) / 'directory.sqlite'
        self.now = 1800000000
        self.destination, self.source = os.urandom(16), os.urandom(16)
        self.author = RNS.Identity()
        self.open()

    def open(self):
        self.store = DirectoryStore(self.path, self.destination, clock=lambda: self.now,
                                    max_snapshots=1, requests_per_minute=10000)

    def tearDown(self):
        self.store.close()
        self.temp.cleanup()

    def request(self, operation, body, request_id=None):
        raw = packed([1, 0, operation, request_id or os.urandom(16), 8192, body])
        response = self.store.handle(self.source, raw)
        self.assertIsNotNone(response)
        return unpacked(response)

    def query(self, cursor=None, limit=1, request_id=None):
        return self.request(2, [[-900000000, -1800000000, 900000000, 1800000000],
                               7, None, limit, cursor], request_id)

    def publish(self, title='Snapshot test'):
        record = [1, self.author.get_public_key(), os.urandom(16), 1, None, 0,
                  305000000, 1205000000, title, '', '', 2, 2, 0, self.now, self.now]
        raw = packed(record)
        signed = [raw, self.author.sign(b'trailmate.geocache/sign/v1\0' + raw)]
        self.assertEqual(self.request(1, [signed])[4], 200)
        return verify_cache(signed)

    def count(self):
        return self.store.db.execute('SELECT COUNT(*) FROM snapshots').fetchone()[0]

    def test_hundreds_of_complete_and_empty_reads_do_not_exhaust_capacity(self):
        for _ in range(300):
            result = self.query()
            self.assertEqual(result[4], 200)
            self.assertIsNone(result[5][2])
            self.assertEqual(self.count(), 0)
        self.publish()
        for _ in range(300):
            self.assertEqual(len(self.query()[5][1]), 1)
            self.assertEqual(self.count(), 0)
        self.assertEqual(self.store.sequence(), 1)

    def test_pending_page_survives_rejected_admission_and_restart(self):
        items = [self.publish() for _ in range(3)]
        first_id = os.urandom(16)
        first = self.query(request_id=first_id)
        self.assertIsNotNone(first[5][2])
        self.assertEqual(self.count(), 1)
        self.assertEqual(self.query()[4], 503)
        self.publish('Not part of frozen query')
        self.store.close()
        self.open()
        self.assertEqual(self.query(request_id=first_id), first)
        second_id = os.urandom(16)
        second = self.query(first[5][2], request_id=second_id)
        last = self.query(second[5][2])
        self.assertIsNone(last[5][2])
        self.assertEqual(self.count(), 0)
        self.assertEqual({r[0] for r in first[5][1]+second[5][1]+last[5][1]},
                         {item.cache_id for item in items})
        # Both durable request replay and cursor replay survive item-list removal.
        self.assertEqual(self.query(first[5][2], request_id=second_id), second)
        self.assertEqual(self.query(first[5][2])[5], second[5])
        self.assertEqual(self.query(second[5][2])[5], last[5])
        self.assertEqual(self.query(limit=64)[4], 200)
        self.now += CURSOR_TTL + 1
        self.assertEqual(self.query(first[5][2])[4], 410)

    def test_empty_sync_checkpoints_release_capacity_and_observe_new_versions(self):
        checkpoint = self.request(4, [None, 1])[5]
        self.assertEqual(checkpoint[4], 0)
        self.assertEqual(self.count(), 0)
        for _ in range(300):
            checkpoint = self.request(4, [checkpoint[3], 1])[5]
            self.assertEqual(checkpoint[2], [])
            self.assertEqual(self.count(), 0)
        item = self.publish()
        delta = self.request(4, [checkpoint[3], 1])[5]
        self.assertEqual(delta[2], [[item.cache_id, 1, item.revision_hash, 0]])
        self.assertEqual(self.count(), 0)

    def test_sync_live_page_and_replays_survive_completion(self):
        items = [self.publish() for _ in range(3)]
        first = self.request(4, [None, 1])[5]
        second = self.request(4, [first[3], 1])[5]
        self.assertEqual(self.count(), 1)
        self.assertEqual(self.query()[4], 503)
        last = self.request(4, [second[3], 1])[5]
        self.assertEqual(last[4], 0)
        self.assertEqual(self.count(), 0)
        self.assertEqual(self.request(4, [first[3], 1])[5], second)
        self.assertEqual(self.request(4, [second[3], 1])[5], last)
        self.assertEqual({row[0] for row in first[2]+second[2]+last[2]},
                         {item.cache_id for item in items})

    def test_existing_completed_snapshot_reclaimed_but_live_cursor_kept(self):
        legacy_id = os.urandom(16)
        with self.store.db:
            self.store.db.execute('INSERT INTO snapshots VALUES(?,?,?,?,?,?,?)',
                                 (legacy_id, self.source, 2, b'old', 0, 0, self.now+CURSOR_TTL))
            self.store.db.execute('INSERT INTO cursors VALUES(?,?,?,?,?,?,?,?)',
                                 (os.urandom(32), self.source, 2, b'old', legacy_id, 0,
                                  self.now+CURSOR_TTL, packed([legacy_id, [], None, CURSOR_TTL])))
        self.assertEqual(self.count(), 1)
        self.assertEqual(self.query()[4], 200)
        self.assertEqual(self.count(), 0)
        self.publish()
        self.publish()
        first = self.query()
        self.store.maintenance()
        self.assertEqual(self.count(), 1)
        self.assertEqual(self.query(first[5][2])[4], 200)
        self.assertEqual(self.count(), 0)


if __name__ == '__main__':
    unittest.main()
