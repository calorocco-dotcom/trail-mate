"""Local public-fixture application/storage bridge; no RNS interfaces opened."""
import json
from pathlib import Path
import sys
import tempfile
import RNS
from tools.tracks.application import TrackApplication
from tools.tracks.blob_store import BlobStore
from tools.tracks.directory_index import DirectoryIndex
from tools.tracks.protocol import unpacked


def main():
    workspace = Path(__file__).resolve().parents[2]
    cache = (workspace / ".codex-build" / "reticulum-directory").resolve()
    if not cache.is_relative_to(workspace):
        raise ValueError("fixture root outside workspace")
    cache.mkdir(parents=True, exist_ok=True)
    directory = RNS.Identity()
    owner = b"s" * 16
    with tempfile.TemporaryDirectory(prefix="package-fixture-", dir=cache) as state:
        blobs = BlobStore(state)
        index = DirectoryIndex(state, blobs)
        app = TrackApplication(directory, blobs, index)
        print(json.dumps({"public": directory.get_public_key().hex(), "epoch": app.epoch.hex()}), flush=True)
        try:
            for line in sys.stdin:
                message = json.loads(line)
                kind = message["kind"]
                if kind == "request":
                    value = {"raw": app.handle(owner, bytes.fromhex(message["raw"])).hex()}
                elif kind == "restart":
                    index.close()
                    blobs.close()
                    blobs = BlobStore(state)
                    index = DirectoryIndex(state, blobs)
                    app = TrackApplication(directory, blobs, index)
                    value = {"restarted": True}
                elif kind == "state":
                    value = {"versions": index.db.execute("SELECT count(*) FROM versions").fetchone()[0],
                             "heads": index.db.execute("SELECT count(*) FROM heads").fetchone()[0]}
                elif kind == "corrupt":
                    digest = bytes.fromhex(message["hash"])
                    path = blobs._final(digest)
                    if not path.is_file():
                        raise ValueError("fixture file missing")
                    path.write_bytes(b"corrupted fixture")
                    value = {"corrupted": True}
                else:
                    raise ValueError("fixture command")
                print(json.dumps({"id": message["id"], **value}), flush=True)
        finally:
            index.close()
            blobs.close()


if __name__ == "__main__":
    main()
