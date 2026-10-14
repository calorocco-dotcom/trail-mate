"""Exercise the installed native LXMF processor, including its skipped reply."""
import threading
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch
import LXMF
import RNS
from tools.geocaching.reply_queue import advance_reply_queue


class ReplyQueueTests(unittest.TestCase):
    def test_native_delivered_removal_skips_next_reply_until_bounded_pump(self):
        destination = SimpleNamespace(hash=bytes(16))
        old = SimpleNamespace(state=LXMF.LXMessage.DELIVERED, include_ticket=False,
                              destination_hash=destination.hash,
                              method=LXMF.LXMessage.OPPORTUNISTIC,
                              get_destination=lambda: destination)
        fresh = SimpleNamespace(state=LXMF.LXMessage.OUTBOUND, progress=0,
                                method=LXMF.LXMessage.DIRECT, delivery_attempts=0,
                                get_destination=lambda: destination,
                                set_delivery_destination=Mock(), send=Mock())
        router = object.__new__(LXMF.LXMRouter)
        router.outbound_processing_lock = threading.Lock()
        router.pending_outbound = [old, fresh]
        router.direct_links = {destination.hash: SimpleNamespace(status=RNS.Link.ACTIVE)}
        router.backchannel_links = {}
        with patch.object(RNS.Reticulum, 'get_instance', return_value=SimpleNamespace(_retain_destination_data=lambda _: None)):
            router.process_outbound()
        self.assertEqual(router.pending_outbound, [fresh])
        fresh.send.assert_not_called()
        due = advance_reply_queue(router, [fresh], 10.0, 0)
        self.assertAlmostEqual(due, 10.1)
        fresh.send.assert_called_once()
        # The upstream's lock still owns concurrency; the helper does not
        # bypass it or spawn work when the periodic processor is running.
        with router.outbound_processing_lock:
            advance_reply_queue(router, [fresh], 10.2, due)
        fresh.send.assert_called_once()

    def test_rate_bound_and_idle_do_not_create_processing_work(self):
        router = SimpleNamespace(process_outbound=Mock())
        self.assertEqual(advance_reply_queue(router, [], 1, 0), 0)
        due = advance_reply_queue(router, [object()], 1, 0)
        self.assertEqual(advance_reply_queue(router, [object()], 1.05, due), due)
        router.process_outbound.assert_called_once()
        advance_reply_queue(router, [object()], due, due)
        self.assertEqual(router.process_outbound.call_count, 2)
