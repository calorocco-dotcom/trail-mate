"""Deterministic per-request limits; no device storage or wall-clock promises."""
from tools.tracks.protocol import integer


class RegionWorkLimit(ValueError):
    pass


class RegionQueryBudget:
    def __init__(self, *, steps=2000000, file_bytes=33554432):
        if not integer(steps, 1, 2000000) or not integer(file_bytes, 1, 33554432):
            raise ValueError("region work budget options")
        self.steps_remaining = steps
        self.bytes_remaining = file_bytes

    def consume(self, steps):
        if not integer(steps, 0, 2000000) or steps > self.steps_remaining:
            raise RegionWorkLimit("region geometry work budget")
        self.steps_remaining -= steps

    def read_file(self, size):
        if not integer(size, 1, 8388608) or size > self.bytes_remaining:
            raise RegionWorkLimit("region file work budget")
        self.bytes_remaining -= size
