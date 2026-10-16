import hashlib
import io
import tempfile
import unittest
import RNS
from tools.tracks.blob_store import BlobStore
from tools.tracks.directory_index import DirectoryIndex
from tools.tracks.kml_geometry import read_verified_kml_geometry, manifest_geometry_bounds
from tools.tracks.protocol import packed
from tools.tracks.region_catalog import RegionCatalog


class RegionQueryTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.blobs = BlobStore(temporary.name)
        self.addCleanup(self.blobs.close)
        self.index = DirectoryIndex(temporary.name, self.blobs)
        self.addCleanup(self.index.close)
        self.identity = RNS.Identity()
        ring = [[0,0],[4,0],[4,1],[1,1],[1,4],[0,4],[0,0]]
        features = [{"type":"Feature","id":identifier,"properties":{"name":"Same region name"},"geometry":{"type":"Polygon","coordinates":[ring]}} for identifier in ("region","other")]
        self.catalog = RegionCatalog("example", {"type":"FeatureCollection","features":features})

    def publish(self, coordinates, nonce, *, false_box=None):
        data = f'<kml xmlns="http://www.opengis.net/kml/2.2"><Placemark><LineString><coordinates>{coordinates}</coordinates></LineString></Placemark></kml>'.encode()
        digest = hashlib.sha256(data).digest()
        result = self.blobs.begin(self.identity.hash,digest,len(data))
        if result["token"]:
            self.blobs.append(self.identity.hash,result["token"],0,data)
            self.blobs.finish(self.identity.hash,result["token"])
        geometries = read_verified_kml_geometry(io.BytesIO(data),digest,len(data))
        box, count = manifest_geometry_bounds(geometries)
        record = [1,self.identity.get_public_key(),bytes([nonce])*16,0,1,None,0,100,100,"Track","",false_box or box,[0,None,None,count],[digest,len(data),"application/vnd.google-earth.kml+xml",1]]
        raw = packed(record)
        result = self.index.publish(self.identity.hash,[raw,self.identity.sign(b"TrailMate.Track.Manifest.v1\0"+raw)],None)
        return result, record

    def test_exact_region_excludes_bbox_only_match(self):
        self.publish("3,2 3,3",1)
        included,_ = self.publish("0.5,0.5 2,0.5",2)
        self.assertEqual(len(self.index.query([0,0,40000000,40000000])["items"]),2)
        page = self.index.query_region(self.catalog,"example:region",self.catalog.version)
        self.assertEqual([item[0] for item in page["items"]],[included["object"]])

    def test_pagination_binds_region_id_version_filters_and_snapshot(self):
        for nonce in range(1,4):
            self.publish("0.5,0.5 2,0.5",nonce)
        first = self.index.query_region(self.catalog,"example:region",self.catalog.version,limit=1)
        second = self.index.query_region(self.catalog,"example:region",self.catalog.version,limit=1,snapshot=first["snapshot"],cursor=first["cursor"])
        self.assertNotEqual(first["items"][0][0],second["items"][0][0])
        with self.assertRaisesRegex(ValueError,"binding"):
            self.index.query_region(self.catalog,"example:other",self.catalog.version,snapshot=first["snapshot"],cursor=first["cursor"])
        with self.assertRaisesRegex(ValueError,"binding"):
            self.index.query_region(self.catalog,"example:region",self.catalog.version,state=None,snapshot=first["snapshot"],cursor=first["cursor"])
        self.publish("0.5,0.5 2,0.5",4)
        with self.assertRaisesRegex(ValueError,"snapshot"):
            self.index.query_region(self.catalog,"example:region",self.catalog.version,snapshot=first["snapshot"],cursor=first["cursor"])

    def test_false_signed_bbox_is_rejected_before_prefilter(self):
        with self.assertRaisesRegex(ValueError,"differs from signed"):
            self.publish("0.5,0.5 2,0.5",1,false_box=[1000000000,0,1000000001,1])
        self.assertEqual(self.index.query_region(self.catalog,"example:region",self.catalog.version)['items'], [])

    def test_archive_filter_keeps_geometry_and_identity(self):
        result, record = self.publish("0.5,0.5 2,0.5",1)
        record[4],record[5],record[6],record[8] = 2,result["hash"],1,101
        raw = packed(record)
        self.index.publish(self.identity.hash,[raw,self.identity.sign(b"TrailMate.Track.Manifest.v1\0"+raw)],result["hash"])
        self.assertEqual(self.index.query_region(self.catalog,"example:region",self.catalog.version)["items"],[])
        archived = self.index.query_region(self.catalog,"example:region",self.catalog.version,state=1,author=self.identity.get_public_key())["items"]
        self.assertEqual(len(archived),1)
        self.assertEqual(archived[0][4],1)


class QuantizationTests(unittest.TestCase):
    def test_shared_half_ties_and_anchor_date_line_bounds(self):
        raw = b'<kml xmlns="http://www.opengis.net/kml/2.2"><Placemark><LineString><coordinates>-0.00000005,-0.00000005 0.00000005,0.00000005</coordinates></LineString></Placemark></kml>'
        geometry = read_verified_kml_geometry(io.BytesIO(raw),hashlib.sha256(raw).digest(),len(raw),chunk_bytes=1)
        self.assertEqual(geometry[0][1],((0,0),(1,1)))
        self.assertEqual(manifest_geometry_bounds((("LineString",((1790000000,0),(-1790000000,10000000))),)),([1790000000,0,-1790000000,10000000],2))


if __name__ == '__main__':
    unittest.main()
