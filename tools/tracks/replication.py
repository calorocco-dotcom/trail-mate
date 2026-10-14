"""Bounded durable pull replication. Only the service loop touches SQLite.

Announce callbacks queue verified public data. File chunks are committed before
peer progress; retries keep exact request IDs. No author key is generated here.
"""
import os
import queue
import time
import msgpack
import RNS
from tools.tracks.protocol import APP_TYPE,binary,integer,packed,unpacked,verify_manifest,verify_successor,text
from tools.tracks.attachment_index import match_index


def validate_inventory(page,after,high):
    if (type(page)is not list or len(page)!=4 or not integer(page[0],after,0x7fffffffffffffff)
            or high is not None and page[0]!=high or not integer(page[1],after,page[0])
            or not integer(page[2],0,1) or type(page[3])is not list or len(page[3])>16):
        raise ValueError('replication inventory envelope')
    last=after
    for row in page[3]:
        if (type(row)is not list or len(row)!=9 or not integer(row[0],last+1,page[0])
                or not binary(row[1],32) or not binary(row[2],32) or not integer(row[3],1,0xffffffff)
                or not integer(row[4],0,1) or row[3]==1 and (row[5]is not None or row[4]!=0)
                or row[3]>1 and (not binary(row[5],32) or not any(row[5]))
                or not binary(row[6],32) or not integer(row[7],1,8388608) or not integer(row[8],1,2)):
            raise ValueError('replication inventory reference')
        last=row[0]
    if page[2] and (not page[3] or page[1]!=last or last>=page[0]) or not page[2] and page[1]!=page[0]:
        raise ValueError('replication inventory continuation')


def match_reference(reference,proof):
    r=proof['record']
    if reference[1:]!=[proof['object_id'],proof['revision_hash'],r[4],r[6],r[5],r[13][0],r[13][1],r[0]]:
        raise ValueError('replication signed reference mismatch')


class TrackReplicaScheduler:
    aspect_filter='trailmate.track.directory'
    receive_path_responses=True
    ROUND_BYTES=256*1024
    POLL=300
    BACKOFF=(60,300,1800,21600)
    MAX_ANCESTORS=64

    def __init__(self,index,delivery,*,clock=time.time,report=lambda *args,**kwargs:None,can_transfer=lambda destination:True):
        self.index,self.db,self.blobs=index,index.db,index.blobs
        self.delivery,self.clock,self.report,self.can_transfer=delivery,clock,report,can_transfer
        self.announces=queue.Queue(maxsize=32)
        self.active=None
        with self.db:
            self.db.execute('CREATE TABLE IF NOT EXISTS track_replica_peers (delivery BLOB PRIMARY KEY,public BLOB NOT NULL,state BLOB NOT NULL,due INTEGER NOT NULL,scheduled INTEGER NOT NULL)')

    def received_announce(self,destination_hash,announced_identity,app_data):
        try:
            data=unpacked(app_data,160)
            if (type(data)is not list or len(data)!=5 or data[:2]!=[1,APP_TYPE] or not binary(data[2],16)
                    or not binary(data[3],16) or not text(data[4],40)
                    or destination_hash!=RNS.Destination.hash_from_name_and_identity(self.aspect_filter,announced_identity)
                    or data[2]!=RNS.Destination.hash_from_name_and_identity('lxmf.delivery',announced_identity)
                    or data[2]==self.delivery):return
            self.announces.put_nowait((data[2],announced_identity.get_public_key(),data[3]))
        except (ValueError,TypeError,AttributeError,msgpack.UnpackException,queue.Full):return

    @staticmethod
    def decode(row):
        return msgpack.unpackb(row['state'],raw=False,max_bin_len=524288,max_array_len=4096,max_map_len=64)

    def save(self,row,state,due=None):
        with self.db:self.db.execute('UPDATE track_replica_peers SET state=?,due=? WHERE delivery=?',(packed(state),int(self.clock()) if due is None else due,row['delivery']))

    def debug(self,event,**fields):
        if os.environ.get('TRAIL_MATE_REPLICATION_DEBUG')=='1':self.report(event,**fields)

    @staticmethod
    def initial(epoch):
        return dict(announced_epoch=epoch,epoch=None,after=0,high=None,page=None,item=0,pending=None,
                    deadline=0,caps_until=0,signed=None,stack=[],files=None,file=0,package=False,
                    round_bytes=0,failures=0,health='Candidate',diagnostic=None)

    def ingest(self,now):
        try:delivery,public,epoch=self.announces.get_nowait()
        except queue.Empty:return
        row=self.db.execute('SELECT * FROM track_replica_peers WHERE delivery=?',(delivery,)).fetchone()
        if row:
            state=self.decode(row)
            if row['public']!=public:return
            if state['announced_epoch']!=epoch:
                # Old content remains immutable; abandon only this peer's
                # unfinished temporary file before selecting a new epoch.
                upload=self.blobs.db.execute('SELECT token FROM uploads WHERE owner=?',(delivery,)).fetchone()
                if upload:self.blobs.cancel(delivery,upload['token'])
                state=self.initial(epoch)
                if self.active==delivery:self.active=None
                self.save(row,state,now)
            return
        with self.db:
            if self.db.execute('SELECT count(*) FROM track_replica_peers').fetchone()[0]>=32:return
            self.db.execute('INSERT INTO track_replica_peers VALUES (?,?,?,?,?)',(delivery,public,packed(self.initial(epoch)),now,0))
        self.report('track_peer_discovered',delivery=delivery.hex())

    def fail(self,row,state,reason,*,quarantine=False,retain=True):
        now=int(self.clock());state['failures']+=1;state['diagnostic']=str(reason)[:96]
        state['health']='Quarantined' if quarantine else 'Backoff';state['deadline']=0
        if not retain:state['pending']=None
        if quarantine:state['caps_until']=0
        delay=21600 if quarantine else self.BACKOFF[min(state['failures']-1,3)]
        self.save(row,state,now+delay)
        if self.active==row['delivery']:self.active=None
        self.report('track_replication_wait',delivery=row['delivery'].hex(),reason=state['diagnostic'],seconds=delay)

    def _files(self,signed):
        record=verify_manifest(signed)['record'];result=[record[13][:2]]
        if record[0]==2 and record[15]is not None:result.append(record[15][:2])
        return result

    def _prepare_signed(self,state,signed):
        proof=verify_manifest(signed);record=proof['record']
        if record[4]>1 and not self.db.execute('SELECT 1 FROM versions WHERE object=? AND hash=?',(proof['object_id'],record[5])).fetchone():
            if len(state['stack'])>=self.MAX_ANCESTORS:raise ValueError('replication parent depth budget')
            state['stack'].append(signed);state['signed']=None;state['files']=None
        else:
            state.update(signed=signed,files=self._files(signed),file=0,package=False)

    def _action(self,row,state):
        if state['caps_until']<=int(self.clock()):return 0,[2]
        if state['page']is None:return 17,[state['after'],state['high'],16]
        refs=state['page'][3]
        if state['item']>=len(refs):
            # All exact versions and files committed before this checkpoint.
            page=state['page'];state.update(after=page[1],high=page[0] if page[2] else None,page=None,item=0,health='Ready',failures=0)
            state['round_bytes']=0
            self.save(row,state,int(self.clock()) if page[2] else int(self.clock())+self.POLL)
            self.report('track_replication_page_committed',delivery=row['delivery'].hex(),sequence=state['after'],more=bool(page[2]))
            return None
        reference=refs[state['item']]
        if state['signed']is None:
            if state['stack']:
                child=verify_manifest(state['stack'][-1]);return 2,[child['object_id'],child['record'][5]]
            existing=self.db.execute('SELECT raw,signature FROM versions WHERE object=? AND hash=?',(reference[1],reference[2])).fetchone()
            if existing:
                signed=[existing['raw'],existing['signature']]
                match_reference(reference,verify_manifest(signed))
                try:self.index._stored_content_manifest(signed)
                except (ValueError,FileNotFoundError):
                    # A stored version is not proof its physical package still
                    # exists. Refill missing files, never advance optimistically.
                    self._prepare_signed(state,signed);self.save(row,state);return None
                state['item']+=1;self.save(row,state);return None
            return 2,[reference[1],reference[2]]
        if state['file']<len(state['files']):
            digest,size=state['files'][state['file']]
            if self.blobs.has_file(digest,size):
                state['file']+=1;self.save(row,state);return None
            allowed=self.can_transfer(row['delivery'])
            if allowed is not True:
                state['health']='WaitingForPath' if allowed is None else 'WaitingForBandwidth'
                self.save(row,state,int(self.clock())+(5 if allowed is None else 60));return None
            upload=self.blobs.begin(row['delivery'],digest,size)
            return 8,[digest,upload['offset'],min(1024,size-upload['offset'])]
        record=verify_manifest(state['signed'])['record']
        if not state['package'] and record[0]==2 and record[15]is not None:return 14,[state['signed']]
        self.index.import_replica(row['delivery'],state['signed'])
        if state['stack']:
            child=state['stack'].pop();self._prepare_signed(state,child)
        else:
            state.update(signed=None,files=None,file=0,package=False);state['item']+=1
        self.save(row,state);return None

    def step(self,send):
        now=int(self.clock());self.ingest(now)
        if self.active is not None:
            row=self.db.execute('SELECT * FROM track_replica_peers WHERE delivery=?',(self.active,)).fetchone()
            state=self.decode(row)
            if state['deadline']>now:return
            self.fail(row,state,'request_timeout')
        row=self.db.execute('SELECT * FROM track_replica_peers WHERE due<=? ORDER BY scheduled,delivery LIMIT 1',(now,)).fetchone()
        if row is None:return
        state=self.decode(row)
        if state['round_bytes']>=self.ROUND_BYTES:
            state['round_bytes']=0;self.save(row,state,now+1);return
        try:
            if state['pending']is None:
                action=self._action(row,state)
                if action is None:return
                operation,body=action
                state['pending']=packed([1,0,operation,os.urandom(16),8192,body])
            state['deadline']=now+120;state['round_bytes']+=len(state['pending'])
            self.save(row,state,now)
            with self.db:self.db.execute('UPDATE track_replica_peers SET scheduled=? WHERE delivery=?',(now,row['delivery']))
            self.active=row['delivery'];request=unpacked(state['pending'])
            self.debug('track_replication_request',delivery=row['delivery'].hex(),operation=request[2],request=request[3].hex())
            send(row['public'],state['pending'])
        except (OSError,ValueError) as error:self.fail(row,state,type(error).__name__+': '+str(error),retain=False)

    def received(self,source,raw):
        row=self.db.execute('SELECT * FROM track_replica_peers WHERE delivery=?',(source,)).fetchone()
        if row is None:return
        state=self.decode(row)
        if state['pending']is None:return
        request=unpacked(state['pending']);response=unpacked(raw)
        self.debug('track_replication_response',delivery=source.hex(),operation=response[2] if type(response)is list and len(response)>2 else None)
        if type(response)is not list or len(response)!=6 or response[:4]!=[1,1,request[2],request[3]]:return
        if response[4]!=200:
            self.fail(row,state,'remote_'+str(response[4]),quarantine=response[4]in(400,413,422,501),retain=False);return
        body=response[5]
        try:
            if request[2]==0:
                if (type(body)is not list or len(body)!=8 or body[:4]!=[1,APP_TYPE,row['public'],state['announced_epoch']]
                        or body[4:7]!=[8388608,1024,32] or type(body[7])is not list
                        or not set([2,8,14,17]).issubset(body[7]) or len(body[7])>32
                        or any(not integer(op,0,255) for op in body[7]) or len(set(body[7]))!=len(body[7])):raise ValueError('replication capability binding')
                state.update(epoch=body[3],caps_until=int(self.clock())+86400)
            elif request[2]==17:
                validate_inventory(body,state['after'],state['high']);state.update(page=body,item=0)
            elif request[2]==2:
                if type(body)is not list or len(body)!=2 or not integer(body[1],0,1):raise ValueError('replication manifest response')
                proof=verify_manifest(body[0])
                if [proof['object_id'],proof['revision_hash']]!=request[5]:raise ValueError('replication manifest binding')
                if state['stack']:verify_successor(body[0],state['stack'][-1])
                else:match_reference(state['page'][3][state['item']],proof)
                self._prepare_signed(state,body[0])
            elif request[2]==8:
                digest,offset,length=request[5];expected_digest,size=state['files'][state['file']]
                if (digest!=expected_digest or type(body)is not list or len(body)!=4 or body[:3]!=[digest,size,offset]
                        or type(body[3])is not bytes or len(body[3])!=length):raise ValueError('replication file range binding')
                upload=self.blobs.begin(source,digest,size)
                if upload['sealed']:
                    state['file']+=1
                elif offset>upload['offset']:
                    # A lost/expired local prefix is recoverable; request a new
                    # range from the actual durable offset on the next step.
                    pass
                else:
                    end=self.blobs.append(source,upload['token'],offset,body[3])
                    if end>upload['offset']:self.blobs.retain_replica_progress(source,upload['token'])
                    if end==size:self.blobs.finish(source,upload['token']);state['file']+=1
            elif request[2]==14:
                proof=verify_manifest(state['signed']);record=proof['record']
                assets=record[15];index=match_index(self.blobs.read_index(assets[0],assets[1]),state['signed'])
                if (type(body)is not list or len(body)!=6 or body[:2]!=[1,proof['revision_hash']] or body[2:5]!=[1,1,1]
                        or type(body[5])is not list or len(body[5])!=len(index[3])):raise ValueError('replication package status')
                seen={digest:size for digest,size in state['files']}
                for entry,available in zip(index[3],body[5]):
                    identifier,digest,size,_,_,_,required=entry
                    if type(available)is not list or len(available)!=5 or available[:4]!=[identifier,digest,size,required] or not integer(available[4],0,1) or required and not available[4]:raise ValueError('replication package entry')
                    if available[4]:
                        if digest in seen and seen[digest]!=size:raise ValueError('replication resource size conflict')
                        if digest not in seen:state['files'].append([digest,size]);seen[digest]=size
                state['package']=True
            state.update(pending=None,deadline=0,health='Replicating',diagnostic=None)
            state['round_bytes']+=len(raw);self.save(row,state)
            if self.active==source:self.active=None
        except (ValueError,OSError,msgpack.UnpackException) as error:
            self.fail(row,state,type(error).__name__+': '+str(error),quarantine=isinstance(error,ValueError),retain=False)
