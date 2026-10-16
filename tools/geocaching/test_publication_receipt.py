import os
from pathlib import Path
import tempfile
import unittest
import RNS
from directory_store import DirectoryStore, packed, unpacked, verify_cache


class PublicationReceiptTests(unittest.TestCase):
    def test_signed_head_proof_is_durable_and_rejects_other_bindings(self):
        directory, author = RNS.Identity(), RNS.Identity()
        destination = RNS.Destination.hash_from_name_and_identity("lxmf.delivery", directory)
        now = 1800000000
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "directory.sqlite"
            store = DirectoryStore(path, destination, clock=lambda: now, receipt_identity=directory)
            def request(op, body, token=None):
                return unpacked(store.handle(b"s" * 16, packed([1, 0, op, token or os.urandom(16), 8192, body])))
            record = [1, author.get_public_key(), b"n" * 16, 1, None, 0, 1, 1, "Receipt", "", "", 2, 2, 0, now, now]
            raw = packed(record)
            signed = [raw, author.sign(b"trailmate.geocache/sign/v1\0" + raw)]
            item = verify_cache(signed)
            self.assertIn(5, store.capabilities()[2])
            self.assertEqual(request(5, [item.cache_id, item.revision_hash, record[1]])[4], 404)
            self.assertEqual(request(1, [signed])[4], 200)
            token = os.urandom(16)
            body = [item.cache_id, item.revision_hash, record[1]]
            reply = request(5, body, token)
            self.assertEqual(reply[4], 200)
            proof, signature = reply[5]
            self.assertTrue(directory.validate(signature, b"trailmate.geocache/receipt/v1\0" + proof))
            self.assertEqual(unpacked(proof), [1, destination, store.epoch, item.cache_id, 1, item.revision_hash, 0, now])
            self.assertEqual(request(5, [item.cache_id, b"x" * 32, record[1]])[4], 409)
            self.assertEqual(request(5, [item.cache_id, item.revision_hash, RNS.Identity().get_public_key()])[4], 409)
            store.close()
            now += 100
            store = DirectoryStore(path, destination, clock=lambda: now, receipt_identity=directory)
            self.assertEqual(request(5, body, token), reply)
            record[3], record[4], record[5], record[15] = 2, item.revision_hash, 2, now
            archived_raw = packed(record)
            archived = [archived_raw, author.sign(b"trailmate.geocache/sign/v1\0" + archived_raw)]
            archive_item = verify_cache(archived)
            self.assertEqual(request(1, [archived])[4], 200)
            self.assertEqual(request(5, body)[4], 409)
            archive_proof = request(5, [archive_item.cache_id, archive_item.revision_hash, record[1]])
            self.assertEqual(archive_proof[4], 200)
            self.assertEqual(unpacked(archive_proof[5][0])[4:7], [2, archive_item.revision_hash, 2])
            store.close()


if __name__ == "__main__":
    unittest.main()
