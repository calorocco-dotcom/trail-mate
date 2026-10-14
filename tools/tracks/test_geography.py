import unittest
from tools.tracks.geography import intersects


class GeographyTests(unittest.TestCase):
    def test_date_line_and_edges(self):
        crossing = [1700000000, 0, -1700000000, 100000000]
        self.assertTrue(intersects(crossing, [1790000000, 0, 1795000000, 100000000]))
        self.assertTrue(intersects(crossing, [-1795000000, 0, -1790000000, 100000000]))
        self.assertFalse(intersects(crossing, [-100000000, 0, 100000000, 100000000]))
        self.assertFalse(intersects(crossing, [1790000000, 100000001, 1795000000, 200000000]))
