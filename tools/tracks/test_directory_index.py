import hashlib
import tempfile
import unittest
import RNS
from tools.tracks.blob_store import BlobStore
from tools.tracks.directory_index import DirectoryIndex
from tools.tracks.protocol import packed, verify_manifest


class IndexTests(unittest.TestCase):
    def test_v2_requires_package_preflight_before_any_version_write(self):
        with tempfile.TemporaryDirectory() as state:
            identity = RNS.Identity()
            blobs = BlobStore(state)
            index = DirectoryIndex(state, blobs)
            record = [2, identity.get_public_key(), b"n" * 16, 0, 1, None, 0, 100, 100,
                      "track", "", [0, 0, 0, 0], [0, None, None, 1],
                      [b"f" * 32, 100, "application/vnd.google-earth.kml+xml", 1],
                      [0, [], 0, 0, 0, 0, 0, None], None]
            raw = packed(record)
            signed = [raw, identity.sign(b"TrailMate.Track.Manifest.v2\0" + raw)]
            self.assertEqual(verify_manifest(signed)["record"][0], 2)
            with self.assertRaisesRegex(ValueError, "KML not stored"):
                index.publish(identity.hash, signed, None)
            self.assertEqual(index.db.execute("SELECT count(*) FROM versions").fetchone()[0], 0)
            self.assertEqual(index.db.execute("SELECT count(*) FROM heads").fetchone()[0], 0)
            index.close()
            blobs.close()

    def test_query_pagination_binds_filters(self):
        with tempfile.TemporaryDirectory() as state:
            identity, data = RNS.Identity(), b'<kml xmlns="http://www.opengis.net/kml/2.2"><Placemark><Point><coordinates>0,0</coordinates></Point></Placemark></kml>'
            digest = hashlib.sha256(data).digest()
            blobs = BlobStore(state)
            token = blobs.begin(identity.hash, digest, len(data))["token"]
            blobs.append(identity.hash, token, 0, data)
            blobs.finish(identity.hash, token)
            index = DirectoryIndex(state, blobs)
            for number in range(1, 4):
                record = [1,identity.get_public_key(),bytes([number])*16,0,1,None,0,100,100,"track","",[0,0,0,0],[0,None,None,1],[digest,len(data),"application/vnd.google-earth.kml+xml",1]]
                raw = packed(record)
                index.publish(identity.hash,[raw,identity.sign(b"TrailMate.Track.Manifest.v1\0"+raw)],None)
            box = [-10000000,-10000000,10000000,10000000]
            first = index.query(box,limit=1)
            second = index.query(box,limit=1,snapshot=first["snapshot"],cursor=first["cursor"])
            third = index.query(box,limit=1,snapshot=first["snapshot"],cursor=second["cursor"])
            self.assertEqual(len({page["items"][0][0] for page in (first,second,third)}),3)
            self.assertIsNone(third["cursor"])
            with self.assertRaises(ValueError):
                index.query([0,0,1,1],limit=1,snapshot=first["snapshot"],cursor=first["cursor"])
            index.close()
            blobs.close()

    def test_publish_update_archive_and_restart(self):
        with tempfile.TemporaryDirectory() as state:
            identity, data = RNS.Identity(), b'<kml xmlns="http://www.opengis.net/kml/2.2"><Placemark><Point><coordinates>0,0</coordinates></Point></Placemark></kml>'
            digest = hashlib.sha256(data).digest()
            blobs, index = BlobStore(state), None
            index = DirectoryIndex(state, blobs)
            record = [1, identity.get_public_key(), b"n" * 16, 0, 1, None, 0, 100, 100, "山径", "", [0,0,0,0], [0,None,None,1], [digest,len(data),"application/vnd.google-earth.kml+xml",1]]
            def sign():
                raw = packed(record)
                return [raw, identity.sign(b"TrailMate.Track.Manifest.v1\0" + raw)]
            first = sign()
            with self.assertRaises(ValueError):
                index.publish(identity.hash, first, None)
            token = blobs.begin(identity.hash, digest, len(data))["token"]
            blobs.append(identity.hash, token, 0, data)
            blobs.finish(identity.hash, token)
            accepted = index.publish(identity.hash, first, None)
            page = index.query([-10000000,-10000000,10000000,10000000], author=identity.get_public_key())
            self.assertEqual(page["items"][0][0], accepted["object"])
            self.assertEqual(index.query([10000000,10000000,20000000,20000000])["items"], [])
            self.assertEqual(index.publish(identity.hash, first, None), accepted)
            with self.assertRaises(ValueError):
                index.publish(identity.hash, first, b"x" * 32)
            self.assertEqual(index.publish(b"x" * 16, first, None), accepted)
            invalid = [first[0], bytes([first[1][0] ^ 1]) + first[1][1:]]
            with self.assertRaises(ValueError):
                index.publish(b"x" * 16, invalid, None)
            record[4], record[5], record[8] = 2, accepted["hash"], 101
            record[6] = 1
            archived = sign()
            with self.assertRaises(ValueError):
                index.publish(identity.hash, archived, None)
            second = index.publish(identity.hash, archived, accepted["hash"])
            self.assertEqual(index.query([-10000000,-10000000,10000000,10000000])["items"], [])
            self.assertEqual(len(index.query([-10000000,-10000000,10000000,10000000], state=1)["items"]), 1)
            with self.assertRaises(ValueError):
                index.query([-10000000,-10000000,10000000,10000000], snapshot=page["snapshot"])
            self.assertEqual(index.publish(identity.hash, archived, accepted["hash"]), second)
            with self.assertRaises(ValueError):
                index.publish(identity.hash, archived, b"x" * 32)
            self.assertFalse(index.get_manifest(accepted["object"], accepted["hash"])["current"])
            index.close()
            index = DirectoryIndex(state, blobs)
            self.assertEqual(verify_manifest(index.get_manifest(second["object"])["signed"])["record"][6], 1)
            self.assertEqual(index.get_manifest(accepted["object"], accepted["hash"])["signed"], first)
            original = index.get_manifest(second["object"])["signed"]
            with index.db:
                index.db.execute("UPDATE versions SET signature=? WHERE hash=?", (bytes([original[1][0] ^ 1]) + original[1][1:], second["hash"]))
            with self.assertRaises(ValueError):
                index.publish(identity.hash, archived, accepted["hash"])
            index.close()
            blobs.close()
