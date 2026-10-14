import hashlib
import tempfile
import unittest
from unittest.mock import patch
import RNS
from tools.tracks.application import TrackApplication
from tools.tracks.blob_store import BlobStore
from tools.tracks.directory_index import DirectoryIndex
from tools.tracks.protocol import packed, unpacked, verify_manifest
from tools.tracks.receipt import verify_receipt


class ApplicationTests(unittest.TestCase):
    def test_wire_upload_and_capabilities(self):
        with tempfile.TemporaryDirectory() as state:
            author, directory = RNS.Identity(), RNS.Identity()
            blobs = BlobStore(state)
            index = DirectoryIndex(state,blobs)
            app = TrackApplication(directory,blobs,index)
            request_id = b"r" * 16
            def request(operation,body,budget=8192):
                response=unpacked(app.handle(author.hash,packed([1,0,operation,request_id,budget,body])))
                self.assertEqual(response[:4],[1,1,operation,request_id])
                return response
            self.assertEqual(request(0,[])[5][1],"trailmate.track.v1")
            data=b'<kml xmlns="http://www.opengis.net/kml/2.2"><Placemark><Point><coordinates>0,0</coordinates></Point></Placemark></kml>'
            digest=hashlib.sha256(data).digest()
            response=request(4,[digest,len(data)])
            self.assertEqual(response[4],200)
            token=response[5][0]
            self.assertEqual(request(5,[token,0,data])[5],[token,len(data)])
            self.assertEqual(request(6,[token])[5][:3],[digest,len(data),1])
            self.assertEqual(request(3,[digest,len(data)])[5][2],1)
            self.assertEqual(request(8,[digest,0,1024])[5][3],data)
            self.assertEqual(request(5,[token,0,data])[4],422)
            record=[1,author.get_public_key(),b"n"*16,0,1,None,0,100,100,"track","",[0,0,0,0],[0,None,None,1],[digest,len(data),"application/vnd.google-earth.kml+xml",1]]
            raw=packed(record)
            signed=[raw,author.sign(b"TrailMate.Track.Manifest.v1\0"+raw)]
            with patch('tools.tracks.application.sign_receipt',side_effect=RuntimeError('interrupted before receipt')):
                with self.assertRaises(RuntimeError):
                    request(7,[signed,None])
            self.assertEqual(index.get_manifest(verify_manifest(signed)['object_id'])['signed'],signed)
            # A pending receipt already binds a committed version. A later
            # validation failure must not erase its original request identity.
            stored_file = blobs.files / (digest.hex() + '.kml')
            stored_file.write_bytes(b'corrupted')
            self.assertEqual(request(7,[signed,None])[4],422)
            pending_receipt = index.db.execute('SELECT fingerprint,body FROM publish_receipts WHERE source=? AND request=?',(author.hash,request_id)).fetchone()
            self.assertIsNotNone(pending_receipt)
            self.assertEqual(pending_receipt['body'],packed(None))
            stored_file.write_bytes(data)
            self.assertEqual(request(7,[signed,b'x'*32])[4],422)
            published=request(7,[signed,None])
            self.assertEqual(published[4],200)
            self.assertEqual(request(7,[signed,None]),published)
            self.assertEqual(request(7,[signed,b"x"*32])[4],422)
            index.close()
            index=DirectoryIndex(state,blobs)
            app=TrackApplication(directory,blobs,index)
            self.assertEqual(request(7,[signed,None]),published)
            with self.assertRaises(ValueError):
                TrackApplication(RNS.Identity(),blobs,index)
            receipt=verify_receipt(published[5][0],directory.get_public_key(),app.epoch,request_id,signed)
            object_id=receipt[2]
            with self.assertRaises(ValueError):
                verify_receipt(published[5][0],directory.get_public_key(),app.epoch,b"x"*16,signed)
            damaged=[published[5][0][0],bytes([published[5][0][1][0]^1])+published[5][0][1][1:]]
            with self.assertRaises(ValueError):
                verify_receipt(damaged,directory.get_public_key(),app.epoch,request_id,signed)
            fetched=request(2,[object_id,None])
            self.assertEqual(verify_manifest(fetched[5][0])["record"][9],"track")
            queried=request(1,[[-10000000,-10000000,10000000,10000000],None,None,0,16,None,None])
            self.assertEqual(queried[5][2][0][0],object_id)
            index.close()
            blobs.close()
