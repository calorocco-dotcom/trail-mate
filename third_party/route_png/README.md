# Private route PNG decoder

The route image cache uses a private compiled instance of pngle from the pinned
LovyanGFX 1.2.28 dependency. The original LovyanGFX PNG API and shared inflater
remain unchanged. Platform wrappers compile these sources; do not also compile
them as an independent library.

Source provenance:

- `lgfx_pngle.c` SHA256 before modification:
  `90a4b5e352771939ebe42af87731dc9b376f449fc4c1f4dd5a45941a97d7da42`.
- `lgfx_miniz.c` SHA256 before extraction:
  `069f08db6105969b3fa677160a66deb2a5d630e5283d8f371cccb429ed4d0926`.
  `route_inflate.c` copies its low-level inflater only, originally lines
  1390–1710, with caller-supplied produced-history validation.
- pngle's MIT notices remain in its source/header. miniz is public domain;
  the upstream statement is retained in `MINIZ-LICENSE.txt`.

Local changes:

- Prefix the private exports and use caller allocator hooks. ESP route hooks
  allocate only in PSRAM, with a bounded allocation size and no internal fallback.
- Zero decoder storage; validate match distance against produced history in
  wrapping mode, including zero/oversized distances. No undeclared dictionary
  bytes can be read for a match.
- Check Adler in the PNG caller; upstream disables it in the shared inflater.
- Require raster completion and actual zlib termination at IEND; reject extra
  raster/stream bytes and stalled decompression. Allow legal empty IDAT chunks.
- Bound source dimensions and pixel count; reject unsupported depth/header data.
- Complete tiny Adam7 passes correctly; reset filter prefix and pass rows.
- Preserve full 16-bit precision for transparent sample matching; reject invalid
  palette indices and transparent samples. Avoid signed shifts and unsigned
  guard-index underflow.

`route_png_cache.cpp` additionally validates complete PNG chunk CRCs, ordering,
lengths, critical chunk types and EOF before creating the decoder. It streams
rows into a caller-provided BGR24 cover image, composites alpha over white and
uses integer nearest-sample coordinates. It never allocates the full source
image. Source limits are 8 MiB, 8192 per dimension, 32 million pixels and 65536
chunks. Existing route cache outputs are at most 320×180.

The wrapper currently requires the pinned LovyanGFX miniz headers and checksum
exports. A build without them returns unsupported rather than allocating another
decoder. Wio L2 is the verified SDK target; this does not prove other boards.
