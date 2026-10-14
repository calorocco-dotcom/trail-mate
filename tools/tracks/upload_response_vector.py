"""Generate public wire responses from the production track application."""
import argparse
import hashlib
from pathlib import Path
import tempfile
import RNS
from tools.tracks.application import TrackApplication
from tools.tracks.blob_store import BlobStore
from tools.tracks.directory_index import DirectoryIndex
from tools.tracks.protocol import packed, unpacked


def generate(output):
    output.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory() as state:
        author,directory=RNS.Identity(),RNS.Identity()
        blobs=BlobStore(state)
        index=DirectoryIndex(state,blobs)
        try:
            app=TrackApplication(directory,blobs,index)
            data=b'<kml/>'+b'\n'*1018
            digest=hashlib.sha256(data).digest()
            def request(operation,body,name):
                raw=app.handle(author.hash,packed([1,0,operation,b'r'*16,8192,body]))
                row=unpacked(raw)
                if row[4]!=200:
                    raise RuntimeError(row)
                (output/name).write_bytes(raw)
                return row[5]
            begun=request(4,[digest,len(data)],'begin.bin')
            for offset in range(0,len(data),256):
                request(5,[begun[0],offset,data[offset:offset+256]],f'ack{offset+256}.bin')
            request(5,[begun[0],768,data[768:]],'ack.bin')
            request(6,[begun[0]],'sealed.bin')
            request(4,[digest,len(data)],'existing.bin')
            (output/'hash.bin').write_bytes(digest)
        finally:
            index.close()
            blobs.close()


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    generate(parser.parse_args().output)
