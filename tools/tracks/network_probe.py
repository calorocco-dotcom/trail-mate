"""Local native LXMF capability probe for the track directory tests."""
import argparse
from pathlib import Path
import queue
import time
import os
import hashlib
import json
import LXMF
import RNS
from tools.tracks.protocol import APP_TYPE, packed, unpacked
from tools.tracks.receipt import verify_receipt


def probe(config,state,delivery_hash,read_info=None):
    rns=RNS.Reticulum(configdir=str(config),loglevel=RNS.LOG_WARNING)
    identity=RNS.Identity()
    router=LXMF.LXMRouter(storagepath=str(state/'lxmf'),enforce_stamps=False,delivery_limit=16,autopeer=False)
    local=router.register_delivery_identity(identity,display_name='Track local probe',stamp_cost=None)
    responses=queue.Queue(maxsize=4)
    request=b'r'*16
    def received(message):
        if message.signature_validated and message.source_hash==delivery_hash and message.fields.get(0xFB)==APP_TYPE:
            raw=message.fields.get(0xFC)
            if type(raw) is bytes:
                try:
                    responses.put_nowait(unpacked(raw))
                except (ValueError,queue.Full):
                    pass
    router.register_delivery_callback(received)
    deadline=time.monotonic()+15
    next_request=0
    try:
        while not RNS.Transport.has_path(delivery_hash) and time.monotonic()<deadline:
            if time.monotonic()>=next_request:
                RNS.Transport.request_path(delivery_hash)
                next_request=time.monotonic()+1
            time.sleep(0.1)
        remote_identity=RNS.Identity.recall(delivery_hash)
        if remote_identity is None:
            raise RuntimeError('directory path unavailable')
        router.announce(local.hash)
        destination=RNS.Destination(remote_identity,RNS.Destination.OUT,RNS.Destination.SINGLE,'lxmf','delivery')
        message=LXMF.LXMessage(destination,local,'Track probe','Track probe',fields={0xFB:APP_TYPE,0xFC:packed([1,0,0,request,8192,[]])},desired_method=LXMF.LXMessage.DIRECT)
        router.handle_outbound(message)
        response=responses.get(timeout=15)
        if response[:5]!=[1,1,0,request,200] or response[5][1]!=APP_TYPE or response[5][2]!=remote_identity.get_public_key():
            raise RuntimeError('capability response binding')
        epoch=response[5][3]
        def query(operation,body):
            print('Track probe sending operation '+str(operation),flush=True)
            request_id=os.urandom(16)
            message=LXMF.LXMessage(destination,local,'Track probe','Track probe',fields={0xFB:APP_TYPE,0xFC:packed([1,0,operation,request_id,8192,body])},desired_method=LXMF.LXMessage.DIRECT)
            router.handle_outbound(message)
            reply=responses.get(timeout=15)
            if reply[:5]!=[1,1,operation,request_id,200]:
                raise RuntimeError('track operation response '+str(reply[:5]))
            print('Track probe received operation '+str(operation),flush=True)
            return reply[5],request_id
        if read_info is not None:
            saved=json.loads(read_info.read_text())
            if saved['directory_public']!=remote_identity.get_public_key().hex() or saved['epoch']!=epoch.hex():
                raise RuntimeError('restarted directory identity binding')
            signed=[bytes.fromhex(item) for item in saved['manifest']]
            receipt=verify_receipt([bytes.fromhex(item) for item in saved['receipt']],remote_identity.get_public_key(),epoch,bytes.fromhex(saved['request']),signed)
            if unpacked(signed[0])[1]==identity.get_public_key():
                raise RuntimeError('reader must have a different author identity')
            fetched,_=query(2,[receipt[2],receipt[3]])
            if fetched[0]!=signed:
                raise RuntimeError('restarted manifest readback')
            downloaded=bytearray()
            while len(downloaded)<receipt[7]:
                part,_=query(8,[receipt[6],len(downloaded),1024])
                if part[:3]!=[receipt[6],receipt[7],len(downloaded)] or not part[3]:
                    raise RuntimeError('restarted download range')
                downloaded.extend(part[3])
            if len(downloaded)!=receipt[7] or hashlib.sha256(downloaded).digest()!=receipt[6]:
                raise RuntimeError('restarted KML hash')
            found,_=query(1,[[1190000000,290000000,1210000000,310000000],None,None,0,16,None,None])
            if len(found[2])!=1 or found[2][0][0]!=receipt[2]:
                raise RuntimeError('restarted viewport query')
            print('Native track fresh-reader restart round trip passed',flush=True)
            return
        data=b'<kml xmlns="http://www.opengis.net/kml/2.2"><Placemark><Point><coordinates>120,30</coordinates></Point></Placemark></kml>'+b'\n'*2048
        digest=hashlib.sha256(data).digest()
        begun,_=query(4,[digest,len(data)])
        token=begun[0]
        for offset in range(0,len(data),1024):
            chunk=data[offset:offset+1024]
            acknowledged,_=query(5,[token,offset,chunk])
            if acknowledged!=[token,offset+len(chunk)]:
                raise RuntimeError('native upload offset')
        query(6,[token])
        record=[1,identity.get_public_key(),os.urandom(16),0,1,None,0,100,100,'Local native track','',[1200000000,300000000,1200000000,300000000],[0,None,None,1],[digest,len(data),'application/vnd.google-earth.kml+xml',1]]
        raw=packed(record)
        signed=[raw,identity.sign(b'TrailMate.Track.Manifest.v1\0'+raw)]
        publication,publication_request=query(7,[signed,None])
        receipt=verify_receipt(publication[0],remote_identity.get_public_key(),epoch,publication_request,signed)
        (state/'publication.json').write_text(json.dumps({'directory_public':remote_identity.get_public_key().hex(),'epoch':epoch.hex(),'manifest':[item.hex() for item in signed],'receipt':[item.hex() for item in publication[0]],'request':publication_request.hex()}))
        fetched,_=query(2,[receipt[2],receipt[3]])
        if fetched[0]!=signed:
            raise RuntimeError('native manifest readback')
        downloaded=bytearray()
        while len(downloaded)<len(data):
            part,_=query(8,[digest,len(downloaded),1024])
            if part[:3]!=[digest,len(data),len(downloaded)] or not part[3]:
                raise RuntimeError('native download range')
            downloaded.extend(part[3])
        if bytes(downloaded)!=data or hashlib.sha256(downloaded).digest()!=digest:
            raise RuntimeError('native KML readback')
        found,_=query(1,[[1190000000,290000000,1210000000,310000000],None,None,0,16,None,None])
        if len(found[2])!=1 or found[2][0][0]!=receipt[2]:
            raise RuntimeError('native viewport query')
        print('Native track LXMF multi-block upload, signed receipt, download hash and viewport query round trip passed',flush=True)
    finally:
        RNS.Reticulum.exit_handler()


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rns-config',required=True,type=Path)
    parser.add_argument('--state',required=True,type=Path)
    parser.add_argument('--delivery',required=True)
    parser.add_argument('--read-info',type=Path)
    args=parser.parse_args()
    args.state.mkdir(parents=True,exist_ok=True)
    probe(args.rns_config,args.state,bytes.fromhex(args.delivery),args.read_info)
