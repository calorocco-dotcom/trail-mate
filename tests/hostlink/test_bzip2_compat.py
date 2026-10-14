"""Cross-check vendored bzip2 against Python libbz2 via the native CLI.

Pass bzip2_compat_cli built with the sources in third_party/bzip2/src.
The process boundary supports a 32-bit native tool with 64-bit Python.
"""
import argparse
import bz2
import random
import subprocess


def run(executable):
    rng = random.Random(20261012)
    cases = [b"", b"a", bytes(range(256)), b"trailmate\x00" * 12000,
             bytes(rng.randrange(256) for _ in range(32768)), b"a" * 1000000]
    checks = 0
    for original in cases:
        for level in (1, 9):
            encoded = subprocess.run([executable, "c", str(level)], input=original,
                                     capture_output=True, check=True).stdout
            assert bz2.decompress(encoded) == original
            checks += 1
            external = bz2.compress(original, compresslevel=level)
            for small in (0, 1):
                decoded = subprocess.run([executable, "d", str(small)], input=external,
                                         capture_output=True, check=True).stdout
                assert decoded == original
                checks += 1
                damaged = bytearray(external)
                damaged[0] ^= 1
                result = subprocess.run([executable, "d", str(small)], input=damaged,
                                        capture_output=True)
                assert result.returncode != 0
                checks += 1
                # Valid header with a corrupted combined CRC must also fail.
                damaged = bytearray(external)
                damaged[-2] ^= 0x40
                result = subprocess.run([executable, "d", str(small)], input=damaged,
                                        capture_output=True)
                assert result.returncode != 0
                checks += 1
    print(f"bzip2 cross-implementation checks passed: {checks}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("executable")
    run(parser.parse_args().executable)
