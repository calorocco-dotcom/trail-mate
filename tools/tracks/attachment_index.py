"""Canonical attachment-index codec; integrity binding is not author proof."""
import hashlib
import re
from tools.geocaching.directory_store import packed, unpacked, text_valid
from tools.tracks.protocol import integer, binary, verify_manifest
from tools.tracks.manifest_extensions import MAX_INDEX_BYTES, MAX_PACKAGE_BYTES, MAX_ATTACHMENTS


def reference_valid(reference):
    if not text_valid(reference, 4096, nonempty=True):
        return False
    match = re.match(r"^https?://", reference, re.IGNORECASE | re.ASCII)
    if match:
        host = re.split(r"[/?#]", reference[match.end():], maxsplit=1)[0]
        return bool(host) and "@" not in host and not any(c == "\\" or ord(c) <= 32 for c in reference)
    path = re.split(r"[?#]", reference, maxsplit=1)[0]
    if not path or path[0] in "/\\":
        return False
    for part in path.split("/"):
        if not part:
            return False
        size, dots, i = 0, True, 0
        while i < len(part):
            c = part[i]
            if c == "%":
                value = part[i+1:i+3]
                if len(value) != 2 or not re.fullmatch("[0-9a-fA-F]{2}", value):
                    return False
                c = chr(int(value, 16))
                i += 2
                if c == "/":
                    return False
            if c in "\\:" or ord(c) < 32 or ord(c) == 127:
                return False
            size += 1
            dots = dots and c == "."
            i += 1
        if dots and size <= 2:
            return False
    return True


def validate_index(record, raw_size=0):
    if (type(record) is not list or len(record) != 4 or not integer(record[0], 1, 1)
            or not binary(record[1], 32) or not integer(record[2], 1, 8388608)
            or type(record[3]) is not list or not integer(len(record[3]), 1, MAX_ATTACHMENTS)):
        raise ValueError("attachment index")
    ids, references = set(), set()
    total = record[2] + raw_size
    for entry in record[3]:
        if (type(entry) is not list or len(entry) != 7 or type(entry[0]) is not str
                or re.fullmatch(r"[A-Za-z0-9_-]{1,48}", entry[0]) is None or not binary(entry[1], 32)
                or not integer(entry[2], 1, 8388608) or not text_valid(entry[3], 96, nonempty=True)
                or re.fullmatch(r"[A-Za-z0-9!#$&^_.+-]+/[A-Za-z0-9!#$&^_.+-]+", entry[3]) is None
                or not reference_valid(entry[4]) or not integer(entry[5], 0, 3) or not integer(entry[6], 0, 1)
                or (entry[5] != 3 or entry[3].lower().startswith("image/")) and entry[2] > 4194304):
            raise ValueError("attachment entry")
        identifier = entry[0].lower()
        if identifier in ids or entry[4] in references:
            raise ValueError("attachment reference conflict")
        ids.add(identifier)
        references.add(entry[4])
        total += entry[2]
        if total > MAX_PACKAGE_BYTES:
            raise ValueError("attachment package budget")
    return record


def encode_index(record):
    raw = packed(validate_index(record))
    if len(raw) > MAX_INDEX_BYTES:
        raise ValueError("attachment index budget")
    validate_index(record, len(raw))
    return raw


def decode_index(raw):
    return validate_index(unpacked(raw, MAX_INDEX_BYTES), len(raw))


def match_index(raw, signed_manifest):
    manifest = verify_manifest(signed_manifest)["record"]
    index = decode_index(raw)
    assets = manifest[15] if manifest[0] == 2 else None
    if (assets is None or assets[1] != len(raw) or assets[2] != len(index[3])
            or manifest[13][0] != index[1] or manifest[13][1] != index[2]
            or hashlib.sha256(raw).digest() != assets[0]):
        raise ValueError("attachment index binding")
    return index
