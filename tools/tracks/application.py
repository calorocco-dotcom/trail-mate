"""Track v1 CMP1 dispatch; native transport authenticates the source identity."""
import os
import time
import hashlib
import sqlite3
from tools.tracks.protocol import APP_TYPE, packed, unpacked, binary, integer
from tools.tracks.protocol import TRACK_BULK_OPERATION, TRACK_BULK_CHUNK_BYTES
from tools.tracks.protocol import verify_manifest
from tools.tracks.receipt import sign_receipt
from tools.tracks.receipt import verify_receipt
from tools.tracks.region_budget import RegionWorkLimit


class TrackApplication:
    def __init__(self, identity, blobs, index, *, regions=None):
        self.identity, self.blobs, self.index = identity, blobs, index
        self.regions = regions
        with index.db:
            index.db.execute("CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY,value BLOB NOT NULL)")
            index.db.execute("INSERT OR IGNORE INTO meta VALUES ('epoch',?)", (os.urandom(16),))
            index.db.execute("INSERT OR IGNORE INTO meta VALUES ('directory_public',?)", (identity.get_public_key(),))
            stored_public = index.db.execute("SELECT value FROM meta WHERE key='directory_public'").fetchone()[0]
            if stored_public != identity.get_public_key():
                raise ValueError("directory identity changed")
            index.db.execute("CREATE TABLE IF NOT EXISTS publish_receipts (source BLOB NOT NULL,request BLOB NOT NULL,fingerprint BLOB NOT NULL,body BLOB NOT NULL,PRIMARY KEY(source,request))")
            self.epoch = index.db.execute("SELECT value FROM meta WHERE key='epoch'").fetchone()[0]
        if not binary(self.epoch, 16):
            raise ValueError("directory epoch corrupted")

    def handle(self, source, raw):
        if not binary(source, 16):
            raise ValueError("authenticated source required")
        row = unpacked(raw, 8192)
        if (type(row) is not list or len(row) != 6 or row[0:2] != [1,0] or not integer(row[2],0,TRACK_BULK_OPERATION)
                or not binary(row[3],16) or not any(row[3]) or not integer(row[4],512,8192) or type(row[5]) is not list):
            raise ValueError("track request envelope")
        _, _, operation, request, budget, body = row
        try:
            result = self._dispatch(source, operation, body, request)
            status = 200
        except RegionWorkLimit as error:
            status, result = 413, [str(error)]
        except (ValueError, FileNotFoundError) as error:
            status, result = 422, [str(error)[:128]]
        except (OSError, sqlite3.Error):
            # A storage failure is neither successful publication nor bad author
            # data. Keep the original request ID so the client can safely retry.
            status, result = 503, ["storage unavailable"]
        response = packed([1,1,operation,request,status,result])
        if len(response) > budget:
            response = packed([1,1,operation,request,413,["response budget",len(response)]])
        else:
            try:
                unpacked(response,8192)
            except ValueError:
                # A short byte payload can still exceed CMP1's collection
                # budget (e.g. many translated names). Fail with a small body.
                response = packed([1,1,operation,request,413,["response element budget",len(response)]])
        return response

    def _dispatch(self, source, operation, body, request):
        if operation == 0 and not body:
            return [1,APP_TYPE,self.identity.get_public_key(),self.epoch,8388608,1024,32,list(range(9))]
        if operation == 0 and body == [2]:
            # Explicit opt-in: old firmware's nine-operation decoder remains
            # compatible with the ordinary capability query above.
            return [1,APP_TYPE,self.identity.get_public_key(),self.epoch,8388608,1024,32,list(range(9))+[14,15,16,17,18,TRACK_BULK_OPERATION]]
        if operation == TRACK_BULK_OPERATION and body and integer(body[0], 0, 2):
            if body[0] == 0 and len(body) == 3:
                result = self.blobs.begin(source, *body[1:])
                return [result["token"],result["offset"],int(result["sealed"]),TRACK_BULK_CHUNK_BYTES]
            if body[0] == 1 and len(body) == 4:
                _,token,offset,content = body
                return [token,self.blobs.append(source,token,offset,content,max_chunk=TRACK_BULK_CHUNK_BYTES)]
            if body[0] == 2 and len(body) == 4:
                return self.blobs.read_chunk(*body[1:],max_chunk=TRACK_BULK_CHUNK_BYTES)
        if operation == 17 and len(body) == 3:
            return self.index.replication_page(*body)
        if operation == 18 and len(body) == 1:
            return self.index.conflict_proof(body[0])
        if operation == 15 and len(body) == 9:
            box,kind,author,state,activity,tags,limit,snapshot,cursor = body
            page = self.index.query(box,kind=kind,author=author,state=state,activity=activity,tags=tags,metadata=True,limit=limit,snapshot=snapshot,cursor=cursor)
            return [page["snapshot"],page["cursor"],page["items"]]
        if operation == 16 and len(body) == 11:
            if self.regions is None:
                raise ValueError("region capability unsupported")
            identifier,version,relation,kind,author,state,activity,tags,limit,snapshot,cursor = body
            if relation != "intersects":
                raise ValueError("region spatial relation unsupported")
            page = self.index.query_region(self.regions,identifier,version,kind=kind,author=author,state=state,activity=activity,tags=tags,metadata=True,limit=limit,snapshot=snapshot,cursor=cursor)
            return [page["snapshot"],page["cursor"],page["items"]]
        if operation == 1 and len(body) == 7:
            box,kind,author,state,limit,snapshot,cursor = body
            page = self.index.query(box,kind=kind,author=author,state=state,limit=limit,snapshot=snapshot,cursor=cursor)
            return [page["snapshot"],page["cursor"],page["items"]]
        if operation == 2 and len(body) == 2:
            result = self.index.get_manifest(*body)
            return [result["signed"],int(result["current"])]
        if operation == 3 and len(body) == 2:
            return self.blobs.file_status(source,*body)
        if operation == 4 and len(body) == 2:
            result = self.blobs.begin(source,*body)
            return [result["token"],result["offset"],int(result["sealed"]),1024]
        if operation == 5 and len(body) == 3:
            token,offset,content = body
            return [token,self.blobs.append(source,token,offset,content)]
        if operation == 6 and len(body) == 1:
            result = self.blobs.finish(source,body[0])
            return [result["hash"],result["bytes"],1,int(result["cleanup_pending"])]
        if operation == 7 and len(body) == 2:
            return self._publish(source,request,body)
        if operation == 8 and len(body) == 3:
            return self.blobs.read_chunk(*body)
        if operation == 14 and len(body) == 1:
            return self.blobs.package_status(body[0])
        if 9 <= operation <= 13:
            if self.regions is None:
                raise ValueError("region capability unsupported")
            if operation == 9 and not body:
                return self.regions.capabilities()
            if operation == 10 and len(body) == 4:
                version,parent,after,limit = body
                return self.regions.children(parent,version=version,after=after,limit=limit)
            if operation == 11 and len(body) == 2:
                identifier,version = body
                node,digest,raw = self.regions.boundary(identifier,version)
                return [version,node,["region-e7-json-v1",digest,len(raw)]]
            if operation == 12 and len(body) == 4:
                return self.regions.boundary_chunk(*body)
            if operation == 13 and len(body) == 9:
                identifier,version,relation,kind,author,state,limit,snapshot,cursor = body
                if relation != "intersects":
                    raise ValueError("region spatial relation unsupported")
                page = self.index.query_region(self.regions,identifier,version,kind=kind,author=author,state=state,limit=limit,snapshot=snapshot,cursor=cursor)
                return [page["snapshot"],page["cursor"],page["items"]]
        raise ValueError("operation body")

    def _publish(self, source, request, body):
        fingerprint=hashlib.sha256(packed(body)).digest()
        with self.index.lock:
            reserved_here=False
            existing=self.index.db.execute("SELECT fingerprint,body FROM publish_receipts WHERE source=? AND request=?",(source,request)).fetchone()
            if existing:
                if existing["fingerprint"]!=fingerprint:
                    raise ValueError("publish request ID conflict")
                if existing["body"]!=packed(None):
                    response=unpacked(existing["body"],2048)
                    if type(response) is not list or len(response)!=2 or not integer(response[1],0,1):
                        raise ValueError("stored receipt response corrupted")
                    verify_receipt(response[0],self.identity.get_public_key(),self.epoch,request,body[0])
                    return response
            else:
                if self.index.db.execute("SELECT count(*) FROM publish_receipts").fetchone()[0]>=4096:
                    raise ValueError("publish receipt capacity")
                with self.index.db:
                    self.index.db.execute("INSERT INTO publish_receipts VALUES (?,?,?,?)",(source,request,fingerprint,packed(None)))
                reserved_here=True
            try:
                result=self.index.publish(source,*body)
            except (ValueError,FileNotFoundError):
                # Index validation failed before committing a version. Only
                # retire this call's new placeholder. An existing placeholder
                # may bind a version committed before receipt generation died.
                if reserved_here:
                    with self.index.db:
                        self.index.db.execute("DELETE FROM publish_receipts WHERE source=? AND request=? AND fingerprint=? AND body=?",(source,request,fingerprint,packed(None)))
                raise
            receipt=sign_receipt(self.identity,self.epoch,request,verify_manifest(body[0]),int(time.time()))
            response=[receipt,int(result["current"])]
            with self.index.db:
                self.index.db.execute("UPDATE publish_receipts SET body=? WHERE source=? AND request=?",(packed(response),source,request))
            return response
