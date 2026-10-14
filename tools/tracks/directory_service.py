"""Native Reticulum/LXMF track directory. Run with an explicit existing RNS config."""
import argparse
from contextlib import contextmanager
import json
import os
from pathlib import Path
import queue
import signal
import sqlite3
import threading
import time
import LXMF
import RNS
import msgpack
from tools.tracks.application import TrackApplication
from tools.tracks.blob_store import BlobStore
from tools.tracks.directory_index import DirectoryIndex
from tools.tracks.protocol import APP_TYPE, packed, unpacked
from tools.tracks.replication import TrackReplicaScheduler
from tools.tracks.region_catalog import RegionCatalog
from tools.geocaching.reply_queue import advance_reply_queue, reply_queue_wait

TRACK_REPLY_INTERVAL_SECONDS = 0.01


@contextmanager
def exclusive_state(state):
    state.mkdir(parents=True,exist_ok=True,mode=0o700)
    with (state/'service.lock').open('a+b') as handle:
        if handle.tell()==0:
            handle.write(b'\0')
            handle.flush()
        handle.seek(0)
        if os.name=='nt':
            import msvcrt
            msvcrt.locking(handle.fileno(),msvcrt.LK_NBLCK,1)
        else:
            import fcntl
            fcntl.flock(handle.fileno(),fcntl.LOCK_EX|fcntl.LOCK_NB)
        try:
            yield
        finally:
            handle.seek(0)
            if os.name=='nt':
                msvcrt.locking(handle.fileno(),msvcrt.LK_UNLCK,1)
            else:
                fcntl.flock(handle.fileno(),fcntl.LOCK_UN)


def load_identity(state):
    path=state/'identity'
    if path.exists():
        identity=RNS.Identity.from_file(str(path))
        if identity is None:
            raise ValueError('persistent directory identity unreadable')
        return identity
    identity=RNS.Identity()
    descriptor=os.open(path,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600)
    with os.fdopen(descriptor,'wb') as output:
        output.write(identity.get_private_key())
        output.flush()
        os.fsync(output.fileno())
    return identity


class TrackDirectoryService:
    def __init__(self,state,config,name='Trail Mate tracks',*,regions=None,allow_low_bandwidth_replication=False,replica_seeds=()):
        self.identity=load_identity(state)
        self.rns=RNS.Reticulum(configdir=str(config),loglevel=RNS.LOG_WARNING)
        self.router=LXMF.LXMRouter(storagepath=str(state/'lxmf'),enforce_stamps=False,delivery_limit=16,autopeer=False)
        self.delivery=self.router.register_delivery_identity(self.identity,display_name=name,stamp_cost=None)
        self.discovery=RNS.Destination(self.identity,RNS.Destination.IN,RNS.Destination.SINGLE,'trailmate','track','directory')
        self.blobs=BlobStore(state/'track-data')
        self.index=DirectoryIndex(state/'track-data',self.blobs)
        self.application=TrackApplication(self.identity,self.blobs,self.index,regions=regions)
        self.inbound=queue.Queue(maxsize=32)
        self.awaiting_identity=queue.Queue(maxsize=8)
        self.pending={}
        self.outbound=[]
        self.stopped=threading.Event()
        self.name=name
        self.replica_seeds=tuple(replica_seeds)
        self.replication=TrackReplicaScheduler(self.index,self.delivery.hash,
            report=lambda event,**fields:print(json.dumps({'event':event,**fields}),flush=True),
            can_transfer=lambda destination:True if allow_low_bandwidth_replication else self.replica_path_available(destination))
        self.storage_retry={}
        RNS.Transport.register_announce_handler(self.replication)
        self.router.register_delivery_callback(self.received)

    @staticmethod
    def replica_path_available(destination):
        bitrate=RNS.Transport.next_hop_interface_bitrate(destination)
        if bitrate is None:
            # Persisted capabilities do not restore a live delivery path after
            # restart. Solicit the signed path before testing its bandwidth.
            RNS.Transport.request_path(destination)
            return None
        return bitrate>=100000

    def received(self,message):
        if message.destination_hash!=self.delivery.hash or message.fields.get(0xFB)!=APP_TYPE:
            return
        raw=message.fields.get(0xFC)
        if type(raw) is not bytes or not 0<len(raw)<=8192:
            return
        if not message.signature_validated:
            if message.unverified_reason==LXMF.LXMessage.SOURCE_UNKNOWN and type(message.packed) is bytes and len(message.packed)<=16000:
                try:
                    self.awaiting_identity.put_nowait((message.hash,message.source_hash,message.packed))
                except queue.Full:
                    pass
            return
        source=message.get_source()
        if source is None or source.identity is None or message.source_hash!=RNS.Destination.hash_from_name_and_identity('lxmf.delivery',source.identity):
            return
        try:
            self.inbound.put_nowait((source.identity.get_public_key(),raw))
        except queue.Full:
            pass

    def send(self,public,raw):
        identity=RNS.Identity(create_keys=False)
        if not identity.load_public_key(public):
            raise ValueError('authenticated source key')
        destination=RNS.Destination(identity,RNS.Destination.OUT,RNS.Destination.SINGLE,'lxmf','delivery')
        message=LXMF.LXMessage(destination,self.delivery,'Track response','Trail Mate Track v1',
                                fields={0xFB:APP_TYPE,0xFC:raw},desired_method=LXMF.LXMessage.DIRECT)
        self.router.handle_outbound(message)
        self.outbound.append(message)

    def attempt_storage(self,stage,work,*,delay=30):
        if stage not in ('replication','cleanup'):
            raise ValueError('storage retry stage')
        now=time.monotonic()
        if self.storage_retry.get(stage,0)>now:
            return False
        try:
            work()
        except (OSError,sqlite3.Error) as error:
            self.storage_retry[stage]=now+delay
            if stage=='replication':
                # Durable pending requests remain intact. Releasing only the
                # in-memory active slot permits the same ID/range to be retried.
                self.replication.active=None
            print(json.dumps({'event':'track_storage_wait','stage':stage,
                              'error':type(error).__name__,
                              'sqliteCode':getattr(error,'sqlite_errorcode',None),
                              'seconds':delay}),flush=True)
            return False
        self.storage_retry.pop(stage,None)
        return True

    def run(self,seconds=None):
        started=time.monotonic()
        next_announce=started
        next_cleanup=started
        next_path=started
        next_outbound=started
        next_seed_path=started
        print(json.dumps({'event':'ready','delivery':self.delivery.hash.hex(),'discovery':self.discovery.hash.hex(),'epoch':self.application.epoch.hex()}),flush=True)
        try:
            while not self.stopped.is_set() and (seconds is None or time.monotonic()-started<seconds):
                now=time.monotonic()
                self.outbound[:]=[item for item in self.outbound if item.state not in (LXMF.LXMessage.DELIVERED,LXMF.LXMessage.FAILED,LXMF.LXMessage.REJECTED,LXMF.LXMessage.CANCELLED)]
                next_outbound=advance_reply_queue(self.router,self.outbound,now,next_outbound,interval=TRACK_REPLY_INTERVAL_SECONDS)
                try:
                    key,source,raw=self.awaiting_identity.get_nowait()
                    if len(self.pending)<8:
                        self.pending.setdefault(key,(source,raw,now+60))
                except queue.Empty:
                    pass
                for key,(source,raw,expires) in list(self.pending.items()):
                    if now>=expires:
                        del self.pending[key]
                    elif RNS.Identity.recall(source) is not None:
                        del self.pending[key]
                        try:
                            self.received(LXMF.LXMessage.unpack_from_bytes(raw))
                        except (ValueError,TypeError,msgpack.UnpackException):
                            pass
                    elif now>=next_path:
                        RNS.Transport.request_path(source)
                        next_path=now+1
                if now>=next_announce:
                    self.router.announce(self.delivery.hash)
                    self.discovery.set_default_app_data(packed([1,APP_TYPE,self.delivery.hash,self.application.epoch,self.name]))
                    self.discovery.announce()
                    next_announce=now+21600
                if now>=next_cleanup:
                    self.attempt_storage('cleanup',self.blobs.cleanup_expired,delay=60)
                    next_cleanup=now+60
                if len(self.outbound)>=32:
                    self.stopped.wait(0.1)
                    continue
                if self.replica_seeds and now>=next_seed_path:
                    for destination in self.replica_seeds:RNS.Transport.request_path(destination)
                    next_seed_path=now+60
                self.attempt_storage('replication',lambda:self.replication.step(self.send))
                try:
                    public,raw=self.inbound.get(timeout=reply_queue_wait(self.outbound,time.monotonic(),next_outbound))
                except queue.Empty:
                    continue
                identity=RNS.Identity(create_keys=False)
                identity.load_public_key(public)
                try:
                    envelope=unpacked(raw)
                    if type(envelope)is list and len(envelope)==6 and envelope[1]==1:
                        self.attempt_storage('replication',lambda:self.replication.received(RNS.Destination.hash_from_name_and_identity('lxmf.delivery',identity),raw))
                    else:self.send(public,self.application.handle(identity.hash,raw))
                except (ValueError,TypeError,OSError,msgpack.UnpackException):
                    pass
        finally:
            self.stopped.set()
            self.index.close()
            self.blobs.close()
            RNS.Reticulum.exit_handler()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--state',required=True,type=Path)
    parser.add_argument('--rns-config',required=True,type=Path)
    parser.add_argument('--name',default='Trail Mate tracks')
    parser.add_argument('--run-seconds',type=float)
    parser.add_argument('--regions-file',type=Path)
    parser.add_argument('--regions-namespace')
    parser.add_argument('--allow-low-bandwidth-replication',action='store_true',help='Operator opt-in to automatic file copies on slow or unknown paths')
    parser.add_argument('--replica-seed',action='append',help='Signed track discovery hint; overrides TRAIL_MATE_TRACK_REPLICA_SEEDS, at most three 32-hex addresses')
    args=parser.parse_args()
    if args.replica_seed is None:args.replica_seed=[item.strip() for item in os.environ.get('TRAIL_MATE_TRACK_REPLICA_SEEDS','').split(',') if item.strip()]
    if not (args.rns_config/'config').is_file():
        parser.error('explicit RNS configuration must already exist')
    if args.run_seconds is not None and not 0<args.run_seconds<=86400:
        parser.error('run-seconds must be within 0..86400')
    if not args.name.strip() or len(args.name.encode('utf-8'))>40:
        parser.error('name must contain 1..40 UTF-8 bytes')
    if (args.regions_file is None) != (args.regions_namespace is None):
        parser.error('regions-file and regions-namespace must be supplied together')
    if len(args.replica_seed)>3 or any(len(seed)!=32 or any(c not in '0123456789abcdefABCDEF' for c in seed) for seed in args.replica_seed):
        parser.error('replica-seed requires at most three 32-hex discovery addresses')
    regions = RegionCatalog.from_file(args.regions_namespace,args.regions_file) if args.regions_file else None
    with exclusive_state(args.state):
        service=TrackDirectoryService(args.state,args.rns_config,args.name,regions=regions,allow_low_bandwidth_replication=args.allow_low_bandwidth_replication,replica_seeds=tuple(bytes.fromhex(seed) for seed in args.replica_seed))
        signal.signal(signal.SIGINT,lambda *_:service.stopped.set())
        signal.signal(signal.SIGTERM,lambda *_:service.stopped.set())
        service.run(args.run_seconds)


if __name__=='__main__':
    main()
