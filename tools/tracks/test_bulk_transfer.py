"""Real native blob storage and CMP1 opt-in transfer boundaries; not RNS proof."""
import hashlib
from contextlib import ExitStack
from pathlib import Path
import tempfile
import unittest
import RNS
from tools.tracks.application import TrackApplication
from tools.tracks.blob_store import BlobStore
from tools.tracks.directory_index import DirectoryIndex
from tools.tracks.protocol import packed, unpacked


class BulkTransferTests(unittest.TestCase):
    def test_maximum_file_resume_restart_ownership_legacy_and_exact_read(self):
        root = Path('.codex-build/hostlink-codec/bulk-transfer-native').resolve()
        root.mkdir(parents=True, exist_ok=True)
        (root/'NOTES.txt').write_text('Native CMP1/SQLite/filesystem 8 MiB bulk transfer acceptance; isolated temporary state removed on terminal test completion. No USB/RNS/public network.', encoding='utf-8')
        with tempfile.TemporaryDirectory(dir=root) as state, ExitStack() as resources:
            self.assertEqual(Path(state).resolve().parent, root)
            identity = RNS.Identity()
            blobs = BlobStore(state)
            index = DirectoryIndex(state, blobs)
            resources.callback(blobs.close)
            resources.callback(index.close)
            app = TrackApplication(identity, blobs, index)
            source, sequence = b's'*16, 0

            def request(op, body, owner=None, budget=8192):
                nonlocal sequence
                sequence += 1
                return unpacked(app.handle(owner or source, packed([1,0,op,sequence.to_bytes(16,'big'),budget,body])))

            data = bytes(range(256))*32768
            digest = hashlib.sha256(data).digest()
            self.assertEqual(request(0, [])[5][7], list(range(9)))
            self.assertIn(19, request(0, [2])[5][7])
            admitted = request(19, [0,digest,len(data)])
            self.assertEqual(admitted[4], 200)
            token, offset, sealed, chunk = admitted[5]
            self.assertEqual((offset,sealed,chunk), (0,0,4096))
            self.assertEqual(request(5,[token,0,data[:4096]])[4],422)
            self.assertEqual(request(19,[1,token,0,data[:4097]])[4],422)
            self.assertEqual(request(19,[1,token,0,data[:4096]],owner=b'x'*16)[4],422)
            for offset in range(0, 2*1024*1024,4096):
                self.assertEqual(request(19,[1,token,offset,data[offset:offset+4096]])[5],[token,offset+4096])
            self.assertEqual(request(19,[1,token,offset,data[offset:offset+4096]])[5],[token,offset+4096])
            bad = bytes([data[offset]^1])+data[offset+1:offset+4096]
            self.assertEqual(request(19,[1,token,offset,bad])[4],422)
            index.close()
            blobs.close()
            blobs = BlobStore(state)
            index = DirectoryIndex(state,blobs)
            resources.callback(blobs.close)
            resources.callback(index.close)
            app = TrackApplication(identity,blobs,index)
            self.assertEqual(request(19,[0,digest,len(data)])[5],[token,2*1024*1024,0,4096])
            for offset in range(2*1024*1024,len(data),4096):
                self.assertEqual(request(19,[1,token,offset,data[offset:offset+4096]])[5],[token,offset+4096])
            self.assertEqual(request(6,[token])[5][:3],[digest,len(data),1])
            self.assertEqual(request(19,[0,digest,len(data)])[5],[None,len(data),1,4096])
            self.assertEqual(request(8,[digest,0,4096])[4],422)
            self.assertEqual(request(19,[2,digest,0,4097])[4],422)
            self.assertEqual(request(19,[2,digest,0,4096],budget=512)[4],413)
            actual = hashlib.sha256()
            for offset in range(0,len(data),4096):
                reply = request(19,[2,digest,offset,4096])
                self.assertEqual(reply[4],200)
                self.assertEqual(reply[5][:3],[digest,len(data),offset])
                self.assertEqual(reply[5][3],data[offset:offset+4096])
                self.assertLessEqual(len(packed(reply)),8192)
                actual.update(reply[5][3])
            self.assertEqual(actual.digest(),digest)
            with self.assertRaisesRegex(ValueError, 'CMP1 type/depth'):
                request(19,[True,token,0,b'a'])
            self.assertEqual(request(19,[3])[4],422)
            index.close()
            blobs.close()
