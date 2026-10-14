"""Read-only independent audit of the synthetic maximum-network acceptance state."""
import argparse
import hashlib
import json
from pathlib import Path
import sqlite3
from tools.tracks.protocol import unpacked, verify_manifest, verify_successor
from tools.tracks.attachment_index import match_index
from tools.tracks.receipt import verify_receipt
from tools.tracks.kml_geometry import read_verified_kml_geometry, manifest_geometry_bounds


def audit(root):
    root = root.resolve()
    with sqlite3.connect('file:'+str(root/'tracks.sqlite').replace('\\','/')+'?mode=ro',uri=True) as database:
        rows = database.execute('SELECT raw,signature FROM versions ORDER BY revision').fetchall()
        if len(rows) != 2:
            raise ValueError('expected two signed versions')
        signed = [[row[0],row[1]] for row in rows]
        proofs = [verify_manifest(row) for row in signed]
        verify_successor(*signed)
        if [proof['record'][4] for proof in proofs] != [1,2]:
            raise ValueError('revision chain')
        if database.execute('SELECT hash FROM heads').fetchall() != [(proofs[-1]['revision_hash'],)]:
            raise ValueError('current head')
        files = {}
        for path in (root/'files').iterdir():
            digest = hashlib.sha256()
            with path.open('rb') as source:
                for block in iter(lambda:source.read(65536),b''):
                    digest.update(block)
            if path.name != digest.hexdigest()+'.kml':
                raise ValueError('stored file digest')
            files[digest.digest()] = path.stat().st_size
        record = proofs[-1]['record']
        index = (root/'files'/(record[15][0].hex()+'.kml')).read_bytes()
        entries = match_index(index,signed[-1])[3]
        if len(entries) != 7 or any(files.get(row[1]) != row[2] or row[6] != 1 for row in entries):
            raise ValueError('required package resources')
        if len(files) != 9 or sum(files.values()) != 67108864:
            raise ValueError('maximum physical package bytes')
        with (root/'files'/(record[13][0].hex()+'.kml')).open('rb') as source:
            box, count = manifest_geometry_bounds(read_verified_kml_geometry(source,record[13][0],record[13][1]))
        if count != 100000 or box != record[11]:
            raise ValueError('maximum KML spatial binding')
        metadata = dict(database.execute('SELECT key,value FROM meta').fetchall())
        receipts = database.execute('SELECT request,body FROM publish_receipts').fetchall()
        if len(receipts) != 2:
            raise ValueError('expected two publication receipts')
        for request, raw in receipts:
            response = unpacked(raw,2048)
            fields = unpacked(response[0][0],1024)
            parent = next(row for row,proof in zip(signed,proofs) if proof['revision_hash'] == fields[3])
            verify_receipt(response[0],metadata['directory_public'],metadata['epoch'],request,parent)
    with sqlite3.connect('file:'+str(root/'blobs.sqlite').replace('\\','/')+'?mode=ro',uri=True) as database:
        if database.execute('SELECT count(*) FROM uploads').fetchone()[0] != 0:
            raise ValueError('unfinished uploads')
    return dict(passed=True,readOnly=True,versions=2,receipts=2,points=count,files=len(files),bytes=sum(files.values()),requiredResources=7,unfinishedUploads=0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--state',type=Path,required=True)
    args = parser.parse_args()
    print(json.dumps(audit(args.state)))


if __name__ == '__main__':
    main()
