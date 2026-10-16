"""Compare ESP32-S3 internal data/BSS symbols in two saved ELF images."""
import argparse
import json
import re
import subprocess
from collections import defaultdict
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("--nm", required=True)
parser.add_argument("--before", required=True)
parser.add_argument("--after", required=True)
args = parser.parse_args()


def read_symbols(filename):
    result = subprocess.run([args.nm, "-S", "-C", "--size-sort", filename], check=True, capture_output=True, text=True)
    symbols = defaultdict(int)
    seen = set()
    for line in result.stdout.splitlines():
        match = re.match(r"^([0-9a-f]+) ([0-9a-f]+) ([bBdDsSgG]) (.+)$", line)
        if not match:
            continue
        address, size = int(match[1], 16), int(match[2], 16)
        if not 0x3FC00000 <= address < 0x3FD00000 or (address, size) in seen:
            continue
        seen.add((address, size))
        symbols[match[4]] += size
    return symbols


before = read_symbols(str(Path(args.before).resolve()))
after = read_symbols(str(Path(args.after).resolve()))
changes = [{"name": name, "before": before[name], "after": after[name], "delta": after[name] - before[name]}
           for name in before.keys() | after.keys() if before[name] != after[name]]
changes.sort(key=lambda row: abs(row["delta"]), reverse=True)
print(json.dumps({"symbolBytesBefore": sum(before.values()), "symbolBytesAfter": sum(after.values()),
                  "delta": sum(after.values()) - sum(before.values()), "changes": changes[:30]}, indent=2))
