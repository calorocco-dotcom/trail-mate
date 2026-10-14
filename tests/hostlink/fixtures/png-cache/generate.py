"""Generate original solid JPEG and compressed PNG regression fixtures."""
from pathlib import Path
import struct
import zlib
from PIL import Image

root = Path(__file__).resolve().parent
Image.new("RGB", (16, 8), (255, 0, 0)).save(root / "red-16x8.jpg", quality=95, subsampling=0)
width = height = 96
pixels = bytes(channel for y in range(height) for x in range(width)
               for channel in ((x * 5) & 255, (y * 7) & 255, ((x + y) * 13) & 255,
                               (0, 128, 255)[(x + y) % 3]))
Image.frombytes("RGBA", (width, height), pixels).save(root / "rgba-96x96.png", compress_level=6)

def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

raw = b"".join(b"\0" + pixels[y * width * 4:(y + 1) * width * 4] for y in range(height))
compressor = zlib.compressobj(level=6, strategy=zlib.Z_FIXED)
compressed = compressor.compress(raw) + compressor.flush()
png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
       + chunk(b"IDAT", compressed) + chunk(b"IEND", b""))
(root / "fixed-96x96.png").write_bytes(png)
