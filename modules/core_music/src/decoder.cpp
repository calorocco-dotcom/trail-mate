#include "music/decoder.h"
#include "music/memory.h"
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#define MINIMP3_EXTERNAL_SCRATCH
#include "../../../third_party/minimp3/minimp3.h"
#undef scratch
#include <algorithm>
#include <cstring>
namespace music
{
struct Decoder::Workspace
{
    Reader* input = nullptr;
    Track* metadata = nullptr;
    mp3dec_t decoder{};
    mp3dec_scratch_t scratch{};
    mp3dec_frame_info_t info{};
    uint8_t input_bytes[8192]{};
    int16_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME]{};
    uint32_t offset = 0, used = 0, audio_bytes = 0;
    uint64_t position_us = 0;
    uint8_t toc[100]{};
    bool has_toc = false, probed = false;
};
Decoder::Decoder() : state_(make<Workspace>()) {}
Decoder::~Decoder() { destroy(state_); }
std::size_t Decoder::workspace_bytes() { return sizeof(Workspace); }
bool Decoder::open(Reader& input, Track& metadata)
{
    if (!state_ || metadata.audio_offset >= input.size()) return false;
    state_->~Workspace();
    new (state_) Workspace{};
    state_->input = &input;
    state_->metadata = &metadata;
    state_->offset = metadata.audio_offset;
    state_->audio_bytes = input.size() - metadata.audio_offset;
    mp3dec_init(&state_->decoder);
    return true;
}
DecodeResult Decoder::next(PcmFrame& out)
{
    out = {};
    if (!state_ || !state_->input) return DecodeResult::Invalid;
    auto& self = *state_;
    // Limit garbage scanning per iteration so storage corruption cannot starve focus/stop commands.
    for (unsigned attempt = 0; attempt < 4; attempt++)
    {
        // Refill in batches from the existing PSRAM window. Topping it up after
        // every MP3 frame turns sequential playback into small SD seek/reads.
        // Half a window still exceeds the decoder's largest supported frame.
        if (self.used < sizeof(self.input_bytes) / 2 && self.offset < self.input->size())
        {
            const auto wanted = std::min<uint32_t>(sizeof(self.input_bytes) - self.used, self.input->size() - self.offset);
            const auto count = self.input->read_at(self.offset, self.input_bytes + self.used, wanted);
            if (!count) return DecodeResult::IoError;
            self.offset += count;
            self.used += count;
        }
        if (!self.used) return DecodeResult::End;
        const int count = mp3dec_decode_frame(&self.decoder, self.input_bytes, self.used, self.pcm, &self.info, &self.scratch);
        if (count > 0)
        {
            if (self.info.hz < 8000 || self.info.hz > 48000 || (self.info.channels != 1 && self.info.channels != 2) || self.info.layer != 3) return DecodeResult::Invalid;
            if (!self.probed)
            {
                self.probed = true;
                const auto* header = self.input_bytes + self.info.frame_offset;
                const bool version1 = ((header[1] >> 3) & 3) == 3;
                const unsigned side = version1 ? (self.info.channels == 1 ? 17 : 32) : (self.info.channels == 1 ? 9 : 17);
                const unsigned at = self.info.frame_offset + 4 + ((header[1] & 1) ? 0 : 2) + side;
                auto big = [](const uint8_t* p)
                { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; };
                if (at + 12 <= static_cast<unsigned>(self.info.frame_bytes) && (!std::memcmp(self.input_bytes + at, "Xing", 4) || !std::memcmp(self.input_bytes + at, "Info", 4)))
                {
                    const auto flags = big(self.input_bytes + at + 4);
                    unsigned cursor = at + 8;
                    if (flags & 1)
                    {
                        const auto frames = big(self.input_bytes + cursor);
                        cursor += 4;
                        const uint64_t duration = uint64_t(frames) * count * 1000 / self.info.hz;
                        if (duration <= UINT32_MAX)
                        {
                            self.metadata->duration_ms = duration;
                            self.metadata->estimated_duration = false;
                        }
                    }
                    if ((flags & 2) && cursor + 4 <= static_cast<unsigned>(self.info.frame_bytes))
                    {
                        self.audio_bytes = std::min(self.audio_bytes, big(self.input_bytes + cursor));
                        cursor += 4;
                    }
                    if ((flags & 4) && cursor + 100 <= static_cast<unsigned>(self.info.frame_bytes))
                    {
                        std::memcpy(self.toc, self.input_bytes + cursor, 100);
                        self.has_toc = true;
                    }
                }
                else if (!self.metadata->duration_ms && self.info.bitrate_kbps > 0)
                {
                    self.metadata->duration_ms = std::min<uint64_t>(UINT32_MAX, uint64_t(self.audio_bytes) * 8 / self.info.bitrate_kbps);
                    self.metadata->estimated_duration = true;
                }
            }
            if (self.info.channels == 2)
                for (int index = 0; index < count; index++) self.pcm[index] = static_cast<int16_t>((int32_t(self.pcm[index * 2]) + self.pcm[index * 2 + 1]) / 2);
            self.position_us += uint64_t(count) * 1000000 / self.info.hz;
            out = {self.pcm, static_cast<uint16_t>(count), static_cast<uint32_t>(self.info.hz)};
        }
        const auto consumed = std::clamp(self.info.frame_bytes, 0, static_cast<int>(self.used));
        if (!consumed) return DecodeResult::Invalid;
        self.used -= consumed;
        std::memmove(self.input_bytes, self.input_bytes + consumed, self.used);
        if (count > 0) return DecodeResult::Frame;
    }
    return DecodeResult::Invalid;
}
bool Decoder::seek(uint32_t milliseconds)
{
    if (!state_ || !state_->metadata || !state_->metadata->duration_ms) return false;
    auto& self = *state_;
    milliseconds = std::min(milliseconds, self.metadata->duration_ms);
    uint64_t offset = uint64_t(self.audio_bytes) * milliseconds / self.metadata->duration_ms;
    if (self.has_toc)
    {
        const uint64_t percent = uint64_t(milliseconds) * 10000 / self.metadata->duration_ms;
        const unsigned index = std::min<uint64_t>(percent / 100, 99), fraction = percent % 100;
        const unsigned a = self.toc[index], b = index == 99 ? 256 : self.toc[index + 1];
        offset = uint64_t(self.audio_bytes) * (a * 100 + (b - a) * fraction) / 25600;
    }
    self.offset = self.metadata->audio_offset + std::min<uint64_t>(offset, self.audio_bytes);
    self.used = 0;
    self.position_us = uint64_t(milliseconds) * 1000;
    mp3dec_init(&self.decoder);
    return true;
}
uint32_t Decoder::position_ms() const { return state_ ? state_->position_us / 1000 : 0; }
} // namespace music
