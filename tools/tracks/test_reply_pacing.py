import unittest
from types import SimpleNamespace
from unittest.mock import Mock
from tools.geocaching.reply_queue import advance_reply_queue, reply_queue_wait


class TrackReplyPacingTests(unittest.TestCase):
    def test_active_ten_ms_cadence_and_idle_wait_remain_bounded(self):
        router = SimpleNamespace(process_outbound=Mock())
        due = advance_reply_queue(router,[object()],1,0,interval=.01)
        self.assertAlmostEqual(due,1.01)
        self.assertEqual(advance_reply_queue(router,[object()],1.005,due,interval=.01),due)
        self.assertEqual(router.process_outbound.call_count,1)
        self.assertAlmostEqual(reply_queue_wait([object()],1.005,due),.005)
        self.assertEqual(reply_queue_wait([],1,due),.1)
        self.assertEqual(reply_queue_wait([object()],2,due),.001)
        advance_reply_queue(router,[object()],due,due,interval=.01)
        self.assertEqual(router.process_outbound.call_count,2)

    def test_default_geocaching_cadence_and_invalid_intervals(self):
        router = SimpleNamespace(process_outbound=Mock())
        self.assertEqual(advance_reply_queue(router,[],1,0),0)
        self.assertAlmostEqual(advance_reply_queue(router,[object()],1,0),1.1)
        for interval in [0,.001,.101,float('nan'),float('inf'),True,'0.01']:
            with self.assertRaises(ValueError):
                advance_reply_queue(router,[object()],2,0,interval=interval)
        self.assertEqual(router.process_outbound.call_count,1)
