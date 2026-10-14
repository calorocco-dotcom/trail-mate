# Trail Mate integration changes

The vendored source is libbzip2 1.0.8. Preserve its original license and notices.

`BZ2_crc32Table` and `BZ2_rNums` have matching `const` definitions and external
declarations. Their values and the compression/decompression algorithms are
unchanged. These lookup tables are never mutated by this library. On ESP the
change allows 3072 bytes formerly linked into internal writable `.data` to be
linked as read-only data in flash. Confirm the final firmware map after builds;
object-file placement alone is not proof of the final link location.

The legacy randomized-block table is retained for decoder compatibility.
Resource decompression workspaces use the existing application PSRAM allocator;
this patch does not change their allocation or lifetime.

`tests/hostlink/bzip2_compat_cli.c` builds against the actual vendored sources.
`tests/hostlink/test_bzip2_compat.py` checks both compression directions against
Python libbz2, levels 1 and 9, both decompression memory modes, and corrupted
headers/checksums. Tests use modern streams and do not prove legacy randomized
block decoding. The values of the legacy lookup table are unchanged.
