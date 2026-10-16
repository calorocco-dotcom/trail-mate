# minimp3 provenance

Upstream: https://github.com/lieff/minimp3

Pinned revision: `ea99364f61c14656440e8d77e9c233ccf3124633`.
The upstream CC0 dedication is retained in `LICENSE` and the header.

Trail Mate adds the optional `MINIMP3_EXTERNAL_SCRATCH` parameter to
`mp3dec_decode_frame`. With the flag, its scratch object is caller-owned;
without it, the upstream API and storage behavior remain intact. The music
adapter allocates the decoder, scratch, input window and PCM in strict PSRAM.
The feature builds only Layer III and disables SIMD. No minimp3_ex file,
full-song buffer, mixer, cover cache, network client or extra decoder task is
included.
