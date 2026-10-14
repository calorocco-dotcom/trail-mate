"""Track v2 bounded metadata and signed attachment-index descriptor."""
from tools.geocaching.directory_store import text_valid

MAX_INDEX_BYTES = 294912
MAX_PACKAGE_BYTES = 64 * 1024 * 1024
MAX_ATTACHMENTS = 64


def _integer(value, low, high):
    return type(value) is int and low <= value <= high


def _hash(value):
    return type(value) is bytes and len(value) == 32


def validate_extensions(metadata, assets, points):
    if (type(metadata) is not list or len(metadata) != 8 or not _integer(metadata[0], 0, 255)
            or type(metadata[1]) is not list or len(metadata[1]) > 8
            or any(not text_valid(tag, 48, nonempty=True) for tag in metadata[1])
            or len(set(metadata[1])) != len(metadata[1])
            or not _integer(metadata[2], 0, 0xffffffff)
            or not _integer(metadata[3], 0, points) or not _integer(metadata[4], 0, points)
            or not _integer(metadata[5], 0, 2) or not _integer(metadata[6], 0, 4)
            or metadata[7] is not None and (type(metadata[7]) is not list or len(metadata[7]) != 2
                                           or not all(_hash(value) for value in metadata[7]))):
        raise ValueError("manifest metadata")
    if assets is not None and (type(assets) is not list or len(assets) != 4 or not _hash(assets[0])
                               or not _integer(assets[1], 1, MAX_INDEX_BYTES)
                               or not _integer(assets[2], 1, MAX_ATTACHMENTS)
                               or not _integer(assets[3], 1, 1)):
        raise ValueError("attachment index descriptor")
