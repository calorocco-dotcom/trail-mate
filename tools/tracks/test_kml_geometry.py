import hashlib
import io
import tempfile
import unittest
from tools.tracks.kml_geometry import read_verified_kml_geometry
from tools.tracks.region_catalog import RegionCatalog
from tools.tracks.blob_store import BlobStore

NS = 'http://www.opengis.net/kml/2.2'
GX = 'http://www.google.com/kml/ext/2.2'


def kml(body):
    return f'<kml xmlns="{NS}" xmlns:gx="{GX}"><Document><name>山径</name>{body}</Document></kml>\n'.encode()


def read(raw, chunk=65536):
    return read_verified_kml_geometry(io.BytesIO(raw), hashlib.sha256(raw).digest(), len(raw), chunk_bytes=chunk)


class GeometryTests(unittest.TestCase):
    def test_chunks_namespaces_points_lines_and_track(self):
        raw = kml('<Folder><Placemark><MultiGeometry><Point><coordinates>180,30,100</coordinates></Point>'
                  '<LineString><coordinates>120,30 121,31,20</coordinates></LineString>'
                  '<gx:MultiTrack><gx:Track><gx:coord>120 30 5</gx:coord><gx:coord>121 31 6</gx:coord></gx:Track></gx:MultiTrack>'
                  '</MultiGeometry></Placemark></Folder>')
        expected = (("Point", (-1800000000, 300000000)), ("LineString", ((1200000000, 300000000), (1210000000, 310000000))),
                    ("LineString", ((1200000000, 300000000), (1210000000, 310000000))))
        for chunk in (1, 2, 3, 7, 64, 65536):
            self.assertEqual(read(raw, chunk), expected)

    def test_polygon_holes_are_one_filled_geometry_not_separate_lines(self):
        raw = kml('<Placemark><Polygon><innerBoundaryIs><LinearRing><coordinates>3,3 7,3 7,7 3,7 3,3</coordinates></LinearRing></innerBoundaryIs>'
                  '<outerBoundaryIs><LinearRing><coordinates>0,0 10,0 10,10 0,10 0,0</coordinates></LinearRing></outerBoundaryIs></Polygon></Placemark>')
        geometries = read(raw, 1)
        self.assertEqual(len(geometries), 1)
        self.assertEqual(geometries[0][0], "Polygon")
        self.assertEqual(len(geometries[0][1]), 2)
        region = {"type": "Feature", "id": "region", "properties": {"name": "Region"}, "geometry": {"type": "Polygon", "coordinates": [[[4,4],[6,4],[6,6],[4,6],[4,4]]]}}
        catalog = RegionCatalog("example", {"type": "FeatureCollection", "features": [region]})
        self.assertFalse(catalog.intersects_geometry("example:region", catalog.version, *geometries[0]))

    def test_hash_size_and_full_xml_must_complete(self):
        raw = kml('<Placemark><Point><coordinates>120,30</coordinates></Point></Placemark>')
        digest = hashlib.sha256(raw).digest()
        for changed, size, expected in ((raw, len(raw), bytes(32)), (raw+b' ', len(raw), digest), (raw[:-1], len(raw), digest), (raw+b'<broken>', len(raw)+8, hashlib.sha256(raw+b'<broken>').digest())):
            with self.assertRaises(ValueError):
                read_verified_kml_geometry(io.BytesIO(changed), expected, size)

    def test_dtd_namespace_external_geometry_and_malformed_coordinates(self):
        cases = [b'<!DOCTYPE kml [<!ENTITY x "0,0">]>'+kml('<Placemark><Point><coordinates>&x;</coordinates></Point></Placemark>'),
                 b'<?xml version="1.0" encoding="ISO-8859-1"?>'+kml('<Placemark><Point><coordinates>0,0</coordinates></Point></Placemark>'),
                 b'<kml><Placemark><Point><coordinates>0,0</coordinates></Point></Placemark></kml>',
                 kml('<NetworkLink/>'), kml('<Placemark><Model/></Placemark>'),
                 kml('<ExtendedData><Placemark><Point><coordinates>0,0</coordinates></Point></Placemark></ExtendedData>')]
        for value in ('NaN,0', '0,Infinity', '181,0', '0,91', '0,0,1e999', '0,0 1,1', '0, 0', '1e999999999999999999999999999999999999,0'):
            cases.append(kml(f'<Placemark><Point><coordinates>{value}</coordinates></Point></Placemark>'))
        cases.append(kml('<Placemark><LineString><coordinates>0,0</coordinates></LineString></Placemark>'))
        cases.append(kml('<Placemark><Point><coordinates>0,0</coordinates><coordinates>1,1</coordinates></Point></Placemark>'))
        cases.append(kml('<Placemark><Polygon><outerBoundaryIs><LinearRing><coordinates>0,0 1,0 0,1 0.00000001,0</coordinates></LinearRing></outerBoundaryIs></Polygon></Placemark>'))
        for raw in cases:
            with self.subTest(raw=raw[:160]), self.assertRaises(ValueError):
                read(raw, 1)

    def test_actual_blob_store_read_then_detect_storage_corruption(self):
        raw = kml('<Placemark><LineString><coordinates>0,0 1,1</coordinates></LineString></Placemark>')
        digest = hashlib.sha256(raw).digest()
        with tempfile.TemporaryDirectory() as state:
            store = BlobStore(state)
            try:
                owner = b'o'*16
                token = store.begin(owner, digest, len(raw))["token"]
                store.append(owner, token, 0, raw)
                store.finish(owner, token)
                self.assertEqual(store.read_geometries(digest, len(raw))[0][0], "LineString")
                stored = store.files/(digest.hex()+'.kml')
                stored.write_bytes(raw.replace(b'1,1', b'2,2'))
                with self.assertRaisesRegex(ValueError, "hash"):
                    store.read_geometries(digest, len(raw))
            finally:
                store.close()


if __name__ == '__main__':
    unittest.main()
