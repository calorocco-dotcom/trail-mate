import hashlib
import tempfile
import unittest
import RNS
from tools.tracks.application import TrackApplication
from tools.tracks.blob_store import BlobStore
from tools.tracks.directory_index import DirectoryIndex
from tools.tracks.protocol import packed, unpacked, verify_manifest
from tools.tracks.attachment_index import encode_index
from tools.tracks.receipt import verify_receipt


class AttachmentPublicationTests(unittest.TestCase):
    def test_required_optional_status_corruption_and_receipt_restart(self):
        with tempfile.TemporaryDirectory() as state:
            author, directory = RNS.Identity(), RNS.Identity()
            blobs = BlobStore(state)
            index = DirectoryIndex(state, blobs)
            app = TrackApplication(directory, blobs, index)
            owner = b"s" * 16  # browser transport identity differs from author
            sequence = 0
            def request(operation, body, identifier=None, budget=8192):
                nonlocal sequence
                sequence += 1
                identifier = identifier or sequence.to_bytes(16, "big")
                return unpacked(app.handle(owner, packed([1, 0, operation, identifier, budget, body])))
            def upload(data):
                digest = hashlib.sha256(data).digest()
                admitted = request(4, [digest, len(data)])
                self.assertEqual(admitted[4], 200)
                token, offset, sealed, _ = admitted[5]
                if not sealed:
                    for start in range(offset, len(data), 1024):
                        self.assertEqual(request(5, [token, start, data[start:start+1024]])[4], 200)
                    self.assertEqual(request(6, [token])[4], 200)
                return digest
            kml = b'<kml xmlns="http://www.opengis.net/kml/2.2"><Placemark><description><![CDATA[<img src="photo.png">]]></description><Point><coordinates>0,0</coordinates></Point></Placemark></kml>'
            image, notes = b"image fixture", b"optional notes"
            kml_hash, image_hash, notes_hash = (hashlib.sha256(value).digest() for value in (kml, image, notes))
            asset_bytes = encode_index([1, kml_hash, len(kml), [
                ["photo", image_hash, len(image), "image/png", "photo.png", 0, 1],
                ["notes", notes_hash, len(notes), "text/plain", "notes.txt", 3, 0]]])
            record = [2, author.get_public_key(), b"n" * 16, 0, 1, None, 0, 100, 100, "track", "",
                      [0, 0, 0, 0], [0, None, None, 1], [kml_hash, len(kml), "application/vnd.google-earth.kml+xml", 1],
                      [1, ["hike"], 1, 0, 1, 0, 0, None], [hashlib.sha256(asset_bytes).digest(), len(asset_bytes), 2, 1]]
            raw = packed(record)
            signed = [raw, author.sign(b"TrailMate.Track.Manifest.v2\0" + raw)]
            self.assertEqual(request(0, [])[5][7], list(range(9)))
            self.assertIn(14, request(0, [2])[5][7])
            self.assertEqual(request(14, [signed])[5][2:5], [0, 0, 0])
            upload(kml)
            upload(asset_bytes)
            status = request(14, [signed])[5]
            self.assertEqual(status[2:5], [1, 1, 0])
            self.assertEqual(request(7, [signed, None])[4], 422)
            self.assertEqual(index.db.execute("SELECT count(*) FROM versions").fetchone()[0], 0)
            upload(image)
            status = request(14, [signed])[5]
            self.assertEqual(status[2:5], [1, 1, 1])
            self.assertEqual([row[4] for row in status[5]], [1, 0])
            publish_id = b"p" * 16
            accepted = request(7, [signed, None], publish_id)
            self.assertEqual(accepted[4], 200)
            verify_receipt(accepted[5][0], directory.get_public_key(), app.epoch, publish_id, signed)
            self.assertEqual(index.get_manifest(verify_manifest(signed)["object_id"])["signed"], signed)
            upload(notes)
            self.assertEqual([row[4] for row in request(14, [signed])[5][5]], [1, 1])
            index.close()
            blobs.close()
            blobs = BlobStore(state)
            index = DirectoryIndex(state, blobs)
            app = TrackApplication(directory, blobs, index)
            self.assertEqual(request(7, [signed, None], publish_id), accepted)
            self.assertEqual(request(14, [signed])[5][4], 1)
            blobs._final(image_hash).write_bytes(b"corrupted")
            self.assertEqual(request(14, [signed])[5][4], 0)
            # Receipt records acceptance at that time. Fresh status is separate.
            self.assertEqual(request(7, [signed, None], publish_id), accepted)
            self.assertEqual(request(7, [signed, None], b"q" * 16)[4], 422)
            self.assertEqual(index.db.execute("SELECT count(*) FROM versions").fetchone()[0], 1)
            index.close()
            blobs.close()
