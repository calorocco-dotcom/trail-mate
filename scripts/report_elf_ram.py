"""Report allocated RAM sections and their symbols without counting flash/dummy regions."""

import argparse
import hashlib
import json
import re
from pathlib import Path

from elftools.elf.elffile import ELFFile


def ram_domain(name):
    if "dummy" in name or "heap_start" in name:
        return None
    if name.startswith((".ext_ram", ".extmem")):
        return "psram"
    if name.startswith((".dram", ".noinit")) or name in (".data", ".bss", ".sdata", ".sbss"):
        return "internal_data"
    if name.startswith((".rtc", ".tcm")):
        return "rtc_tcm"
    if name.startswith(".iram"):
        return "internal_code"
    return None


def report(path):
    with path.open("rb") as stream:
        elf = ELFFile(stream)
        sections = {index: section for index, section in enumerate(elf.iter_sections())
                    if section["sh_flags"] & 2 and ram_domain(section.name)}
        totals = {}
        section_rows = []
        for index, section in sections.items():
            domain = ram_domain(section.name)
            size = section["sh_size"]
            totals[domain] = totals.get(domain, 0) + size
            section_rows.append({"name": section.name, "domain": domain, "bytes": size,
                                 "address": hex(section["sh_addr"])})
        symbols = []
        table = elf.get_section_by_name(".symtab")
        if table is None:
            raise ValueError("ELF has no symbol table")
        for symbol in table.iter_symbols():
            section = sections.get(symbol["st_shndx"])
            if section is not None and symbol["st_size"]:
                symbols.append({"name": symbol.name, "section": section.name,
                                "domain": ram_domain(section.name), "bytes": symbol["st_size"]})
        symbols.sort(key=lambda row: row["bytes"], reverse=True)
    return {"schema": "trail-mate.ram.v1", "elf_sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            "totals": totals, "sections": section_rows, "symbols": symbols}


def memory_config(path):
    prefixes = ("CONFIG_SPIRAM", "CONFIG_ESP_WIFI_", "CONFIG_MBEDTLS_", "CONFIG_LWIP_",
                "CONFIG_LV_USE_", "CONFIG_LV_MEM_", "CONFIG_ESP_MAIN_TASK_STACK_SIZE")
    result = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        match = re.fullmatch(r"(CONFIG_\w+)=(.*)", line)
        disabled = re.fullmatch(r"# (CONFIG_\w+) is not set", line)
        if match and match[1].startswith(prefixes):
            result[match[1]] = match[2]
        elif disabled and disabled[1].startswith(prefixes):
            result[disabled[1]] = "n"
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--revision", default="unknown")
    parser.add_argument("--sdkconfig", type=Path, help="Resolved IDF sdkconfig, not defaults")
    parser.add_argument("--expect-config", action="append", default=[], metavar="CONFIG_NAME=value")
    args = parser.parse_args()
    result = report(args.elf)
    result["revision"] = args.revision
    if args.sdkconfig:
        result["memory_config"] = memory_config(args.sdkconfig)
    for expected in args.expect_config:
        key, value = expected.split("=", 1)
        actual = result.get("memory_config", {}).get(key)
        if actual != value:
            parser.error(f"Resolved {key}: expected {value}, got {actual!r}")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(f"RAM report ({args.revision}): {json.dumps(result['totals'], sort_keys=True)}")
    for symbol in [row for row in result["symbols"] if row["domain"] == "internal_data"][:20]:
        print(f"  {symbol['bytes']:8d} {symbol['section']:16s} {symbol['name']}")


if __name__ == "__main__":
    main()
