import subprocess
import sys
import unittest
from pathlib import Path
import RNS
from tools.tracks.protocol import verify_manifest, verify_successor, packed


class ProtocolTests(unittest.TestCase):
    def test_update_archive_chain(self):
        identity = RNS.Identity()
        record = [1, identity.get_public_key(), b"n" * 16, 0, 1, None, 0, 100, 100,
                  "山径", "描述\n", [0, 0, 0, 0], [0, None, None, 1],
                  [b"f" * 32, 100, "application/vnd.google-earth.kml+xml", 1]]
        def sign(row):
            raw = packed(row)
            return [raw, identity.sign(b"TrailMate.Track.Manifest.v1\0" + raw)]
        first = sign(record)
        record[4] = 2
        record[5] = verify_manifest(first)["revision_hash"]
        record[8] = 101
        second = sign(record)
        self.assertEqual(verify_successor(first, second)["record"][4], 2)
        record[4] = 3
        record[5] = verify_manifest(second)["revision_hash"]
        record[6] = 1
        archived = sign(record)
        self.assertEqual(verify_successor(second, archived)["record"][6], 1)
        record[13] = [b"x" * 32, 100, "application/vnd.google-earth.kml+xml", 1]
        with self.assertRaises(ValueError):
            verify_successor(second, sign(record))
        record[9] = "bad\x01title"
        with self.assertRaises(ValueError):
            verify_manifest(sign(record))

    def test_native_signature(self):
        executable = Path(".codex-build/hostlink-codec/native/track_manifest_sign" + (".exe" if sys.platform == "win32" else ""))
        lines = subprocess.check_output([str(executable.resolve())], text=True).splitlines()
        raw, signature = (bytes.fromhex(line) for line in lines)
        result = verify_manifest([raw, signature])
        self.assertEqual(result["record"][9], "山径")
        self.assertEqual(result["record"][7], 5000000000)
        self.assertEqual(len(result["object_id"]), 32)
        with self.assertRaises(ValueError):
            verify_manifest([raw, bytes([signature[0] ^ 1]) + signature[1:]])


if __name__ == "__main__":
    unittest.main()
