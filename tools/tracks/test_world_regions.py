"""Check the shipped world catalog through the production directory reader."""
from pathlib import Path
import unittest

from tools.tracks.region_catalog import RegionCatalog


class WorldRegionDataTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.catalog = RegionCatalog.from_file("world", Path(__file__).parent / "data/world-regions.json")

    def test_two_levels_and_country_child_pagination(self):
        catalog = self.catalog
        self.assertEqual(catalog.capabilities()[4], 1)
        for country, expected in (("CHN", 32), ("USA", 51), ("JPN", 47), ("AUT", 9)):
            with self.subTest(country=country):
                parent = "world:" + country
                after, children = None, []
                while True:
                    _, after, page = catalog.children(parent, version=catalog.version, after=after, limit=16)
                    children.extend(page)
                    if after is None:
                        break
                self.assertEqual(len(children), expected)
                self.assertEqual(len({child[0] for child in children}), expected)
                self.assertTrue(all(child[1] == parent and child[2] == 1 and child[6] == 0 for child in children))

    def test_real_city_points_use_region_polygon(self):
        catalog = self.catalog
        cities = [("world:CHN-1820", "Zhejiang", 120.1551, 30.2741),
                  ("world:USA-3521", "California", -122.4194, 37.7749),
                  ("world:JPN-1860", "Tokyo", 139.6917, 35.6895),
                  ("world:NZL-3398", "Auckland", 174.7633, -36.8485)]
        for identifier, name, longitude, latitude in cities:
            with self.subTest(region=name):
                node = catalog.get(identifier, catalog.version)
                self.assertEqual(node[3], name)
                self.assertIn("zh-Hans", dict(node[4]))
                point = [round(longitude * 10000000), round(latitude * 10000000)]
                self.assertTrue(catalog.intersects_geometry(identifier, catalog.version, "Point", point))
                self.assertFalse(catalog.intersects_geometry(identifier, catalog.version, "Point", [0, 0]))


if __name__ == "__main__":
    unittest.main()
