"""Persistent signed track versions. Transport authenticates the publishing source."""
from pathlib import Path
import sqlite3
import hashlib
import threading
import RNS
from tools.tracks.protocol import binary, integer, packed, verify_manifest, verify_successor
from tools.tracks.geography import validate_box, intersects
from tools.tracks.region_budget import RegionQueryBudget
from tools.tracks.query_metadata import validate_metadata_filters, matches_metadata, query_summary


class DirectoryIndex:
    def __init__(self, state, blobs, *, max_versions=10000):
        if not integer(max_versions, 1, 1000000):
            raise ValueError("version capacity")
        self.blobs, self.max_versions = blobs, max_versions
        self.lock = threading.RLock()
        Path(state).mkdir(parents=True, exist_ok=True)
        self.db = sqlite3.connect(Path(state) / "tracks.sqlite", check_same_thread=False)
        self.db.row_factory = sqlite3.Row
        self.db.execute("PRAGMA foreign_keys=ON")
        self.db.execute("PRAGMA synchronous=FULL")
        self.db.executescript("""
            CREATE TABLE IF NOT EXISTS versions (
                hash BLOB PRIMARY KEY, object BLOB NOT NULL, revision INTEGER NOT NULL,
                raw BLOB NOT NULL, signature BLOB NOT NULL);
            CREATE INDEX IF NOT EXISTS history ON versions(object, revision);
            CREATE TABLE IF NOT EXISTS heads (
                object BLOB PRIMARY KEY, hash BLOB NOT NULL REFERENCES versions(hash));
            CREATE TABLE IF NOT EXISTS replication_log (
                sequence INTEGER PRIMARY KEY AUTOINCREMENT,
                object BLOB NOT NULL, hash BLOB UNIQUE NOT NULL REFERENCES versions(hash));
            CREATE TABLE IF NOT EXISTS replication_conflicts (
                object BLOB PRIMARY KEY, proof_a BLOB NOT NULL REFERENCES versions(hash),
                proof_b BLOB NOT NULL REFERENCES versions(hash));
        """)
        # Backfill existing history in original commit order. Ignored inserts
        # must not consume sequence IDs on every service restart.
        with self.db:
            self.db.execute("""INSERT INTO replication_log(object,hash)
                SELECT v.object,v.hash FROM versions v WHERE NOT EXISTS (
                    SELECT 1 FROM replication_log l WHERE l.hash=v.hash) ORDER BY v.rowid""")

    def close(self):
        self.db.close()

    def query(self, box, *, kind=None, author=None, state=0, activity=None, tags=None, metadata=False, limit=16, snapshot=None, cursor=None):
        validate_box(box)
        activity, tags = validate_metadata_filters(activity, [] if tags is None else tags)
        if type(metadata) is not bool or not metadata and (activity is not None or tags):
            raise ValueError("metadata query requires extension")
        if (kind is not None and not integer(kind, 0, 1) or author is not None and not binary(author, 64)
                or state is not None and not integer(state, 0, 1) or not integer(limit, 1, 32)
                or snapshot is not None and not integer(snapshot, 0, 0x7fffffffffffffff)
                or cursor is not None and (type(cursor) is not list or len(cursor) != 2 or not binary(cursor[0], 32) or not binary(cursor[1], 32) or snapshot is None)):
            raise ValueError("query filters/cursor")
        binding = hashlib.sha256(packed(["trailmate.track.query.metadata.v1", box, kind, author, state, activity, tags] if metadata else [box, kind, author, state])).digest()
        if cursor is not None and cursor[1] != binding:
            raise ValueError("query cursor binding")
        with self.lock, self.db:
            self.db.execute("BEGIN")
            current = self.db.execute("SELECT coalesce(max(rowid),0) FROM versions").fetchone()[0]
            if snapshot is not None and snapshot != current:
                raise ValueError("query snapshot changed")
            rows = self.db.execute("SELECT h.object,h.hash,v.raw,v.signature FROM heads h JOIN versions v ON v.hash=h.hash WHERE h.object>? AND NOT EXISTS (SELECT 1 FROM replication_conflicts c WHERE c.object=h.object) ORDER BY h.object LIMIT 257", (cursor[0] if cursor else b"",)).fetchall()
            items, scanned, after = [], 0, None
            for row in rows[:256]:
                verified = verify_manifest([row["raw"], row["signature"]])
                if verified["object_id"] != row["object"] or verified["revision_hash"] != row["hash"]:
                    raise ValueError("stored query version corrupted")
                record = verified["record"]
                scanned += 1
                after = row["object"]
                if (kind is None or record[3] == kind) and (author is None or record[1] == author) and (state is None or record[6] == state) and matches_metadata(record, activity, tags) and intersects(box, record[11]):
                    items.append(query_summary(verified, metadata))
                    if len(items) == limit:
                        break
            return {"snapshot": current, "items": items, "cursor": [after,binding] if scanned < len(rows) else None}

    def query_region(self, catalog, identifier, boundary_version, *, kind=None, author=None, state=0, activity=None, tags=None, metadata=False, limit=16, snapshot=None, cursor=None):
        from tools.tracks.kml_geometry import manifest_geometry_bounds
        work = RegionQueryBudget()
        region = catalog.get(identifier, boundary_version)
        activity, tags = validate_metadata_filters(activity, [] if tags is None else tags)
        if type(metadata) is not bool or not metadata and (activity is not None or tags):
            raise ValueError("metadata query requires extension")
        if (kind is not None and not integer(kind, 0, 1) or author is not None and not binary(author, 64)
                or state is not None and not integer(state, 0, 1) or not integer(limit, 1, 32)
                or snapshot is not None and not integer(snapshot, 0, 0x7fffffffffffffff)
                or cursor is not None and (type(cursor) is not list or len(cursor) != 2 or not binary(cursor[0], 32) or not binary(cursor[1], 32) or snapshot is None)):
            raise ValueError("region query filters/cursor")
        binding = hashlib.sha256(packed(["trailmate.track.region.query.metadata.v1", identifier, boundary_version, "intersects", kind, author, state, activity, tags] if metadata else [identifier, boundary_version, "intersects", kind, author, state])).digest()
        if cursor is not None and cursor[1] != binding:
            raise ValueError("region query cursor binding")
        # Limit file parsing to 16 candidates per page. Empty pages can retain a
        # cursor; clients must continue rather than declaring the region empty.
        with self.lock, self.db:
            self.db.execute("BEGIN")
            current = self.db.execute("SELECT coalesce(max(rowid),0) FROM versions").fetchone()[0]
            if snapshot is not None and snapshot != current:
                raise ValueError("region query snapshot changed")
            rows = self.db.execute("SELECT h.object,h.hash,v.raw,v.signature FROM heads h JOIN versions v ON v.hash=h.hash WHERE h.object>? AND NOT EXISTS (SELECT 1 FROM replication_conflicts c WHERE c.object=h.object) ORDER BY h.object LIMIT 17", (cursor[0] if cursor else b"",)).fetchall()
            items, scanned, after = [], 0, None
            for row in rows[:16]:
                verified = verify_manifest([row["raw"], row["signature"]])
                if verified["object_id"] != row["object"] or verified["revision_hash"] != row["hash"]:
                    raise ValueError("stored region query version corrupted")
                record = verified["record"]
                scanned += 1
                after = row["object"]
                if (kind is not None and record[3] != kind or author is not None and record[1] != author or state is not None and record[6] != state or not matches_metadata(record, activity, tags)):
                    continue
                # A signed bbox is an author assertion. Derive it from the
                # hash-verified file before allowing it to prefilter geometry.
                work.read_file(record[13][1])
                geometries = self.blobs.read_geometries(record[13][0], record[13][1])
                box, points = manifest_geometry_bounds(geometries)
                if box != record[11] or points != record[12][3]:
                    raise ValueError("KML geometry differs from signed manifest")
                work.consume(points)
                hit = intersects(region[5], box) and any(catalog.intersects_geometry(identifier, boundary_version, *geometry, budget=work) for geometry in geometries)
                del geometries
                if hit:
                    items.append(query_summary(verified, metadata))
                    if len(items) == limit:
                        break
            return {"snapshot": current, "items": items, "cursor": [after, binding] if scanned < len(rows) else None}

    def publish(self, source, signed, expected_current):
        if not binary(source, 16) or expected_current is not None and not binary(expected_current, 32):
            raise ValueError("publish source/current hash")
        verified = self._stored_content_manifest(signed)
        record = verified["record"]
        if expected_current != record[5]:
            raise ValueError("request parent differs from signed version")
        with self.lock, self.db:
            self.db.execute("BEGIN IMMEDIATE")
            if self.db.execute("SELECT 1 FROM replication_conflicts WHERE object=?", (verified["object_id"],)).fetchone():
                raise ValueError("track author version conflict")
            head = self.db.execute("SELECT hash FROM heads WHERE object=?", (verified["object_id"],)).fetchone()
            current = head["hash"] if head else None
            existing = self.db.execute("SELECT object,raw,signature FROM versions WHERE hash=?", (verified["revision_hash"],)).fetchone()
            if existing:
                if existing["object"] != verified["object_id"]:
                    raise ValueError("version object mismatch")
                stored = verify_manifest([existing["raw"], existing["signature"]])
                if stored["revision_hash"] != verified["revision_hash"] or stored["object_id"] != verified["object_id"]:
                    raise ValueError("stored version corrupted")
                return {"object": verified["object_id"], "hash": verified["revision_hash"], "revision": record[4], "current": current == verified["revision_hash"]}
            if expected_current != current:
                raise ValueError("current version conflict")
            if head:
                previous = self.db.execute("SELECT raw,signature FROM versions WHERE hash=?", (current,)).fetchone()
                verify_successor([previous["raw"], previous["signature"]], signed)
            elif record[4] != 1:
                raise ValueError("missing parent")
            self._insert_version(verified)
            self.db.execute("INSERT INTO heads VALUES (?,?) ON CONFLICT(object) DO UPDATE SET hash=excluded.hash", (verified["object_id"], verified["revision_hash"]))
            return {"object": verified["object_id"], "hash": verified["revision_hash"], "revision": record[4], "current": True}

    def _stored_content_manifest(self, signed):
        verified = verify_manifest(signed)
        record = verified["record"]
        # The verified manifest signature authorizes the content. A browser can
        # transport device-authored content using its own authenticated LXMF
        # identity; source still binds upload ownership and request deduplication.
        if not self.blobs.has_file(record[13][0], record[13][1]):
            raise ValueError("KML not stored or corrupted")
        if record[0] == 2 and not self.blobs.package_status(signed)[4]:
            raise ValueError("required track package not stored or corrupted")
        from tools.tracks.kml_geometry import manifest_geometry_bounds
        geometries = self.blobs.read_geometries(record[13][0], record[13][1])
        bounds, points = manifest_geometry_bounds(geometries)
        del geometries
        if bounds != record[11] or points != record[12][3]:
            raise ValueError("KML geometry differs from signed manifest")
        return verified

    def _insert_version(self, verified):
        if self.db.execute("SELECT count(*) FROM versions").fetchone()[0] >= self.max_versions:
            raise ValueError("version capacity")
        self.db.execute("INSERT INTO versions VALUES (?,?,?,?,?)", (verified["revision_hash"], verified["object_id"], verified["record"][4], verified["raw"], verified["signature"]))
        self.db.execute("INSERT INTO replication_log(object,hash) VALUES (?,?)", (verified["object_id"], verified["revision_hash"]))

    def import_replica(self, source, signed):
        if not binary(source,16):
            raise ValueError("authenticated replica source required")
        verified=self._stored_content_manifest(signed)
        object_id,revision_hash,record=verified["object_id"],verified["revision_hash"],verified["record"]
        with self.lock,self.db:
            self.db.execute("BEGIN IMMEDIATE")
            existing=self.db.execute("SELECT object,raw,signature FROM versions WHERE hash=?",(revision_hash,)).fetchone()
            if existing:
                checked=verify_manifest([existing["raw"],existing["signature"]])
                if existing["object"]!=object_id or checked["object_id"]!=object_id or checked["revision_hash"]!=revision_hash:
                    raise ValueError("stored version corrupted")
                return {"duplicate":True,"conflict":self.db.execute("SELECT 1 FROM replication_conflicts WHERE object=?",(object_id,)).fetchone() is not None}
            if record[4]>1:
                parent=self.db.execute("SELECT object,raw,signature FROM versions WHERE hash=?",(record[5],)).fetchone()
                if parent is None or parent["object"]!=object_id:
                    raise ValueError("missing replicated parent")
                verify_successor([parent["raw"],parent["signature"]],signed)
            fork=self.db.execute("SELECT hash FROM versions WHERE object=? AND revision=? ORDER BY hash LIMIT 1",(object_id,record[4])).fetchone()
            self._insert_version(verified)
            if fork:
                proofs=sorted([fork["hash"],revision_hash])
                self.db.execute("INSERT OR IGNORE INTO replication_conflicts VALUES (?,?,?)",(object_id,*proofs))
            conflicted=self.db.execute("SELECT 1 FROM replication_conflicts WHERE object=?",(object_id,)).fetchone() is not None
            head=self.db.execute("SELECT h.hash,v.revision FROM heads h JOIN versions v ON v.hash=h.hash WHERE h.object=?",(object_id,)).fetchone()
            if not conflicted and (head is None or head["revision"]<record[4]):
                self.db.execute("INSERT INTO heads VALUES (?,?) ON CONFLICT(object) DO UPDATE SET hash=excluded.hash",(object_id,revision_hash))
            return {"duplicate":False,"conflict":conflicted}

    def replication_page(self, after=0, high=None, limit=16):
        if not integer(after,0,0x7fffffffffffffff) or high is not None and not integer(high,after,0x7fffffffffffffff) or not integer(limit,1,32):
            raise ValueError("replication page bounds")
        with self.lock,self.db:
            self.db.execute("BEGIN")
            current=self.db.execute("SELECT coalesce(max(sequence),0) FROM replication_log").fetchone()[0]
            if high is None:high=current
            if high>current or after>high:
                raise ValueError("replication watermark unavailable")
            rows=self.db.execute("SELECT l.sequence,l.object,l.hash,v.raw,v.signature FROM replication_log l JOIN versions v ON v.hash=l.hash WHERE l.sequence>? AND l.sequence<=? ORDER BY l.sequence LIMIT ?",(after,high,limit+1)).fetchall()
            more=len(rows)>limit
            items=[]
            for row in rows[:limit]:
                checked=verify_manifest([row["raw"],row["signature"]])
                if checked["object_id"]!=row["object"] or checked["revision_hash"]!=row["hash"]:
                    raise ValueError("replication log corrupted")
                record=checked["record"]
                items.append([row["sequence"],row["object"],row["hash"],record[4],record[6],record[5],record[13][0],record[13][1],record[0]])
            return [high,items[-1][0] if more else high,int(more),items]

    def conflict_proof(self, object_id):
        if not binary(object_id,32):
            raise ValueError("track conflict object")
        with self.lock:
            row=self.db.execute("SELECT proof_a,proof_b FROM replication_conflicts WHERE object=?",(object_id,)).fetchone()
            return None if row is None else [object_id,row["proof_a"],row["proof_b"]]

    def get_manifest(self, object_id, revision_hash=None):
        if not binary(object_id, 32) or revision_hash is not None and not binary(revision_hash, 32):
            raise ValueError("track lookup")
        with self.lock:
            head = self.db.execute("SELECT hash FROM heads WHERE object=?", (object_id,)).fetchone()
            if not head:
                raise ValueError("track missing")
            conflict=self.db.execute("SELECT 1 FROM replication_conflicts WHERE object=?",(object_id,)).fetchone() is not None
            if revision_hash is None and conflict:
                raise ValueError("track author version conflict")
            requested = head["hash"] if revision_hash is None else revision_hash
            row = self.db.execute("SELECT raw,signature FROM versions WHERE object=? AND hash=?", (object_id, requested)).fetchone()
            if not row:
                raise ValueError("version missing")
            verified = verify_manifest([row["raw"], row["signature"]])
            if verified["object_id"] != object_id or verified["revision_hash"] != requested:
                raise ValueError("stored version corrupted")
            return {"signed": [row["raw"], row["signature"]], "current": not conflict and requested == head["hash"]}
