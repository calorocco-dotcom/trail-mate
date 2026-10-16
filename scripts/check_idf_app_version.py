"""Verify the version embedded in a native ESP-IDF application image."""
import argparse
from pathlib import Path
import struct
import sys

# ESP-IDF 5.5 esp_image_header_t (24 B), segment header (8 B), then
# esp_app_desc_t (256 B). Its version field is 16 bytes into the descriptor.
PREFIX_BYTES = 24 + 8 + 256


def image_version(image: Path) -> str:
    with image.open('rb') as stream:
        prefix = stream.read(PREFIX_BYTES)
    if len(prefix) != PREFIX_BYTES or prefix[0] != 0xE9 or prefix[1] == 0:
        raise ValueError('not a complete ESP application header')
    if struct.unpack_from('<I', prefix, 28)[0] < 256:
        raise ValueError('first image segment cannot contain an application descriptor')
    if struct.unpack_from('<I', prefix, 32)[0] != 0xABCD5432:
        raise ValueError('application descriptor is missing')
    field = prefix[48:80]
    if b'\0' not in field:
        raise ValueError('application version is not terminated')
    try:
        version = field.split(b'\0', 1)[0].decode('ascii')
    except UnicodeDecodeError as error:
        raise ValueError('application version is not ASCII') from error
    if not version:
        raise ValueError('application version is empty')
    return version


def verify(image: Path, expected: str) -> str:
    actual = image_version(image)
    if actual != expected:
        raise ValueError(f'application version {actual!r} does not match release {expected!r}')
    return actual


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    parser.add_argument('--version', required=True)
    args = parser.parse_args()
    try:
        actual = verify(args.image, args.version)
    except (OSError, ValueError) as error:
        print(f'IDF image version check failed: {error}', file=sys.stderr)
        return 1
    print(f'IDF application version verified: {actual} ({args.image})')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
