"""Exact author annotations in directory queries; v1 means unspecified/untagged."""
from tools.tracks.protocol import integer, text


def validate_metadata_filters(activity, tags):
    if (activity is not None and not integer(activity, 0, 255)
            or type(tags) is not list or len(tags) > 8
            or any(not text(tag, 48) for tag in tags) or len(set(tags)) != len(tags)):
        raise ValueError("metadata query filters")
    return activity, list(tags)


def matches_metadata(record, activity, tags):
    actual_activity, actual_tags = (record[14][0], record[14][1]) if record[0] == 2 else (0, [])
    return (activity is None or activity == actual_activity) and all(tag in actual_tags for tag in tags)


def query_summary(verified, metadata):
    record = verified["record"]
    row = [verified["object_id"], verified["revision_hash"], record[4], record[3], record[6],
           record[9], record[11], record[12], record[13][1], record[1]]
    if metadata:
        row.extend([record[14][0], record[14][1]] if record[0] == 2 else [0, []])
    return row
