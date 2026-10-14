import hashlib
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from tools.tracks.blob_store import BlobStore


class BlobTests(unittest.TestCase):
    def test_expiry_cleanup_keeps_unknown_and_retries_failure(self):
        with tempfile.TemporaryDirectory() as state:
            clock = [100]
            store = BlobStore(state, max_bytes=5, clock=lambda: clock[0])
            token = store.begin(b"a" * 16, b"h" * 32, 3)["token"]
            store.append(b"a" * 16, token, 0, b"abc")
            orphan = Path(state) / "uploads" / "unknown"
            orphan.write_bytes(b"xy")
            self.assertEqual(store.cleanup_expired(), 0)
            clock[0] = 3700
            with self.assertRaises(ValueError):
                store.append(b"a" * 16, token, 0, b"abc")
            with patch.object(Path, "unlink", side_effect=OSError("busy")):
                self.assertEqual(store.cleanup_expired(), 0)
            self.assertEqual(store.db.execute("SELECT count(*) FROM uploads").fetchone()[0], 1)
            self.assertEqual(store.cleanup_expired(), 1)
            self.assertEqual(orphan.read_bytes(), b"xy")
            self.assertFalse((Path(state) / "uploads" / token.hex()).exists())
            self.assertEqual(store.begin(b"b" * 16, b"i" * 32, 3)["offset"], 0)
            store.close()

    def test_unknown_files_count_and_slot_reservation(self):
        with tempfile.TemporaryDirectory() as state:
            store = BlobStore(state, max_bytes=10, max_files=8)
            orphan = Path(state) / "uploads" / "unknown"
            orphan.write_bytes(b"123456")
            with self.assertRaises(ValueError):
                store.begin(b"a" * 16, b"h" * 32, 5)
            self.assertEqual(orphan.read_bytes(), b"123456")
            orphan.unlink()
            store.begin(b"a" * 16, b"h" * 32, 3)
            store.begin(b"b" * 16, b"i" * 32, 7)
            with self.assertRaises(ValueError):
                store.begin(b"c" * 16, b"j" * 32, 1)
            store.close()
        with tempfile.TemporaryDirectory() as state:
            store = BlobStore(state, max_files=2)
            (Path(state) / "files" / "unknown").write_bytes(b"x")
            with self.assertRaises(ValueError):
                store.begin(b"a" * 16, b"h" * 32, 1)
            store.close()

    def test_restart_resume_and_hash(self):
        with tempfile.TemporaryDirectory() as state:
            owner, source = b"a" * 16, b"<kml>" + b"x" * 500 + b"</kml>"
            digest = hashlib.sha256(source).digest()
            store = BlobStore(state)
            token = store.begin(owner, digest, len(source))["token"]
            self.assertEqual(store.append(owner, token, 0, source[:120]), 120)
            self.assertEqual(store.append(owner, token, 0, source[:120]), 120)
            with self.assertRaises(ValueError):
                store.append(b"b" * 16, token, 120, source[120:])
            store.close()
            store = BlobStore(state)
            self.assertEqual(store.begin(owner, digest, len(source))["offset"], 120)
            store.append(owner, token, 120, source[120:])
            self.assertTrue(store.finish(owner, token)["sealed"])
            self.assertEqual(list((Path(state) / "uploads").iterdir()), [])
            self.assertEqual((Path(state) / "files" / (digest.hex() + ".kml")).read_bytes(), source)
            self.assertTrue(store.begin(owner, digest, len(source))["sealed"])
            store.close()
            store = BlobStore(state)
            self.assertTrue(store.finish(owner, token)["sealed"])
            with self.assertRaises(ValueError):
                store.finish(b"b" * 16, token)
            store.close()

    def test_cleanup_failure_preserves_completion(self):
        with tempfile.TemporaryDirectory() as state:
            source, owner = b"<kml/>", b"a" * 16
            store = BlobStore(state)
            token = store.begin(owner, hashlib.sha256(source).digest(), len(source))["token"]
            store.append(owner, token, 0, source)
            with patch.object(Path, "unlink", side_effect=OSError("busy")):
                result = store.finish(owner, token)
            self.assertTrue(result["sealed"])
            self.assertTrue(result["cleanup_pending"])
            self.assertFalse(store.finish(owner, token)["cleanup_pending"])
            self.assertEqual(list((Path(state) / "uploads").iterdir()), [])
            store.close()

    def test_final_file_precedes_database_cleanup(self):
        with tempfile.TemporaryDirectory() as state:
            source, owner = b"<kml/>", b"a" * 16
            digest = hashlib.sha256(source).digest()
            store = BlobStore(state)
            token = store.begin(owner, digest, len(source))["token"]
            store.append(owner, token, 0, source)
            temporary = Path(state) / "uploads" / token.hex()
            final = Path(state) / "files" / (digest.hex() + ".kml")
            os.link(temporary, final)
            temporary.unlink()
            store.close()
            store = BlobStore(state)
            self.assertTrue(store.finish(owner, token)["sealed"])
            self.assertEqual(store.db.execute("SELECT count(*) FROM uploads").fetchone()[0], 0)
            self.assertEqual(final.read_bytes(), source)
            store.close()

    def test_capacity_and_bad_hash(self):
        with tempfile.TemporaryDirectory() as state:
            store = BlobStore(state, max_bytes=10)
            with self.assertRaises(ValueError):
                store.begin(b"a" * 16, b"h" * 32, 11)
            token = store.begin(b"a" * 16, b"h" * 32, 3)["token"]
            store.append(b"a" * 16, token, 0, b"bad")
            with self.assertRaises(ValueError):
                store.finish(b"a" * 16, token)
            self.assertEqual(list((Path(state) / "files").iterdir()), [])
            store.close()
