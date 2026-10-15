"""Read current UTF-8 and historical BOM-marked UTF-16 changelogs for CI."""

import argparse
from pathlib import Path
import re
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("changelog", nargs="?", type=Path, default=Path("CHANGELOG.md"))
    parser.add_argument("--version", action="store_true", help="Print the first version heading only")
    args = parser.parse_args()
    raw = args.changelog.read_bytes()
    text = raw.decode("utf-16" if raw.startswith((b"\xff\xfe", b"\xfe\xff")) else "utf-8-sig")
    text = text.replace("\r\r\n", "\n").replace("\r\n", "\n")
    if args.version:
        match = re.search(r"^## \[([0-9][^\]]*)\]", text, re.MULTILINE)
        if not match:
            parser.error("Changelog has no version heading")
        print(match.group(1).strip())
    else:
        sys.stdout.write(text)


if __name__ == "__main__":
    main()
