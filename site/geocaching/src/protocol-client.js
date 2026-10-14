import {Destination, DestType, Identity, Resource, Reticulum, toHex} from '@reticulum/core';
import {WebSocketClientInterface} from '@reticulum/core/src/interfaces/websocket.js';
import {LXMessage, LXMRouter} from '@reticulum/lxmf';
import {APP_TYPE, bytes, decode, encode, equal, integer, validText, validateSummary, verifySigned, viewportBoxes} from './protocol.js';
import {compressionProvider, MAX_LXMF_BYTES} from './compression.js';
import {countryContains} from './country-boundaries.js';
import {createLiveRefresh} from './live-refresh.js';
import {publishSignedCache} from './publish-signed.js';
import {readDirectoryCurrent} from './directory-current.js';
import {readDirectoryPublicationReceipt} from './directory-receipt.js';
import {rememberQuerySnapshot,releaseQuerySnapshots} from './query-snapshots.js';

// These limits apply to the dedicated worker's stack instance, before links
// are accepted. Large LXMF messages still use normal RNS Resource transport.
Resource.DEFAULT_MAX_SIZE = MAX_LXMF_BYTES;
Resource.DEFAULT_MAX_TOTAL_SIZE = MAX_LXMF_BYTES;
Resource.DEFAULT_MAX_PARTS = 128;
Resource.DEFAULT_MAX_SEGMENTS = 1;

class BrowserLXMRouter extends LXMRouter {
  observedLinks = new WeakSet();

  async recoverDirectLink(destinationHash) {
    // A TCP upstream can fail while the browser's WSS interface stays online.
    // The native transport may already have a new path, but a cached LXMF link
    // still belongs to the old path. Retrying on that link cannot recover it.
    const key = toHex(destinationHash), link = this.directLinks.get(key);
    this.directLinks.delete(key);
    if (link) await link.teardown().catch(() => {});
    this.rns.transport.routingTable.expireRoute(destinationHash);
    await this.rns.transport.requestPathAuto(destinationHash);
  }

  _attachLinkMessageListeners(link) {
    // The upstream router calls this on every send, including reused links.
    // Keep one listener pair per link so browsing doesn't accumulate handlers.
    if (this.observedLinks.has(link)) return;
    this.observedLinks.add(link);
    // @reticulum/lxmf 0.9.0 injects bz2 on accepted links and on outgoing
    // large messages, but not on a short request's outbound reply link.
    link.bz2 = compressionProvider;
    link.maxResourceSize = MAX_LXMF_BYTES;
    super._attachLinkMessageListeners(link);
  }
}

export class DirectoryClient {
  publicationReceipt(directoryKey,signed,authorPublicKey){return readDirectoryPublicationReceipt(this,directoryKey,signed,authorPublicKey);}
  current(directoryKey,cacheId){return readDirectoryCurrent(this,directoryKey,cacheId);}
  async verifiedDirectories(){
    const rows=[];
    for(const entry of this.directories.values()){
      if(!entry.ready)continue;
      const publicKey=await entry.identity.getPublicKey();
      if(this.closed||this.directories.get(entry.key)!==entry||!entry.ready)continue;
      rows.push({key:entry.key,delivery:entry.destination.slice(),publicKey:publicKey.slice(),epoch:entry.epoch.slice(),name:entry.name,capabilitiesVerified:true,receiptSupported:entry.receiptSupported===true});
    }
    return rows;
  }
  publish(signed,{directoryKey,authorPublicKey}={}){
    return publishSignedCache(this,directoryKey,signed,authorPublicKey);
  }
  constructor(notify = () => {}, appType = APP_TYPE) {
    if(![APP_TYPE,'trailmate.track.v1'].includes(appType))throw Error('Unsupported browser directory protocol');
    this.appType=appType;
    this.notify = notify;
    this.directories = new Map();
    this.rows = new Map();
    this.pages = [];
    this.querySnapshots = new Map();
    this.tail = Promise.resolve();
    this.generation = 0;
    this.pending = null;
    this.closed = false;
  }

  async connect(url, privateKey = null, discoverySeeds = []) {
    if (!Array.isArray(discoverySeeds) || discoverySeeds.length > 3 ||
        discoverySeeds.some(seed => typeof seed !== 'string' || !/^[0-9a-f]{32}$/i.test(seed))) throw Error('Invalid directory discovery seeds');
    const endpoint = new URL(url);
    if (endpoint.username || endpoint.password || endpoint.hash ||
        !(endpoint.protocol === 'wss:' || (endpoint.protocol === 'ws:' &&
          ['localhost', '127.0.0.1', '[::1]'].includes(endpoint.hostname)))) throw Error('Use a WSS endpoint');
    this.rns = new Reticulum({compressionProvider, logLevel: typeof __GEOCACHING_LOG_LEVEL__ === 'undefined' ? 'ERROR' : __GEOCACHING_LOG_LEVEL__});
    this.identity = privateKey ? await Identity.fromPrivateKey(privateKey) : await Identity.generate();
    this.router = new BrowserLXMRouter(this.identity, this.rns);
    await this.router.init();
    this.router.addEventListener('message', event => this.receive(event.detail.message).catch(() => {}));
    this.rns.transport.addEventListener('announce', event => this.discover(event.detail).catch(() => {}));
    // A long-lived map must recover after a NAS restart or prolonged outage.
    // The interface owns one cancellable retry loop; keep its rate bounded.
    this.interface = new WebSocketClientInterface({url: endpoint.href, framing: 'raw', maxReconnectTries: null, reconnectWait: 15});
    this.discoveryRefresh=createLiveRefresh({isVisible:()=>true,
      canRefresh:()=>!this.closed && this.interface.online && !this.pending,
      refresh:()=>this.enqueue(async()=>{
        if (this.closed || !this.interface.online || [...this.directories.values()].some(entry=>entry.ready)) return;
        for (const seed of discoverySeeds) {
          await this.rns.transport.requestPathAuto(Uint8Array.from(seed.match(/../g),pair=>parseInt(pair,16)));
        }
      })});
    this.interface.addEventListener('disconnected', () => {
      this.discoveryRefresh.setReady(false);
      this.generation++;
      this.pending?.reject(Error('Map connection interrupted'));
      this.notify({type: 'status', state: 'disconnected'});
    });
    this.interface.addEventListener('connected', () => {
      this.discoveryRefresh.setReady(discoverySeeds.length>0);
      this.notify({type: 'status', state: 'discovering'});
      this.enqueue(async () => {
        await this.router.announce('Trail Mate web visitor');
        // Drop failed reconnect handshakes before soliciting new announces;
        // otherwise discover() suppresses their responses as known peers.
        await this.revalidateDirectories();
        // Operator-owned discovery hints only: discover() still verifies the
        // signed announcement and binds the service to its delivery identity.
        for (const seed of discoverySeeds) {
          await this.rns.transport.requestPathAuto(Uint8Array.from(seed.match(/../g), pair => parseInt(pair, 16)));
        }
      }).catch(() => {});
    });
    // Bind before the first dial. A failed first dial can reconnect later;
    // attaching only after connect() resolves leaves that interface orphaned.
    this.rns.addInterface(this.interface, true);
    await this.interface.connect();
  }

  enqueue(action) {
    const task = this.tail.then(() => { if (this.closed) throw Error('Connection closed'); return action(); });
    this.tail = task.catch(() => {});
    return task;
  }

  async revalidateDirectories() {
    // A service may announce only every six hours. Revalidate known peers
    // immediately after reconnect, and let failed peers be discovered again.
    for (const entry of [...this.directories.values()].filter(d => d.ready).slice(0, 3)) {
      entry.ready = false;
      try {
        await this.router.recoverDirectLink(entry.destination);
        await this.checkDirectory(entry);
      } catch (error) {
        if (this.directories.get(entry.key) === entry) this.directories.delete(entry.key);
        this.notify({type: 'source-error', name: entry.name, message: error.message});
      }
    }
  }

  async discover({destinationHash, identity, appData, packet}) {
    if (this.closed || !(appData instanceof Uint8Array) || appData.length > 96) return;
    const service = await Destination.OUT('trailmate.geocache.directory', DestType.SINGLE, identity, this.rns);
    if (!equal(service.destinationHash, destinationHash)) return;
    const data = decode(appData, 96);
    if (!Array.isArray(data) || data.length !== 5 || data[0] !== 1 || !bytes(data[1], 16) ||
        !bytes(data[2], 16) || !validText(data[4], 40, false, true) ||
        !(integer(data[3], 0, Number.MAX_SAFE_INTEGER) || (typeof data[3] === 'bigint' && data[3] >= 0n && data[3] <= 0xffffffffffffffffn))) return;
    const delivery = await Destination.OUT('lxmf.delivery', DestType.SINGLE, identity, this.rns);
    if (!equal(data[1], delivery.destinationHash)) return;
    const key = toHex(data[1]);
    if (this.directories.has(key) || this.directories.size >= 32) return;
    const entry = {key, destination: data[1], identity, epoch:data[2].slice(), name: data[4], ready: false};
    this.directories.set(key, entry);
    try {
      await this.rns.transport.rememberIdentity(await packet.getHash(), data[1], await identity.getPublicKey(), null);
      await this.enqueue(() => this.checkDirectory(entry));
    } catch (error) {
      // A failed first handshake must not permanently suppress later announces.
      if (this.directories.get(key) === entry && !entry.ready) this.directories.delete(key);
      this.notify({type: 'source-error', name: entry.name, message: error.message});
    }
  }

  async checkDirectory(entry) {
    const caps = await this.request(entry, 0, []);
    const compatible=value=>Array.isArray(value)&&value.length===12&&Array.isArray(value[0])&&value[0].includes(1)&&
      Array.isArray(value[1])&&value[1].includes(1)&&Array.isArray(value[2])&&value[2].length<=32&&
      value[2].every(op=>integer(op,0,255))&&new Set(value[2]).size===value[2].length&&
      [0,1,2,3,4].every(op=>value[2].includes(op))&&value[3]===8192&&value[4]===4096&&
      integer(value[5],1,64)&&value[8]===2&&value[10]===1;
    if(!compatible(caps))throw Error('Directory is not compatible');
    entry.receiptSupported = caps[2].includes(5);
    entry.queryReleaseSupported = false;
    // Keep the legacy capability response unchanged. Old services reject this
    // optional selector; new services explicitly advertise query lease release.
    let extended;
    try{extended=await this.request(entry,0,[2]);}
    catch(error){if(error.status!==400||error.reason!=='malformed_request')throw error;}
    if(extended!==undefined){
      if(!compatible(extended)||caps[2].some(op=>!extended[2].includes(op))||
         [0,1,3,4,5,6,7,8,9,10,11].some(index=>!equal(encode(caps[index]),encode(extended[index]))))throw Error('Directory is not compatible');
      entry.queryReleaseSupported=extended[2].includes(6);
    }
    if(this.closed||this.directories.get(entry.key)!==entry)throw Error('Directory connection changed');
    entry.ready = true;
    this.discoveryRefresh?.setReady(false);
    entry.limit = Math.min(20, caps[5]);
    this.notify({type: 'directory', name: entry.name, key: entry.key, count: [...this.directories.values()].filter(d => d.ready).length});
  }

  async receive(message) {
    const pending = this.pending;
    if (!pending || !equal(message.sourceHash, pending.directory.destination) ||
        !equal(message.destinationHash, this.router.deliveryDest.destinationHash) ||
        !await message.verifySignature(pending.directory.identity)) return;
    const field = key => message.fields instanceof Map ? message.fields.get(key) : message.fields[key];
    if (field(0xfb) !== this.appType) return;
    const response = decode(field(0xfc));
    if (!Array.isArray(response) || response.length !== 6 || response[0] !== 1 || response[1] !== 1 ||
        response[2] !== pending.operation || !equal(response[3], pending.id)) return;
    if (this.pending !== pending) return;
    if (response[4] === 200) pending.resolve(response[5]);
    else if (integer(response[4], 400, 599) && Array.isArray(response[5])) {
      const reason=String(response[5][0]).slice(0,60);
      pending.reject(Object.assign(Error(`Directory ${response[4]}: ${reason}`),{code:'DIRECTORY_REJECTED',status:response[4],reason}));
    }
  }

  async request(directory, operation, body, {requestId} = {}) {
    if (this.pending) throw Error('A directory request is already active');
    if(requestId!==undefined&&(!bytes(requestId,16)||!requestId.some(Boolean)))throw Error('Invalid directory request ID');
    const id = requestId?.slice()??crypto.getRandomValues(new Uint8Array(16));
    const raw = encode([1, 0, operation, id, 8192, body]);
    let timer;
    const response = new Promise((resolve, reject) => {
      this.pending = {directory, operation, id, resolve, reject};
      timer = setTimeout(() => reject(Error('Directory response timed out')), 45000);
    });
    // Attach rejection handling before asynchronous link setup can finish.
    response.catch(() => {});
    const send = () => this.router.send(new LXMessage({sourceHash: this.router.deliveryDest.destinationHash,
      destinationHash: directory.destination, title: 'Trail Mate Geocache v1', content: 'Geocache request',
      fields: new Map([[0xfb, this.appType], [0xfc, raw]])}), this.identity, null);
    let retry, attempts = 0, sending = false;
    const attempt = async () => {
      if (this.pending?.id !== id || sending || attempts >= 3) return;
      sending = true;
      attempts++;
      let delay = 20000;
      try {
        if (attempts > 1) await this.router.recoverDirectLink(directory.destination);
        if (this.pending?.id === id) await send();
      } catch (error) {
        // A transit node can answer from its old path cache while learning a
        // surviving uplink. Allow a fresh link attempt after that failed
        // handshake, within the same deadline and using the same request ID.
        delay = 1000;
        if (this.pending?.id === id && attempts >= 3) this.pending.reject(error);
      } finally {
        sending = false;
        if (this.pending?.id === id && attempts < 3) retry = setTimeout(attempt, delay);
      }
    };
    try {
      void attempt();
      return await response;
    } finally {
      clearTimeout(timer); clearTimeout(retry);
      this.pending = null;
    }
  }

  query(bounds, stateMask = 3, region = null, queryToken = 0, {refresh = false} = {}) {
    if (!integer(stateMask, 1, 7)) return Promise.reject(Error('Invalid state filter'));
    const generation = ++this.generation;
    return this.enqueue(async () => {
      if(generation!==this.generation)return;
      const previousPages=refresh ? this.queryPages || [] : [];
      // Replacement discards old continuations; return their capacity before
      // admitting a new snapshot. Current "more" never calls this operation.
      await releaseQuerySnapshots(this);
      if(generation!==this.generation)return;
      const nextRows=new Map();
      if (!refresh) this.rows=nextRows;
      this.pages = [];
      this.queryToken=queryToken;
      const sources = [...this.directories.values()].filter(d => d.ready).slice(0, 3);
      if (!sources.length) throw Error('No verified public directory is available');
      const queryPages=[];
      let succeeded = 0;
      for (const directory of sources) for (const bbox of viewportBoxes(bounds)) {
        if (generation !== this.generation) return;
        const page = {directory, bbox, stateMask, region, cursor: null, snapshot: null, last: null,
          rows:nextRows, background:refresh, reads:0};
        queryPages.push(page);
        const previous=previousPages.find(item=>item.directory.key===directory.key &&
          item.bbox.every((value,index)=>value===bbox[index]));
        try {
          await this.readRegionPage(page, generation);
          // Refresh the number of pages the visitor already opened. Preserve
          // pagination without automatically crawling the whole directory.
          while (refresh && generation===this.generation && page.cursor && nextRows.size<500 &&
            page.reads<(previous?.reads || 1)) {
            const queued=this.pages.indexOf(page);
            if(queued>=0)this.pages.splice(queued,1);
            await this.readPage(page,generation);
          }
          succeeded++;
        }
        catch (error) { this.notify({type: 'source-error', name: directory.name, message: error.message}); }
      }
      if (!succeeded) throw Error('All directory queries failed. Results are not live.');
      if (generation!==this.generation) return;
      this.rows=nextRows; this.queryPages=queryPages;
      for (const page of queryPages) page.background=false;
      this.emitRows(generation);
    });
  }

  async readPage(page, generation) {
    const result = await this.request(page.directory, 2, [page.bbox, page.stateMask, null, page.directory.limit, page.cursor]);
    if (!Array.isArray(result) || result.length !== 4 || !bytes(result[0], 16) || !Array.isArray(result[1]) ||
        result[1].length > page.directory.limit || !(result[2] === null || (result[2] instanceof Uint8Array && result[2].length >= 1 && result[2].length <= 64)) ||
        !integer(result[3], 0, 604800) || (result[2] !== null && !result[1].length) ||
        (page.snapshot && !equal(page.snapshot, result[0]))) throw Error('Invalid query page');
    rememberQuerySnapshot(this,page,result);
    // Even a response superseded while in flight has an owned lease to release.
    if (generation !== this.generation) return;
    const rows=page.rows || this.rows;
    for (const summary of result[1]) {
      validateSummary(summary);
      const id = toHex(summary[0]);
      if (page.last && id <= page.last) throw Error('Directory page order changed');
      page.last = id;
      if (page.region && !countryContains(page.region,summary[4]/1e7,summary[5]/1e7)) continue;
      const previous = rows.get(id);
      if (!previous && rows.size >= 500) break;
      if (previous && previous.summary[1] === summary[1] && !equal(previous.summary[2], summary[2])) {
        previous.conflict = true; continue;
      }
      if (!previous || previous.summary[1] < summary[1]) rows.set(id, {id, summary, source: page.directory.key,
        sourceName: page.directory.name, checkedAt: Date.now(), conflict: false});
    }
    page.snapshot = result[0]; page.cursor = result[2]; page.reads=(page.reads || 0)+1;
    if (page.cursor && rows.size < 500) this.pages.push(page);
    if (!page.background) this.emitRows(generation);
  }

  async readRegionPage(page, generation) {
    const rows=page.rows || this.rows, before=rows.size;
    // A rectangular query may start with neighbors or land outside a sea.
    // Advance a bounded batch automatically rather than showing a false end.
    for(let scanned=0;scanned<5;scanned++) {
      await this.readPage(page,generation);
      if(generation!==this.generation || !page.region || !page.cursor || rows.size>before || scanned===4) return;
      const queued=this.pages.indexOf(page);
      if(queued>=0)this.pages.splice(queued,1);
    }
  }

  more() {
    const generation = this.generation;
    return this.enqueue(async () => {
      const pages = this.pages.splice(0);
      for (const page of pages) {
        if (generation !== this.generation) return;
        await this.readRegionPage(page, generation);
      }
      this.emitRows(generation);
    });
  }

  emitRows(generation) {
    if (generation === this.generation) this.notify({type: 'results', generation,
      queryToken:this.queryToken, rows: [...this.rows.values()], more: this.pages.length > 0, limited: this.rows.size >= 500});
  }

  get(id) {
    const row = this.rows.get(id);
    return this.enqueue(async () => {
      if (!row || row.conflict) throw Error('Cache is missing or sources disagree');
      const body = await this.request(this.directories.get(row.source), 3, [row.summary[0], row.summary[2], null]);
      if (!Array.isArray(body) || body.length !== 3 || !integer(body[1], 0, 1) || !integer(body[2], 0, 1)) throw Error('Invalid get response');
      if (body[2]) throw Error('Directory reports an author version conflict');
      const verified = await verifySigned(body[0], row.summary);
      return {...verified, isCurrent: body[1] === 1, checkedAt: Date.now(), sourceName: row.sourceName};
    });
  }

  async close() {
    this.closed = true; this.generation++;
    this.querySnapshots.clear();
    this.discoveryRefresh?.stop();
    this.pending?.reject(Error('Disconnected'));
    await this.interface?.disconnect();
    await this.rns?.stop();
  }
}
