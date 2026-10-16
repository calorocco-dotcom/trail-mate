"""HostLink log-event framing for the bounded navigation capture helper."""
import binascii
import struct


def encode_frame(kind, payload=b''):
    raw = struct.pack('<2sBBHH', b'HL', 1, kind, 1, len(payload)) + payload
    return raw + struct.pack('<H', binascii.crc_hqx(raw, 0xffff))


class FrameDecoder:
    """Incremental CRC validation, bounded to a single 522-byte frame."""
    def __init__(self):
        self.buffer = bytearray()

    def feed(self, incoming):
        frames = []
        for byte in incoming:
            self.buffer.append(byte)
            while self.buffer:
                buf = self.buffer
                if buf[0] != 0x48 or (len(buf) > 1 and buf[1] != 0x4c):
                    del buf[0]
                    continue
                if len(buf) < 8:
                    break
                size = int.from_bytes(buf[6:8], 'little')
                if buf[2] != 1 or size > 512:
                    del buf[0]
                    continue
                total = size + 10
                if len(buf) < total:
                    break
                if binascii.crc_hqx(buf[:total-2], 0xffff) != int.from_bytes(buf[total-2:total], 'little'):
                    del buf[0]
                    continue
                frames.append((buf[3], bytes(buf[8:total-2])))
                del buf[:total]
        return frames
