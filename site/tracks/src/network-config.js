// The gateway is shared; discovery addresses belong to different protocols.
// A Geocaching discovery address must never become a track directory hint.
export function parseTrackNetworkConfig(config){
 if(!config||typeof config!=='object'||Array.isArray(config)||typeof config.endpoint!=='string')throw Error('Invalid track network configuration');
 const url=new URL(config.endpoint);
 if(url.username||url.password||url.hash||!(url.protocol==='wss:'||url.protocol==='ws:'&&['localhost','127.0.0.1','[::1]'].includes(url.hostname)))throw Error('Invalid track network endpoint');
 const seeds=config.trackDiscoverySeeds??[];
 if(!Array.isArray(seeds)||seeds.length>3||seeds.some(seed=>typeof seed!=='string'||!/^[0-9a-f]{32}$/i.test(seed)))throw Error('Invalid track discovery seeds');
 return Object.freeze({endpoint:url.href,discoverySeeds:Object.freeze([...new Set(seeds.map(seed=>seed.toLowerCase()))])});
}
