import unittest
from unittest.mock import patch
from tools.tracks import test_region_query as fixtures
from tools.tracks.application import TrackApplication
from tools.tracks.protocol import packed, unpacked
from tools.tracks.region_budget import RegionQueryBudget, RegionWorkLimit


class RegionBudgetTests(unittest.TestCase):
    def setUp(self):
        self.fixture = fixtures.RegionQueryTests()
        self.fixture.setUp()
        self.addCleanup(self.fixture.doCleanups)
        self.catalog = self.fixture.catalog

    def test_exact_limit_and_shared_geometries(self):
        budget = RegionQueryBudget(steps=16)
        self.assertTrue(self.catalog.intersects_geometry('example:region', self.catalog.version, 'Point', [0, 0], budget=budget))
        self.assertEqual(budget.steps_remaining, 0)
        with self.assertRaises(RegionWorkLimit):
            self.catalog.intersects_geometry('example:region', self.catalog.version, 'Point', [0, 0], budget=budget)
        # Reusing a budget must accumulate work, not reset for each geometry.
        budget = RegionQueryBudget(steps=31)
        self.assertTrue(self.catalog.intersects_geometry('example:region', self.catalog.version, 'Point', [0, 0], budget=budget))
        with self.assertRaises(RegionWorkLimit):
            self.catalog.intersects_geometry('example:region', self.catalog.version, 'Point', [0, 0], budget=budget)

    def test_segment_pairs_are_counted_after_vertex_tests(self):
        budget = RegionQueryBudget(steps=28)
        with self.assertRaisesRegex(RegionWorkLimit, 'geometry work budget'):
            self.catalog.intersects_geometry('example:region', self.catalog.version, 'LineString', [[30000000, 20000000], [30000000, 30000000]], budget=budget)
        self.assertEqual(budget.steps_remaining, 0)
        self.assertFalse(self.catalog.intersects_geometry('example:region', self.catalog.version, 'LineString', [[30000000, 20000000], [30000000, 30000000]]))

    def test_whole_page_file_budget_stops_before_second_file(self):
        _, record = self.fixture.publish('0.5,0.5 2,0.5', 1)
        self.fixture.publish('0.5,0.5 2,0.5', 2)
        budget = RegionQueryBudget(file_bytes=record[13][1])
        with patch('tools.tracks.directory_index.RegionQueryBudget', return_value=budget), patch.object(self.fixture.blobs, 'read_geometries', wraps=self.fixture.blobs.read_geometries) as read:
            with self.assertRaisesRegex(RegionWorkLimit, 'file work budget'):
                self.fixture.index.query_region(self.catalog, 'example:region', self.catalog.version)
            self.assertEqual(read.call_count, 1)
        self.assertEqual(len(self.fixture.index.query_region(self.catalog, 'example:region', self.catalog.version)['items']), 2)

    def test_wire_budget_failure_is_413_and_has_no_partial_results(self):
        self.fixture.publish('0.5,0.5 2,0.5', 1)
        application = TrackApplication(self.fixture.identity, self.fixture.blobs, self.fixture.index, regions=self.catalog)
        request = packed([1, 0, 13, b'r' * 16, 8192, ['example:region', self.catalog.version, 'intersects', None, None, 0, 16, None, None]])
        with patch('tools.tracks.directory_index.RegionQueryBudget', return_value=RegionQueryBudget(steps=1)):
            response = unpacked(application.handle(self.fixture.identity.hash, request))
        self.assertEqual(response[:5], [1, 1, 13, b'r' * 16, 413])
        self.assertEqual(response[5], ['region geometry work budget'])
        self.assertEqual(unpacked(application.handle(self.fixture.identity.hash, request))[4], 200)

    def test_file_capacity_and_invalid_budget_options(self):
        budget = RegionQueryBudget(file_bytes=8388608)
        budget.read_file(8388608)
        self.assertEqual(budget.bytes_remaining, 0)
        with self.assertRaises(RegionWorkLimit):
            budget.read_file(1)
        for options in ({'steps': 0}, {'steps': True}, {'steps': 2000001}, {'file_bytes': 33554433}):
            with self.assertRaises(ValueError):
                RegionQueryBudget(**options)


if __name__ == '__main__':
    unittest.main()
