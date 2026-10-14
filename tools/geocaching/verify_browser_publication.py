"""Read-only independent native check of a synthetic browser publication chain."""
import argparse
import json
from pathlib import Path
import sqlite3
import sys
import RNS
from directory_store import verify_cache


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--database', type=Path, required=True)
    args = parser.parse_args()
    data = json.loads(sys.stdin.buffer.read(65537))
    signed = [[bytes(part) for part in item] for item in data]
    assert len(signed) == 3
    records = [verify_cache(item) for item in signed]
    assert [item.record[3] for item in records] == [1, 2, 3]
    assert [item.record[5] for item in records] == [0, 0, 2]
    assert records[0].record[4] is None
    assert all(item.cache_id == records[0].cache_id for item in records)
    assert records[1].record[4] == records[0].revision_hash
    assert records[2].record[4] == records[1].revision_hash
    identity = RNS.Identity(create_keys=False)
    assert identity.load_public_key(records[0].record[1])
    author_delivery = RNS.Destination.hash_from_name_and_identity('lxmf.delivery', identity)
    with sqlite3.connect(args.database.resolve().as_uri()+'?mode=ro', uri=True) as database:
        rows = database.execute('SELECT raw,signature FROM objects WHERE cache=? ORDER BY revision', (records[0].cache_id,)).fetchall()
        assert [list(row) for row in rows] == signed
        head = database.execute('SELECT hash,conflict FROM heads WHERE cache=?', (records[0].cache_id,)).fetchone()
        assert head == (records[2].revision_hash, 0)
        # At least one real directory publication is correlated to the browser's
        # transport identity. The signed object has the independent USB author.
        sources = [row[0] for row in database.execute('SELECT DISTINCT source FROM requests')]
        assert sources and all(source != author_delivery for source in sources)
    print(json.dumps({'nativeSignatures': True, 'exactStoredVersions': 3,
                      'archiveHead': True, 'transportDiffersFromAuthor': True}))


if __name__ == '__main__':
    main()
