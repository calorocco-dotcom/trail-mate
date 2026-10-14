"""Bounded native LXMF reply scheduling for directory request/response flows."""

REPLY_INTERVAL_SECONDS = 0.1


def advance_reply_queue(router, active, now, next_due, *, interval=REPLY_INTERVAL_SECONDS):
    """Progress replies on the existing thread with a bounded active cadence.

    LXMF 1.1.1 can remove a delivered item while iterating pending_outbound,
    skipping the newly queued reply until its four-second periodic job. Calling
    its own synchronized processor again avoids that delay. No router globals,
    delivery attempts, stamps, proof handling or queue limits are changed.
    """
    if type(interval) not in (int, float) or not 0.01 <= interval <= REPLY_INTERVAL_SECONDS:
        raise ValueError('reply interval')
    if active and now >= next_due:
        router.process_outbound()
        return now + interval
    return next_due


def reply_queue_wait(active, now, next_due):
    """Idle wait stays 100 ms; active replies wake by their bounded deadline."""
    return max(0.001, min(REPLY_INTERVAL_SECONDS, next_due-now)) if active else REPLY_INTERVAL_SECONDS
