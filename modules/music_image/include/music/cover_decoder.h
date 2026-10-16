#pragma once
#include "music/reader.h"
#include "music/service.h"
namespace music
{
// Worker-only: decode the selected APIC region, never a whole MP3 or a full-size JPEG raster.
// Returns one owned immutable reference. It can be shared with a visible player/widget.
Artwork* decode_cover(Reader& input, const Track& track, uint16_t edge, CoverResult& result, bool (*pump)(void*) = nullptr, void* context = nullptr);
} // namespace music
