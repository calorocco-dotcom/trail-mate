"""Bounded immutable track-package blobs; authenticated source owns uploads."""
import hashlib
import os
from pathlib import Path
import sqlite3
import threading
import time
from tools.tracks.protocol import binary, integer, TRACK_BULK_CHUNK_BYTES


class BlobStore:
    def __init__(self, state, *, max_bytes=64 * 1024 * 1024, max_uploads=16, max_files=1024, clock=time.time):
        if not integer(max_bytes, 1, 0x7fffffffffffffff) or not integer(max_uploads, 1, 256) or not integer(max_files, 2, 4096) or not callable(clock):
            raise ValueError("storage limits")
        self.root = Path(state)
        self.files, self.uploads = self.root / "files", self.root / "uploads"
        self.files.mkdir(parents=True, exist_ok=True)
        self.uploads.mkdir(exist_ok=True)
        self.max_bytes, self.max_uploads, self.clock = max_bytes, max_uploads, clock
        self.max_files = max_files
        self.lock = threading.RLock()
        self.db = sqlite3.connect(self.root / "blobs.sqlite", check_same_thread=False)
        self.db.row_factory = sqlite3.Row
        self.db.execute("PRAGMA synchronous=FULL")
        self.db.execute("CREATE TABLE IF NOT EXISTS uploads (token BLOB PRIMARY KEY, owner BLOB UNIQUE NOT NULL, hash BLOB NOT NULL, bytes INTEGER NOT NULL, offset INTEGER NOT NULL, expires INTEGER NOT NULL)")
        self.db.execute("CREATE TABLE IF NOT EXISTS completed (token BLOB PRIMARY KEY, owner BLOB NOT NULL, hash BLOB NOT NULL, bytes INTEGER NOT NULL, expires INTEGER NOT NULL)")
        self.db.commit()

    def close(self):
        self.db.close()

    def has_file(self, digest, size):
        if not integer(size, 1, 8388608):
            return False
        with self.lock:
            return self._verified(self._final(digest), digest, size)

    def file_status(self, owner, digest, size):
        if not binary(owner, 16) or not integer(size, 1, 8388608):
            raise ValueError("file status binding")
        with self.lock:
            if self.has_file(digest, size):
                return [digest, size, 1, None, size]
            row = self.db.execute("SELECT token,offset,expires FROM uploads WHERE owner=? AND hash=? AND bytes=?", (owner, digest, size)).fetchone()
            if row and row["expires"] > self.clock():
                return [digest, size, 0, row["token"], row["offset"]]
            return [digest, size, 0, None, 0]

    def read_chunk(self, digest, offset, length, *, max_chunk=1024):
        if max_chunk not in (1024, TRACK_BULK_CHUNK_BYTES) or not integer(offset, 0, 8388608) or not integer(length, 1, max_chunk):
            raise ValueError("file range")
        with self.lock:
            path = self._final(digest)
            size = path.stat().st_size
            if not integer(size, 1, 8388608) or offset >= size:
                raise ValueError("file range")
            with path.open("rb") as source:
                source.seek(offset)
                data = source.read(min(length, size - offset))
            return [digest, size, offset, data]

    def read_geometries(self, digest, size):
        from tools.tracks.kml_geometry import read_verified_kml_geometry
        with self.lock, self._final(digest).open("rb") as source:
            return read_verified_kml_geometry(source, digest, size)

    def read_index(self, digest, size):
        from tools.tracks.manifest_extensions import MAX_INDEX_BYTES
        if not binary(digest, 32) or not integer(size, 1, MAX_INDEX_BYTES):
            raise ValueError("attachment index size/hash")
        with self.lock, self._final(digest).open("rb") as source:
            raw = source.read(size + 1)
            if len(raw) != size or hashlib.sha256(raw).digest() != digest:
                raise ValueError("attachment index corrupted")
            return raw

    def package_status(self, signed):
        from tools.tracks.protocol import verify_manifest
        from tools.tracks.attachment_index import match_index
        proof = verify_manifest(signed)
        record = proof["record"]
        with self.lock:
            kml_ready = self.has_file(record[13][0], record[13][1])
            assets = record[15] if record[0] == 2 else None
            if assets is None:
                return [1, proof["revision_hash"], int(kml_ready), 1, int(kml_ready), []]
            if not self.has_file(assets[0], assets[1]):
                return [1, proof["revision_hash"], int(kml_ready), 0, 0, []]
            index = match_index(self.read_index(assets[0], assets[1]), signed)
            rows = []
            complete = kml_ready
            for identifier, digest, size, _, _, _, required in index[3]:
                available = self.has_file(digest, size)
                rows.append([identifier, digest, size, required, int(available)])
                if required and not available:
                    complete = False
            return [1, proof["revision_hash"], int(kml_ready), 1, int(complete), rows]

    def _final(self, digest):
        if not binary(digest, 32):
            raise ValueError("file hash")
        # Retain the historical physical suffix; payloads are opaque bytes.
        # Only read_geometries interprets a hash-verified KML as geometry.
        return self.files / (digest.hex() + ".kml")

    def _verified(self, path, digest, size):
        if not path.is_file() or path.stat().st_size != size:
            return False
        actual = hashlib.sha256()
        with path.open("rb") as source:
            for block in iter(lambda: source.read(65536), b""):
                actual.update(block)
        return actual.digest() == digest

    def begin(self, owner, digest, size):
        if not binary(owner, 16) or not any(owner) or not binary(digest, 32) or not integer(size, 1, 8388608):
            raise ValueError("upload binding")
        with self.lock, self.db:
            final = self._final(digest)
            if final.exists():
                if not self._verified(final, digest, size):
                    raise ValueError("existing file corrupted")
                return {"token": None, "offset": size, "sealed": True}
            existing = self.db.execute("SELECT * FROM uploads WHERE owner=?", (owner,)).fetchone()
            if existing:
                if existing["hash"] != digest or existing["bytes"] != size or existing["expires"] <= self.clock():
                    raise ValueError("active upload conflict")
                return {"token": existing["token"], "offset": existing["offset"], "sealed": False}
            rows = self.db.execute("SELECT token, bytes FROM uploads").fetchall()
            occupied, entries = self._reserved_space(rows)
            if len(rows) >= self.max_uploads or occupied + size > self.max_bytes or entries + len(rows) + 2 > self.max_files:
                raise ValueError("upload capacity")
            token = os.urandom(16)
            path = self.uploads / token.hex()
            created = False
            try:
                with path.open("xb") as output:
                    created = True
                    output.flush()
                    os.fsync(output.fileno())
                # Complete registration before returning a token. In particular,
                # an unsuccessful commit must not accumulate empty temp files.
                with self.db:
                    self.db.execute("INSERT INTO uploads VALUES (?,?,?,?,?,?)", (token, owner, digest, size, 0, int(self.clock()) + 3600))
            except (OSError, sqlite3.Error):
                try:
                    # If rollback itself failed, ownership is uncertain. Keep
                    # the file until the database can prove it unregistered.
                    if created and not self.db.in_transaction and self.db.execute("SELECT 1 FROM uploads WHERE token=?", (token,)).fetchone() is None:
                        path.unlink(missing_ok=True)
                except (OSError, sqlite3.Error):
                    pass
                raise
            return {"token": token, "offset": 0, "sealed": False}

    def _reserved_space(self, rows):
        known = {row["token"].hex(): row["bytes"] for row in rows}
        occupied, entries = sum(known.values()), 0
        for directory in (self.files, self.uploads):
            with os.scandir(directory) as items:
                for item in items:
                    entries += 1
                    if entries > self.max_files or not item.is_file(follow_symlinks=False):
                        raise ValueError("unknown directory entry or file capacity")
                    length = item.stat(follow_symlinks=False).st_size
                    occupied += max(0, length - known[item.name]) if directory == self.uploads and item.name in known else length
                    if occupied > self.max_bytes:
                        raise ValueError("storage capacity")
        return occupied, entries

    def _load(self, owner, token):
        if not binary(owner, 16) or not binary(token, 16):
            raise ValueError("upload identity/token")
        row = self.db.execute("SELECT * FROM uploads WHERE token=? AND owner=?", (token, owner)).fetchone()
        if row is None or row["expires"] <= self.clock():
            raise ValueError("upload unavailable")
        return row

    def append(self, owner, token, offset, content, *, max_chunk=1024):
        if max_chunk not in (1024, TRACK_BULK_CHUNK_BYTES) or type(content) is not bytes or not 0 < len(content) <= max_chunk or not integer(offset, 0, 8388608):
            raise ValueError("upload chunk")
        with self.lock, self.db:
            row = self._load(owner, token)
            if offset > row["offset"] or offset + len(content) > row["bytes"]:
                raise ValueError("upload offset")
            path = self.uploads / token.hex()
            with path.open("r+b") as output:
                if path.stat().st_size < row["offset"]:
                    raise ValueError("durable prefix missing")
                if offset < row["offset"]:
                    output.seek(offset)
                    if offset + len(content) > row["offset"] or output.read(len(content)) != content:
                        raise ValueError("retry chunk conflict")
                    return row["offset"]
                # Bytes beyond the committed offset are from an unacknowledged
                # write; only this authenticated, logged upload may discard them.
                output.truncate(row["offset"])
                output.seek(offset)
                if output.write(content) != len(content):
                    raise OSError("short upload write")
                output.flush()
                os.fsync(output.fileno())
            next_offset = offset + len(content)
            self.db.execute("UPDATE uploads SET offset=? WHERE token=?", (next_offset, token))
            return next_offset

    def finish(self, owner, token):
        if not binary(owner, 16) or not binary(token, 16):
            raise ValueError("upload identity/token")
        with self.lock:
            with self.db:
                self.db.execute("DELETE FROM completed WHERE expires<=?", (int(self.clock()),))
                completed = self.db.execute("SELECT * FROM completed WHERE token=? AND owner=?", (token, owner)).fetchone()
                if completed:
                    if not self._verified(self._final(completed["hash"]), completed["hash"], completed["bytes"]):
                        raise ValueError("completed file corrupted")
                    return {"hash": completed["hash"], "bytes": completed["bytes"], "sealed": True,
                            "cleanup_pending": not self._cleanup_upload(token)}
                if self.db.execute("SELECT count(*) FROM completed").fetchone()[0] >= 4096:
                    raise ValueError("completion receipt capacity")
                row = self._load(owner, token)
                path, final = self.uploads / token.hex(), self._final(row["hash"])
                if row["offset"] != row["bytes"]:
                    raise ValueError("incomplete upload")
                if final.exists():
                    if not self._verified(final, row["hash"], row["bytes"]):
                        raise ValueError("immutable file conflict")
                else:
                    if not self._verified(path, row["hash"], row["bytes"]):
                        raise ValueError("upload hash mismatch")
                    try:
                        os.link(path, final)
                    except FileExistsError:
                        if not self._verified(final, row["hash"], row["bytes"]):
                            raise ValueError("immutable file conflict")
                if not self._verified(final, row["hash"], row["bytes"]):
                    raise ValueError("final file verification")
                self.db.execute("DELETE FROM uploads WHERE token=?", (token,))
                self.db.execute("INSERT INTO completed VALUES (?,?,?,?,?)", (token, owner, row["hash"], row["bytes"], int(self.clock()) + 86400))
                receipt = {"hash": row["hash"], "bytes": row["bytes"], "sealed": True}
            receipt["cleanup_pending"] = not self._cleanup_upload(token)
            return receipt

    def _cleanup_upload(self, token):
        try:
            (self.uploads / token.hex()).unlink(missing_ok=True)
            return True
        except OSError:
            return False

    def retain_replica_progress(self,owner,token):
        # Internal replica flow only: idle expiry slides on NEW durable bytes,
        # not retries. Ordinary browser upload lifetime remains unchanged.
        with self.lock,self.db:
            row=self._load(owner,token)
            expires=max(row['expires'],int(self.clock())+3600)
            self.db.execute('UPDATE uploads SET expires=? WHERE token=? AND owner=?',(expires,token,owner))

    def cancel(self,owner,token):
        if not binary(owner,16) or not binary(token,16):raise ValueError('upload identity/token')
        with self.lock,self.db:
            row=self.db.execute('SELECT 1 FROM uploads WHERE token=? AND owner=?',(token,owner)).fetchone()
            if row is None:return False
            if not self._cleanup_upload(token):raise OSError('temporary upload cleanup failed')
            self.db.execute('DELETE FROM uploads WHERE token=? AND owner=?',(token,owner))
            return True

    def cleanup_expired(self, limit=8):
        if not integer(limit, 1, 64):
            raise ValueError("cleanup limit")
        removed = 0
        with self.lock, self.db:
            rows = self.db.execute("SELECT token FROM uploads WHERE expires<=? ORDER BY expires, token LIMIT ?", (int(self.clock()), limit)).fetchall()
            for row in rows:
                if self._cleanup_upload(row["token"]):
                    self.db.execute("DELETE FROM uploads WHERE token=?", (row["token"],))
                    removed += 1
        return removed
