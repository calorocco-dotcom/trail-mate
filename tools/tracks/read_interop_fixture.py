"""Public-only stdin/stdout fixture for the actual track directory application."""
import hashlib
import json
from pathlib import Path
import sys
from tempfile import TemporaryDirectory
import LXMF
import RNS
from tools.tracks.application import TrackApplication
from tools.tracks.blob_store import BlobStore
from tools.tracks.directory_index import DirectoryIndex
from tools.tracks.protocol import APP_TYPE, packed, verify_manifest, integer
from tools.tracks.region_catalog import RegionCatalog


def main():
    # Node sends UTF-8 JSON on Windows too; the inherited console code page
    # must not corrupt regional names before they enter the signed response.
    sys.stdin.reconfigure(encoding='utf-8')
    setup = json.loads(sys.stdin.readline())
    count = setup.get('count', 1)
    if not integer(count, 1, 192):
        raise ValueError('fixture record count')
    local = RNS.Identity(create_keys=False)
    if not local.load_public_key(bytes.fromhex(setup['local_public'])):
        raise ValueError('local public identity')
    directory, author = RNS.Identity(), RNS.Identity()
    source = RNS.Destination(directory, RNS.Destination.OUT, RNS.Destination.SINGLE, 'lxmf', 'delivery')
    destination = RNS.Destination(local, RNS.Destination.OUT, RNS.Destination.SINGLE, 'lxmf', 'delivery')
    content = b'<kml xmlns="http://www.opengis.net/kml/2.2"><Placemark><LineString><coordinates>120,30 121,31</coordinates></LineString></Placemark></kml>'
    digest = hashlib.sha256(content).digest()
    record = [1, author.get_public_key(), b'n'*16, 0, 1, None, 0, 100, 100, 'Shared trail', '',
              [1200000000, 300000000, 1210000000, 310000000], [0, None, None, 2],
              [digest, len(content), 'application/vnd.google-earth.kml+xml', 1]]
    if setup.get('metadata') is not None:
        record[0] = 2
        record.extend([setup['metadata'], None])
    raw = packed(record)
    signed = [raw, author.sign(f'TrailMate.Track.Manifest.v{record[0]}\0'.encode()+raw)]
    verified = verify_manifest(signed)
    with TemporaryDirectory(prefix='read-interop-', dir='.codex-build/reticulum-directory') as temporary:
        blobs = BlobStore(Path(temporary)/'blobs')
        index = DirectoryIndex(Path(temporary)/'index', blobs)
        try:
            upload = blobs.begin(author.hash, digest, len(content))
            blobs.append(author.hash, upload['token'], 0, content)
            blobs.finish(author.hash, upload['token'])
            index.publish(author.hash, signed, None)
            for number in range(1, count):
                extra = list(record)
                extra[2], extra[9] = number.to_bytes(16, 'big'), f'Shared trail {number}'
                extra_raw = packed(extra)
                index.publish(author.hash, [extra_raw, author.sign(f'TrailMate.Track.Manifest.v{extra[0]}\0'.encode() + extra_raw)], None)
            regions = RegionCatalog('test',setup['regions']) if setup.get('regions') else None
            application = TrackApplication(directory, blobs, index,regions=regions)
            print(json.dumps({'directory_public':directory.get_public_key().hex(), 'directory_delivery':source.hash.hex(),
                              'local_delivery':destination.hash.hex(), 'id':verified['object_id'].hex(),
                              'revision':verified['revision_hash'].hex(), 'content':content.hex()}), flush=True)
            for line in sys.stdin:
                incoming = json.loads(line)
                response = application.handle(local.hash, bytes.fromhex(incoming['payload']))
                message = LXMF.LXMessage(destination, source, 'Track response', 'Trail Mate Track v1',
                                         fields={0xFB:APP_TYPE,0xFC:response}, desired_method=LXMF.LXMessage.DIRECT)
                message.pack()
                print(json.dumps({'wire':message.packed.hex()}), flush=True)
        finally:
            index.close()
            blobs.db.close()


if __name__ == '__main__':
    main()
