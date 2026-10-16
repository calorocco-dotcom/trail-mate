"""Transport regression: fragmentation, interleaved events, CRC and bounded noise."""
import unittest
from navigation_capture_transport import encode_frame, FrameDecoder


class NavigationCaptureTest(unittest.TestCase):
    def test_fragmented_log_with_status(self):
        decoder = FrameDecoder()
        raw = encode_frame(2) + encode_frame(0x82, b'other') + encode_frame(0x83, b'[Navigation] failed\n')
        frames = []
        for byte in raw:
            frames.extend(decoder.feed(bytes([byte])))
            self.assertLessEqual(len(decoder.buffer), 522)
        self.assertEqual(frames, [(2, b''), (0x82, b'other'), (0x83, b'[Navigation] failed\n')])

    def test_corrupt_frame_and_noise_resynchronize(self):
        bad = bytearray(encode_frame(0x83, b'bad'))
        bad[-1] ^= 1
        decoder = FrameDecoder()
        self.assertEqual(decoder.feed(b'noise' * 10000 + bad + encode_frame(0x83, b'good')), [(0x83, b'good')])
        self.assertLessEqual(len(decoder.buffer), 522)

    def test_oversized_header_is_rejected(self):
        decoder = FrameDecoder()
        self.assertEqual(decoder.feed(b'HL\x01\x83\x01\x00\xff\xff' + encode_frame(2)), [(2, b'')])


if __name__ == '__main__':
    unittest.main()
