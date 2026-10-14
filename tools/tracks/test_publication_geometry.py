import hashlib
import tempfile
import unittest
import RNS
from tools.tracks.application import TrackApplication
from tools.tracks.blob_store import BlobStore
from tools.tracks.directory_index import DirectoryIndex
from tools.tracks.protocol import packed, unpacked


class PublicationGeometryTests(unittest.TestCase):
    def test_invalid_xml_empty_geometry_and_false_metadata_never_enter_index(self):
        with tempfile.TemporaryDirectory() as state:
            author, directory = RNS.Identity(), RNS.Identity()
            blobs, index = BlobStore(state), None
            try:
                index = DirectoryIndex(state, blobs)
                app = TrackApplication(directory, blobs, index)
                valid = b'<kml xmlns="http://www.opengis.net/kml/2.2"><Placemark><Point><coordinates>0,0</coordinates></Point></Placemark></kml>'
                cases = [(b'<kml/>', [0,0,0,0], 1), (valid[:-6], [0,0,0,0], 1),
                         (b'<!DOCTYPE kml [<!ENTITY p "0,0">]>' + valid.replace(b'0,0', b'&p;'), [0,0,0,0], 1),
                         (valid, [100,100,200,200], 1), (valid, [0,0,0,0], 2)]
                for sequence, (data, box, count) in enumerate(cases, 1):
                    digest = hashlib.sha256(data).digest()
                    token = blobs.begin(author.hash, digest, len(data))['token']
                    if token:
                        blobs.append(author.hash, token, 0, data)
                        blobs.finish(author.hash, token)
                    record = [1,author.get_public_key(),bytes([sequence])*16,0,1,None,0,100,100,'Bad geometry','',box,[0,None,None,count],[digest,len(data),'application/vnd.google-earth.kml+xml',1]]
                    raw = packed(record)
                    signed = [raw,author.sign(b'TrailMate.Track.Manifest.v1\0'+raw)]
                    response = unpacked(app.handle(author.hash,packed([1,0,7,bytes([sequence])*16,8192,[signed,None]])))
                    self.assertEqual(response[4],422)
                    self.assertEqual(index.db.execute('SELECT count(*) FROM versions').fetchone()[0],0)
                    self.assertEqual(index.db.execute('SELECT count(*) FROM heads').fetchone()[0],0)
                    self.assertEqual(index.db.execute('SELECT count(*) FROM publish_receipts').fetchone()[0],0)
            finally:
                if index:
                    index.close()
                blobs.close()


if __name__ == '__main__':
    unittest.main()
