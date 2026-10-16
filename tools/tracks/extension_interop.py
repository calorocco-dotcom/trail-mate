"""Public fixture stdin bridge for JS/Python/native extension interoperability."""
import sys
from tools.tracks.attachment_index import match_index
from tools.tracks.protocol import verify_manifest, verify_successor


def main():
    lines = sys.stdin.read().splitlines()
    mode, raw = lines[0], [bytes.fromhex(line) for line in lines[1:]]
    if mode == "INDEX":
        record = match_index(raw[0], raw[1:3])
        print(f"{len(record[3])},{sum(row[6] for row in record[3])},{record[2]+len(raw[0])+sum(row[2] for row in record[3])}")
    elif mode == "VERIFY":
        proof = verify_manifest(raw[:2])
        print(proof["object_id"].hex())
        print(proof["revision_hash"].hex())
    elif mode == "CHAIN":
        verify_successor(raw[:2], raw[2:4])
    else:
        raise ValueError("interop mode")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, IndexError):
        sys.exit(1)
