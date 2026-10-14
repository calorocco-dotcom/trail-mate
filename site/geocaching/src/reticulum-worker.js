import {DirectoryClient} from './protocol-client.js';
import {makeGpx} from './gpx-download.js';

let client = null, liveReady = false;
self.onmessage = async ({data}) => {
  const {id, command, args = {}} = data;
  try {
    let result;
    if (command === 'connect') {
      liveReady = false;
      await client?.close();
      client = new DirectoryClient(event => {
        if (event.type === 'directory') liveReady = true;
        if (event.type === 'status' && event.state === 'disconnected') liveReady = false;
        self.postMessage({event});
      });
      await client.connect(args.url, null, args.discoverySeeds);
    } else if (command === 'disconnect') {
      liveReady = false;
      await client?.close(); client = null;
    } else {
      if (!client || !liveReady) throw Error('Live directory connection unavailable');
      if (command === 'directories') result = await client.verifiedDirectories();
      else if (command === 'current') result = await client.current(args.directoryKey,args.cacheId);
      else if (command === 'publish') result = await client.publish(args.signed,{directoryKey:args.directoryKey,authorPublicKey:args.authorPublicKey});
      else if (command === 'publicationReceipt') result = await client.publicationReceipt(args.directoryKey,args.signed,args.authorPublicKey);
      else if (command === 'query') await client.query(args.bounds, args.stateMask, args.region, args.queryToken, {refresh:args.refresh===true});
      else if (command === 'more') await client.more();
      else if (command === 'get') result = await client.get(args.cacheId);
      else if (command === 'download') {
        if (!Array.isArray(args.cacheIds) || !args.cacheIds.length || args.cacheIds.length > 20) throw Error('Select 1–20 caches');
        const records = [], failures = [];
        for (const cacheId of args.cacheIds) {
          try { records.push((await client.get(cacheId)).signed); }
          catch (error) { failures.push({cacheId, message: error.message}); }
        }
        result = {gpx: records.length ? await makeGpx(records) : null, succeeded: records.length, failures};
      } else throw Error('Unknown command');
    }
    self.postMessage({id, result});
  } catch (error) {
    self.postMessage({id, error: error.message, errorCode:typeof error.code==='string'?error.code:null,
                     errorStatus:Number.isInteger(error.status)?error.status:null,errorReason:typeof error.reason==='string'?error.reason:null});
  }
};
