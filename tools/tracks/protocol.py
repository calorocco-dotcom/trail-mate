"""Track manifest validation shared by native Reticulum directory tooling."""
import hashlib
import RNS
from tools.tracks.manifest_extensions import validate_extensions
from tools.geocaching.directory_store import packed, unpacked, text_valid

APP_TYPE = "trailmate.track.v1"
TRACK_BULK_OPERATION = 19
TRACK_BULK_CHUNK_BYTES = 4096


def integer(value, low, high):
    return type(value) is int and low <= value <= high


def binary(value, size):
    return type(value) is bytes and len(value) == size


def text(value, maximum, empty=False):
    return text_valid(value, maximum, multiline=empty, nonempty=not empty)


def verify_manifest(signed, expected_author=None):
    if type(signed) is not list or len(signed) != 2 or not binary(signed[1], 64):
        raise ValueError("signed manifest shape")
    raw, signature = signed
    record = unpacked(raw, 4096)
    if type(record) is not list or not record or not integer(record[0], 1, 2) or len(record) != (14 if record[0] == 1 else 16):
        raise ValueError("manifest version/shape")
    version, author, nonce, kind, revision, parent, state, created, updated, title, description, box, stats, file = record[:14]
    if not binary(author, 64) or not any(author) or not binary(nonce, 16) or not any(nonce):
        raise ValueError("manifest identity")
    if not integer(kind, 0, 1) or not integer(revision, 1, 0xffffffff) or not integer(state, 0, 1):
        raise ValueError("manifest revision/state")
    if revision == 1:
        if parent is not None or state != 0:
            raise ValueError("first revision")
    elif not binary(parent, 32) or not any(parent):
        raise ValueError("parent hash")
    if not integer(created, 0, 253402300799) or not integer(updated, created, 253402300799):
        raise ValueError("manifest time")
    if not text(title, 128) or not text(description, 2048, True):
        raise ValueError("manifest text")
    if (type(box) is not list or len(box) != 4 or not integer(box[0], -1800000000, 1799999999)
            or not integer(box[2], -1800000000, 1799999999) or not integer(box[1], -900000000, 900000000)
            or not integer(box[3], box[1], 900000000)):
        raise ValueError("manifest bounds")
    if (type(stats) is not list or len(stats) != 4 or not integer(stats[0], 0, 0xffffffff)
            or any(v is not None and not integer(v, 0, 0xffffffff) for v in stats[1:3])
            or not integer(stats[3], 1, 100000)):
        raise ValueError("manifest statistics")
    if (type(file) is not list or len(file) != 4 or not binary(file[0], 32)
            or not integer(file[1], 1, 8388608) or file[2] != "application/vnd.google-earth.kml+xml" or file[3] != 1):
        raise ValueError("manifest file")
    if expected_author is not None and author != expected_author:
        raise ValueError("author mismatch")
    if version == 2:
        validate_extensions(record[14], record[15], stats[3])
    identity = RNS.Identity(create_keys=False)
    if not identity.load_public_key(author) or not identity.validate(signature, f"TrailMate.Track.Manifest.v{version}\0".encode() + raw):
        raise ValueError("author signature")
    return {"record": record, "raw": raw, "signature": signature,
            "object_id": hashlib.sha256(b"TrailMate.Track.ID.v1\0" + author + nonce).digest(),
            "revision_hash": hashlib.sha256(f"TrailMate.Track.Revision.v{version}\0".encode() + raw).digest()}


def verify_successor(previous_signed, next_signed):
    previous, next_version = verify_manifest(previous_signed), verify_manifest(next_signed)
    old, new = previous["record"], next_version["record"]
    if (previous["object_id"] != next_version["object_id"] or new[4] != old[4] + 1
            or new[5] != previous["revision_hash"] or new[7] != old[7] or new[8] < old[8]
            or old[6] == 1 and new[6] == 0 or new[0] < old[0]):
        raise ValueError("version chain")
    if new[6] == 1 and new[13] != old[13]:
        raise ValueError("archive file binding")
    if new[6] == 1 and new[0] == 2 and new[15] != (old[15] if old[0] == 2 else None):
        raise ValueError("archive attachment binding")
    return next_version
