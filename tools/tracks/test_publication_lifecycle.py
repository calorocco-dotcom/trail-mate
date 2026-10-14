"""Complete CMP1 lifecycle against real identity signatures and durable stores."""
import hashlib
import tempfile
import unittest
import RNS
from tools.tracks.application import TrackApplication
from tools.tracks.blob_store import BlobStore
from tools.tracks.directory_index import DirectoryIndex
from tools.tracks.protocol import packed, unpacked, verify_manifest
from tools.tracks.receipt import verify_receipt


class PublicationLifecycleTests(unittest.TestCase):
    def test_publish_update_archive_restart_and_conflicting_parent(self):
        with tempfile.TemporaryDirectory() as state:
            author, directory, other = RNS.Identity(), RNS.Identity(), RNS.Identity()
            blobs = BlobStore(state)
            index = DirectoryIndex(state, blobs)
            sequence = 0
            try:
                app = TrackApplication(directory, blobs, index)

                def request(operation, body, *, source=None, identifier=None):
                    nonlocal sequence
                    sequence += 1
                    identifier = identifier or sequence.to_bytes(16, 'big')
                    result = unpacked(app.handle(source or author.hash, packed([1, 0, operation, identifier, 8192, body])))
                    self.assertEqual(result[:4], [1, 1, operation, identifier])
                    return result

                def upload(coordinates):
                    content = ('<kml xmlns="http://www.opengis.net/kml/2.2"><Placemark><LineString><coordinates>' + coordinates + '</coordinates></LineString></Placemark></kml>').encode()
                    digest = hashlib.sha256(content).digest()
                    begin = request(4, [digest, len(content)])
                    self.assertEqual(begin[4], 200)
                    token = begin[5][0]
                    for offset in range(0, len(content), 17):
                        self.assertEqual(request(5, [token, offset, content[offset:offset + 17]])[4], 200)
                    self.assertEqual(request(6, [token])[5][:3], [digest, len(content), 1])
                    return content, [digest, len(content), 'application/vnd.google-earth.kml+xml', 1]

                def sign(record):
                    raw = packed(record)
                    return [raw, author.sign(b'TrailMate.Track.Manifest.v1\0' + raw)]

                original, first_file = upload('120,30 120.01,30.01')
                revised, second_file = upload('121,31 121.02,31.02')
                first = [1, author.get_public_key(), b'n' * 16, 0, 1, None, 0, 100, 100, 'Original', '', [1200000000,300000000,1200100000,300100000], [0,None,None,2], first_file]
                first_signed = sign(first)
                first_verified = verify_manifest(first_signed)
                first_id = b'a' * 16
                first_reply = request(7, [first_signed, None], identifier=first_id)
                self.assertEqual(first_reply[4], 200)
                verify_receipt(first_reply[5][0], directory.get_public_key(), app.epoch, first_id, first_signed)

                second = list(first)
                second[4:6] = [2, first_verified['revision_hash']]
                second[8:10] = [101, 'Revised']
                second[11], second[13] = [1210000000,310000000,1210200000,310200000], second_file
                second_signed = sign(second)
                second_verified = verify_manifest(second_signed)
                second_id = b'b' * 16
                second_reply = request(7, [second_signed, first_verified['revision_hash']], identifier=second_id)
                self.assertEqual(second_reply[4], 200)
                self.assertEqual(second_verified['object_id'], first_verified['object_id'])
                verify_receipt(second_reply[5][0], directory.get_public_key(), app.epoch, second_id, second_signed)
                stale = list(second)
                stale[9] = 'Concurrent stale revision'
                self.assertEqual(request(7, [sign(stale), first_verified['revision_hash']])[4], 422)
                self.assertEqual(request(7, [second_signed, first_verified['revision_hash']], source=other.hash)[4], 200)
                forged = [second_signed[0], bytes([second_signed[1][0] ^ 1]) + second_signed[1][1:]]
                self.assertEqual(request(7, [forged, first_verified['revision_hash']], source=other.hash)[4], 422)

                archive = list(second)
                archive[4:7] = [3, second_verified['revision_hash'], 1]
                archive[8] = 102
                changed_archive = list(archive)
                changed_archive[13] = first_file
                self.assertEqual(request(7, [sign(changed_archive), second_verified['revision_hash']])[4], 422)
                archive_signed, archive_id = sign(archive), b'c' * 16
                archive_reply = request(7, [archive_signed, second_verified['revision_hash']], identifier=archive_id)
                self.assertEqual(archive_reply[4], 200)
                verify_receipt(archive_reply[5][0], directory.get_public_key(), app.epoch, archive_id, archive_signed)
                object_id = first_verified['object_id']
                self.assertEqual(request(2, [object_id, None])[5], [archive_signed, 1])
                self.assertEqual(request(2, [object_id, first_verified['revision_hash']])[5], [first_signed, 0])
                world = [-1800000000,-900000000,1799999999,900000000]
                self.assertEqual(request(1, [world,None,author.get_public_key(),0,16,None,None])[5][2], [])
                self.assertEqual(request(1, [world,None,author.get_public_key(),1,16,None,None])[5][2][0][0], object_id)
                self.assertEqual(request(8, [second_file[0],0,len(revised)])[5][3], revised)
                self.assertEqual(request(8, [first_file[0],0,len(original)])[5][3], original)

                epoch = app.epoch
                index.close()
                index = DirectoryIndex(state, blobs)
                app = TrackApplication(directory, blobs, index)
                self.assertEqual(app.epoch, epoch)
                self.assertEqual(request(2, [object_id,None])[5], [archive_signed,1])
                self.assertEqual(request(7, [archive_signed,second_verified['revision_hash']],identifier=archive_id), archive_reply)
            finally:
                index.close()
                blobs.close()


if __name__ == '__main__':
    unittest.main()
