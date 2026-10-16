import {readDeviceDraft} from './device-maintenance.js';
import {readDeviceDraftPage} from './device-draft-catalog.js';
import {saveDeviceDraft} from './device-draft-save.js';
import {readDeviceDirectories} from './device-directories.js';
import {prepareDeviceDraft} from './device-draft-prepare.js';
import {readPublicationJob,stopPublicationJob} from './device-publication-job.js';
import {commitDeviceDraft} from './device-draft-commit.js';
import {OPERATION} from '../../device/src/management-protocol.js';
import {uploadUnsignedDeviceCache,beginDeviceGeocacheSignature,readDeviceGeocacheSignature} from './device-signature.js';
import {uploadDevicePublicationConfirmation} from './device-confirmation.js';
import {beginDevicePublicationConfirmation,readDevicePublicationConfirmation} from './device-confirmation-status.js';

// One reader owns the web maintenance lease for a connected management session.
// Serialize release with reads: an old SESSION_CLOSE must never cancel a new job.
export class DeviceDraftReader {
  #session; #tail=Promise.resolve(); #active; #closed=false; #releaseError;
  constructor(session){this.#session=session;}
  confirmPublication(signed,receipt,author,options={}){
    const content=signed?.map(part=>part.slice()),proof=receipt?Object.fromEntries(Object.entries(receipt).map(([key,value])=>[key,value instanceof Uint8Array?value.slice():value])):receipt;
    const retained={...options,id:options.transferId?.slice(),operations:options.operations?.slice()};
    return this.#run(async(identity,settings)=>{
      const transfer=await uploadDevicePublicationConfirmation(this.#session,content,proof,identity,settings);
      let result=await beginDevicePublicationConfirmation(this.#session,transfer,identity,settings);
      const deadline=performance.now()+90000;
      while(!result.confirmed){
        if(result.phase===2)throw Object.assign(Error('Device confirmation failed'),{code:'CONFIRMATION_FAILED',binding:result.binding});
        if(performance.now()>=deadline)throw Object.assign(Error('Device confirmation pending'),{code:'CONFIRMATION_UNCONFIRMED',binding:result.binding});
        await new Promise(resolve=>setTimeout(resolve,500));
        result=await readDevicePublicationConfirmation(this.#session,result.binding,identity,settings);
      }
      return {...result,transfer};
    },author,retained);
  }
  clearSignatureTransfer(id,author,options={}){
    const key=id?.slice();
    return this.#run(async()=>{
      try{const result=await this.#session.request(OPERATION.TRANSFER_ABORT,[key]);if(!Array.isArray(result)||result.length)throw Error('Invalid upload cleanup reply');}
      // Exact abort can report a retired/missing ID after another completed
      // transfer replaced the journal. Preserve that journal; do not delete or
      // restart anything to retire the old signature input.
      catch(error){if(error.code!==10&&error.code!==4)throw error;}
    },author,options);
  }
  signRecord(source,author,options={}){
    const raw=source?.slice(),retained={...options,id:options.transferId?.slice(),operationId:options.operationId?.slice(),draftId:options.draftId?.slice(),operations:options.operations?.slice()};
    return this.#run(async(identity,settings)=>{
      const transfer=await uploadUnsignedDeviceCache(this.#session,raw,identity,settings);
      const binding=await beginDeviceGeocacheSignature(this.#session,transfer,identity,settings);
      const deadline=performance.now()+90000;
      for(;;){
        const result=await readDeviceGeocacheSignature(this.#session,binding,raw,identity,settings);
        if(result.ready)return {...result,transfer};
        if(result.phase===2)throw Object.assign(Error('Device signature failed'),{code:'SIGNATURE_FAILED',binding});
        if(performance.now()>=deadline)throw Object.assign(Error('Device signature pending'),{code:'SIGNATURE_UNCONFIRMED',binding});
        await new Promise(resolve=>setTimeout(resolve,500));
      }
    },author,retained);
  }
  read(id,author,options={}){
    const key=id?.slice();
    return this.#run((identity,settings)=>readDeviceDraft(this.#session,key,identity,settings),author,options);
  }
  list(author,options={}){
    return this.#run((identity,settings)=>readDeviceDraftPage(this.#session,identity,settings),author,options);
  }
  directories(options={}){
    return this.#run((identity,settings)=>readDeviceDirectories(this.#session,settings),undefined,options);
  }
  job(binding,author,options={}){
    const retained={...binding,operationId:binding?.operationId?.slice(),cacheId:binding?.cacheId?.slice(),revisionHash:binding?.revisionHash?.slice()};
    return this.#run((identity,settings)=>readPublicationJob(this.#session,retained,identity,settings),author,options);
  }
  stop(binding,author,options={}){
    const retained={...binding,operationId:binding?.operationId?.slice(),cacheId:binding?.cacheId?.slice(),revisionHash:binding?.revisionHash?.slice()};
    return this.#run((identity,settings)=>stopPublicationJob(this.#session,retained,identity,settings),author,options);
  }
  commit(prepared,author,options={}){
    const retained={...prepared,prepareId:prepared?.prepareId?.slice(),id:prepared?.id?.slice(),authorKey:prepared?.authorKey?.slice(),
      directory:prepared?.directory?.slice(),directoryEpoch:prepared?.directoryEpoch?.slice(),contentHash:prepared?.contentHash?.slice()};
    return this.#run((identity,settings)=>commitDeviceDraft(this.#session,retained,identity,settings),author,options);
  }
  prepare(snapshot,directory,author,options={}){
    const committed={...snapshot,draft:{...snapshot?.draft,id:snapshot?.draft?.id?.slice()},bytes:snapshot?.bytes?.slice()};
    const target={...directory,delivery:directory?.delivery?.slice(),epoch:directory?.epoch?.slice()};
    const settings={...options,prepareId:options.prepareId?.slice()};
    return this.#run((identity,settings)=>prepareDeviceDraft(this.#session,committed,target,identity,settings),author,settings);
  }
  save(id,fields,expected,author,options={}){
    const key=id?.slice(),values={...fields},settings={...options,transferId:options.transferId?.slice()};
    return this.#run((identity,settings)=>saveDeviceDraft(this.#session,key,values,expected,identity,settings),author,settings);
  }
  #run(operation,author,options){
    if(this.#closed)return Promise.reject(new Error('草稿读取器已关闭'));
    this.#active?.abort(new DOMException('已切换草稿','AbortError'));
    const controller=new AbortController();this.#active=controller;
    const external=options.signal;
    const abort=()=>controller.abort(external.reason);
    if(external?.aborted)abort();else external?.addEventListener('abort',abort,{once:true});
    const identity=author?{...author,publicKey:author.publicKey?.slice()}:author,settings={...options};
    const work=this.#tail.then(async()=>{
      let result,failure;
      try{
        if(this.#releaseError)throw this.#releaseError;
        if(controller.signal.aborted)throw controller.signal.reason;
        result=await operation(identity,{...settings,signal:controller.signal});
      }catch(error){failure=error;}
      // Even malformed/failed reads can leave a device read job in progress.
      // The management transport supplies its own bounded response timeout.
      if(!this.#releaseError){
        try{
          const reply=await this.#session.request(OPERATION.SESSION_CLOSE,[]);
          if(!Array.isArray(reply)||reply.length)throw Error('设备维护关闭响应无效');
        }
        catch(error){this.#releaseError=new Error('设备维护资源释放未确认，请重新连接设备',{cause:error});failure??=this.#releaseError;}
      }
      if(controller.signal.aborted)failure??=controller.signal.reason;
      if(failure)throw failure;
      return result;
    }).finally(()=>{
      external?.removeEventListener('abort',abort);
      if(this.#active===controller)this.#active=undefined;
    });
    this.#tail=work.catch(()=>{});
    return work;
  }
  cancel(reason=new DOMException('草稿读取已取消','AbortError')){
    this.#active?.abort(reason);
    return this.#tail;
  }
  async close(){this.#closed=true;await this.cancel();}
}
