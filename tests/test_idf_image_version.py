import importlib.util
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / 'scripts/check_idf_app_version.py'
spec = importlib.util.spec_from_file_location('idf_image_version', SCRIPT)
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


def image(version=b'1.0.0\0'):
    data = bytearray(288)
    data[0:2] = bytes([0xE9, 1])
    struct.pack_into('<I', data, 28, 256)
    struct.pack_into('<I', data, 32, 0xABCD5432)
    data[48:48 + len(version)] = version
    return data


class IdfImageVersionTest(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.path = Path(self.folder.name) / 'firmware.bin'

    def test_embedded_descriptor_must_match_release(self):
        self.path.write_bytes(image())
        self.assertEqual(checker.verify(self.path, '1.0.0'), '1.0.0')
        with self.assertRaisesRegex(ValueError, 'does not match'):
            checker.verify(self.path, '1.0.1')

    def test_refuses_truncated_wrong_header_and_missing_descriptor(self):
        for offset in [0, 1, 28, 32]:
            data = image()
            data[offset:offset + 4] = b'\0' * 4
            self.path.write_bytes(data)
            with self.assertRaises(ValueError):
                checker.image_version(self.path)
        self.path.write_bytes(image()[:-1])
        with self.assertRaises(ValueError):
            checker.image_version(self.path)

    def test_refuses_empty_unterminated_and_nonascii_versions(self):
        for version in [b'\0', b'x' * 32, b'\xff\0']:
            self.path.write_bytes(image(version))
            with self.assertRaises(ValueError):
                checker.image_version(self.path)

    def test_cli_rejects_default_idf_version_in_mislabeled_file(self):
        self.path.write_bytes(image(b'1\0'))
        result = subprocess.run([sys.executable, str(SCRIPT), str(self.path),
                                 '--version', '1.0.0'], capture_output=True, text=True)
        self.assertEqual(result.returncode, 1)
        self.assertIn("'1' does not match release '1.0.0'", result.stderr)


if __name__ == '__main__':
    unittest.main()
