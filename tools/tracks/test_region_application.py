import hashlib
import json
import tempfile
import unittest
import RNS
from tools.tracks.application import TrackApplication
from tools.tracks.blob_store import BlobStore
from tools.tracks.directory_index import DirectoryIndex
from tools.tracks.protocol import packed, unpacked
from tools.tracks.region_catalog import RegionCatalog


class RegionApplicationTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.blobs = BlobStore(temporary.name)
        self.addCleanup(self.blobs.close)
        self.index = DirectoryIndex(temporary.name,self.blobs)
        self.addCleanup(self.index.close)
        self.author, self.directory = RNS.Identity(), RNS.Identity()
        ring = [[0,0],[4,0],[4,1],[1,1],[1,4],[0,4],[0,0]]
        features = [{"type":"Feature","id":identifier,"properties":{"name":identifier,"parent":parent},"geometry":{"type":"Polygon","coordinates":[ring]}}
                    for identifier,parent in (("country",None),("province","country"),("city","province"),("district","city"),("other","country"))]
        self.catalog = RegionCatalog("example",{"type":"FeatureCollection","features":features})
        self.application = TrackApplication(self.directory,self.blobs,self.index,regions=self.catalog)

    def request(self, operation, body, budget=8192):
        identifier = b'r'*16
        response = unpacked(self.application.handle(self.author.hash,packed([1,0,operation,identifier,budget,body])))
        self.assertEqual(response[:4],[1,1,operation,identifier])
        return response

    def test_extension_preserves_base_capability_and_versioned_hierarchy(self):
        base = self.request(0,[])[5]
        self.assertEqual(len(base),8)
        self.assertEqual(base[-1],list(range(9)))
        caps = self.request(9,[])[5]
        self.assertEqual(caps,[1,"trailmate.track.regions.v1","example",self.catalog.version,3,32,1024,[9,10,11,12,13]])
        first = self.request(10,[self.catalog.version,"example:country",None,1])[5]
        self.assertEqual(first[1],"example:other")
        second = self.request(10,[self.catalog.version,"example:country",first[1],1])[5]
        self.assertEqual(second[2][0][0],"example:province")
        self.assertEqual(self.request(10,[bytes(32),None,None,1])[4],422)
        self.assertEqual(self.request(10,[self.catalog.version,"example:province",first[1],1])[4],422)

    def test_boundary_descriptor_and_exact_chunks_have_one_hash(self):
        version,node,descriptor = self.request(11,["example:country",self.catalog.version])[5]
        self.assertEqual(version,self.catalog.version)
        self.assertEqual(node[0],"example:country")
        self.assertEqual(descriptor[0],"region-e7-json-v1")
        digest,size = descriptor[1:]
        raw = bytearray()
        while len(raw)<size:
            offset,length = len(raw),min(37,size-len(raw))
            response = self.request(12,["example:country",version,offset,length])[5]
            self.assertEqual(response[:3],[digest,size,offset])
            self.assertEqual(len(response[3]),length)
            raw.extend(response[3])
        self.assertEqual(hashlib.sha256(raw).digest(),digest)
        self.assertEqual(json.loads(raw)[:3],[1,version.hex(),"example:country"])
        self.assertEqual(self.request(12,["example:country",version,size,1])[4],422)
        self.assertEqual(self.request(12,["example:country",bytes(32),0,1])[4],422)
        # One immutable encoded region is reused between chunks.
        first = self.catalog.boundary("example:country",version)[2]
        self.assertIs(first,self.catalog.boundary("example:country",version)[2])

    def test_wire_region_query_reads_actual_signed_kml_and_rejects_unknown_relation(self):
        for number,coordinates,box in ((1,"0.5,0.5 2,0.5",[5000000,5000000,20000000,5000000]),(2,"3,2 3,3",[30000000,20000000,30000000,30000000])):
            raw = f'<kml xmlns="http://www.opengis.net/kml/2.2"><Placemark><LineString><coordinates>{coordinates}</coordinates></LineString></Placemark></kml>'.encode()
            digest = hashlib.sha256(raw).digest()
            token = self.blobs.begin(self.author.hash,digest,len(raw))["token"]
            self.blobs.append(self.author.hash,token,0,raw)
            self.blobs.finish(self.author.hash,token)
            record = [1,self.author.get_public_key(),bytes([number])*16,0,1,None,0,100,100,str(number),"",box,[0,None,None,2],[digest,len(raw),"application/vnd.google-earth.kml+xml",1]]
            manifest = packed(record)
            self.index.publish(self.author.hash,[manifest,self.author.sign(b"TrailMate.Track.Manifest.v1\0"+manifest)],None)
        body = ["example:country",self.catalog.version,"intersects",None,None,0,16,None,None]
        response = self.request(13,body)
        self.assertEqual(response[4],200)
        self.assertEqual([row[5] for row in response[5][2]],["1"])
        body[2] = "bbox"
        self.assertEqual(self.request(13,body)[4],422)

    def test_collection_budget_is_checked_even_when_bytes_fit(self):
        names = {"a"+chr(ord('a')+index):"x" for index in range(22)}
        ring = [[0,0],[1,0],[1,1],[0,1],[0,0]]
        features = [{"type":"Feature","id":"r","properties":{"name":"r"},"geometry":{"type":"Polygon","coordinates":[ring]}}]
        features += [{"type":"Feature","id":str(index),"properties":{"name":"n","parent":"r","names":names},"geometry":{"type":"Polygon","coordinates":[ring]}} for index in range(32)]
        self.application.regions = RegionCatalog("e",{"type":"FeatureCollection","features":features})
        body = [self.application.regions.version,"e:r",None,32]
        raw_body = self.application._dispatch(self.author.hash,10,body,b'r'*16)
        self.assertLess(len(packed([1,1,10,b'r'*16,200,raw_body])),8192)
        response = self.request(10,body)
        self.assertEqual(response[4],413)
        self.assertEqual(response[5][0],"response element budget")

    def test_missing_pack_is_explicit_and_response_budget_stays_bounded(self):
        self.application.regions = None
        self.assertEqual(self.request(9,[])[4:], [422,["region capability unsupported"]])
        self.assertEqual(self.request(0,[])[5][-1],list(range(9)))
        self.application.regions = self.catalog
        self.assertEqual(self.request(9,[1])[4],422)
        self.assertEqual(self.request(14,[])[4],422)
        with self.assertRaisesRegex(ValueError,"envelope"):
            self.request(19,[])
        feature = {"type":"Feature","id":"region","properties":{"name":"Region","names":{locale:"x"*128 for locale in ("en","de","es","fr")}},
                   "geometry":{"type":"Polygon","coordinates":[[[0,0],[1,0],[1,1],[0,1],[0,0]]]}}
        self.application.regions = RegionCatalog("large",{"type":"FeatureCollection","features":[feature]})
        response = self.request(11,["large:region",self.application.regions.version],budget=512)
        self.assertEqual(response[4],413)
        self.assertEqual(response[5][0],"response budget")
        self.assertLessEqual(len(packed(response)),512)


if __name__ == '__main__':
    unittest.main()
