#!/usr/bin/env python3
"""Reject a hardware test image that does not link the HostLink service.

This is an artifact admission check, not USB or protocol acceptance. Run it
against an unstripped ELF with the matching toolchain's nm executable.
"""
import argparse
from pathlib import Path
import subprocess
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--elf", type=Path, required=True)
parser.add_argument("--nm", type=Path, required=True)
args = parser.parse_args()
if not args.elf.is_file() or not args.nm.is_file():
    parser.error("ELF and nm must both be existing files")
result = subprocess.run(
    [str(args.nm.resolve()), "--defined-only", "--demangle", str(args.elf.resolve())],
    capture_output=True, text=True, check=False,
)
if result.returncode:
    print(result.stderr, file=sys.stderr)
    sys.exit(result.returncode)
required = ("hostlink::start()", "hostlink::process_pending_commands()",
            "hostlink::management::execute_management(", "usb_cdc::start()")
missing = [symbol for symbol in required if symbol not in result.stdout]
if missing:
    print("HostLink test image rejected: missing linked symbols:", file=sys.stderr)
    for symbol in missing:
        print(f"  {symbol}", file=sys.stderr)
    sys.exit(1)
print("HostLink service symbols linked. USB readiness and hardware behavior remain unverified.")
