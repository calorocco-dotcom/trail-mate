import copy
import unittest
from tools.tracks.region_catalog import RegionCatalog


def feature(identifier, parent=None, name="Same name", coordinates=None):
    return {"type": "Feature", "id": identifier, "properties": {"parent": parent, "name": name, "names": {"zh-Hans": "同名行政区"}},
            "geometry": {"type": "Polygon", "coordinates": [coordinates or [[0, 0], [10, 0], [10, 10], [0, 10], [0, 0]]]}}


class RegionCatalogTests(unittest.TestCase):
    def setUp(self):
        self.data = {"type": "FeatureCollection", "features": [feature("country"), feature("province", "country"), feature("city", "province"), feature("district", "city"), feature("other", "country")]}

    def test_hierarchy_version_and_page_binding(self):
        catalog = RegionCatalog("example", self.data)
        self.assertEqual(catalog.get("example:district", catalog.version)[2], 3)
        self.assertEqual(catalog.get("example:country", catalog.version)[6], 2)
        page = catalog.children("example:country", version=catalog.version, limit=1)
        self.assertEqual(page[1], "example:other")
        self.assertEqual(catalog.children("example:country", version=catalog.version, after=page[1])[2][0][0], "example:province")
        with self.assertRaisesRegex(ValueError, "version"):
            catalog.get("example:country", bytes(32))
        with self.assertRaisesRegex(ValueError, "page"):
            catalog.children("example:province", version=catalog.version, after=page[1])

    def test_version_is_order_independent_and_input_is_not_borrowed(self):
        catalog = RegionCatalog("example", self.data)
        other = copy.deepcopy(self.data)
        other["features"].reverse()
        self.assertEqual(catalog.version, RegionCatalog("example", other).version)
        self.data["features"][0]["properties"]["name"] = "Edited"
        self.assertNotEqual(catalog.version, RegionCatalog("example", self.data).version)
        self.assertEqual(catalog.get("example:country", catalog.version)[3], "Same name")

    def test_date_line_and_invalid_hierarchy_geometry(self):
        data = {"type": "FeatureCollection", "features": [feature("island", coordinates=[[179, 0], [-179, 0], [-179, 1], [179, 1], [179, 0]])]}
        catalog = RegionCatalog("example", data)
        self.assertEqual(catalog.get("example:island", catalog.version)[5], [1790000000, 0, -1790000000, 10000000])
        for change in (lambda d: d["features"][0]["properties"].update(parent="district"),
                       lambda d: d["features"][0]["properties"].update(parent="missing"),
                       lambda d: d["features"].append(copy.deepcopy(d["features"][0])),
                       lambda d: d["features"][0]["geometry"]["coordinates"][0].pop()):
            invalid = copy.deepcopy(self.data)
            change(invalid)
            with self.assertRaises(ValueError):
                RegionCatalog("example", invalid)


class RegionIntersectionTests(unittest.TestCase):
    def catalog(self, rings):
        item = feature("region")
        item["geometry"]["coordinates"] = rings
        return RegionCatalog("example", {"type": "FeatureCollection", "features": [item]})

    def test_concave_boundary_excludes_bbox_false_positive(self):
        catalog = self.catalog([[[0, 0], [4, 0], [4, 1], [1, 1], [1, 4], [0, 4], [0, 0]]])
        intersects = lambda kind, points: catalog.intersects_geometry("example:region", catalog.version, kind, points)
        self.assertFalse(intersects("Point", [30000000, 30000000]))
        self.assertFalse(intersects("LineString", [[30000000, 20000000], [30000000, 30000000]]))
        self.assertTrue(intersects("LineString", [[-10000000, 5000000], [50000000, 5000000]]))
        self.assertTrue(intersects("Point", [0, 0]))

    def test_holes_and_enclosing_kml_polygons(self):
        catalog = self.catalog([[[0, 0], [10, 0], [10, 10], [0, 10], [0, 0]], [[3, 3], [7, 3], [7, 7], [3, 7], [3, 3]]])
        intersects = lambda kind, points: catalog.intersects_geometry("example:region", catalog.version, kind, points)
        self.assertFalse(intersects("Point", [50000000, 50000000]))
        self.assertTrue(intersects("Point", [30000000, 50000000]))
        self.assertFalse(intersects("LineString", [[40000000, 40000000], [60000000, 60000000]]))
        self.assertTrue(intersects("LineString", [[20000000, 50000000], [80000000, 50000000]]))
        outer = [[-10000000, -10000000], [110000000, -10000000], [110000000, 110000000], [-10000000, 110000000], [-10000000, -10000000]]
        self.assertTrue(intersects("Polygon", [outer]))
        hole = [[-5000000, -5000000], [105000000, -5000000], [105000000, 105000000], [-5000000, 105000000], [-5000000, -5000000]]
        self.assertFalse(intersects("Polygon", [outer, hole]))

    def test_date_line_and_reject_unknown_geometry(self):
        catalog = self.catalog([[[179, 0], [-179, 0], [-179, 1], [179, 1], [179, 0]]])
        intersects = lambda kind, points: catalog.intersects_geometry("example:region", catalog.version, kind, points)
        self.assertTrue(intersects("Point", [-1795000000, 5000000]))
        self.assertFalse(intersects("Point", [0, 5000000]))
        self.assertTrue(intersects("LineString", [[1785000000, 5000000], [-1785000000, 5000000]]))
        for kind, coordinates in (("MultiPoint", []), ("LineString", [[0, 0]]), ("Point", [True, 0]), ("Polygon", [[[0, 0], [1, 0], [0, 1]]])):
            with self.assertRaises(ValueError):
                intersects(kind, coordinates)


if __name__ == "__main__":
    unittest.main()
