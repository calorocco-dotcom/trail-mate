#pragma once
#include "music/catalog.h"
#include "music/decoder.h"
#include "music/lyrics.h"
#include "music/playback_policy.h"
#include "music/playlists.h"
namespace music
{
class AudioOutput
{
  public:
    virtual ~AudioOutput() = default;
    virtual bool supported() const = 0;
    virtual bool blocked() const = 0;
    virtual bool needs_frame() const { return true; }
    virtual bool begin(uint32_t sample_rate) = 0;
    virtual bool write(const int16_t* mono, std::size_t count) = 0;
    virtual void end() = 0;
};
class ImageLoader
{
  public:
    virtual ~ImageLoader() = default;
    virtual Artwork* decode(Reader& input, const Track& track, uint16_t edge, bool (*pump)(void*), void* context, CoverResult& result) = 0;
};
struct ResumeState
{
    Track current{};
    QueueCursor queue;
    Browse queue_kind = Browse::Songs;
    uint64_t playlist = 0;
    char category[96]{};
    uint32_t position_ms = 0, queue_count = 0, media_session = 0;
    PlayMode mode = PlayMode::RepeatAll;
    uint8_t volume = 35;
};
// Single worker owner. The ESP proxy publishes copies and sends bounded commands;
// it never calls these methods from LVGL or another audio owner.
class Player
{
    Storage& storage_;
    AudioOutput& output_;
    ImageLoader& images_;
    Catalog catalog_;
    Playlists playlists_;
    TagReader tags_;
    Decoder* decoder_ = nullptr;
    Lyrics* lyrics_ = nullptr;
    File *song_ = nullptr, *lyric_file_ = nullptr;
    Artwork* artwork_ = nullptr;
    PlaybackIntent intent_;
    QueueCursor queue_;
    Browse queue_kind_ = Browse::Songs;
    uint64_t queue_playlist_ = 0;
    char queue_category_[96]{}, path_[320]{};
    uint32_t queue_count_ = 0, media_session_ = 0, audio_rate_ = 0;
    uint16_t artwork_edge_ = 0, decoded_edge_ = 0;
    uint8_t visible_rows_ = 0;
    bool show_lyrics_ = false, audio_open_ = false, loaded_ = false, pumping_ = false;
    bool refresh_library_ = true;
    uint64_t lyric_track_ = 0, artwork_track_ = 0;
    void close_audio();
    void close_decoder();
    void clear_lyrics();
    void clear_artwork();
    bool open_song();
    bool audio_tick();
    bool select_queue(uint32_t index, bool play = true);
    void update_library();
    void update_lyrics();
    void update_artwork();
    static bool cooperate(void* context);
    static bool cooperate_image(void* context);

  public:
    Snapshot state{};
    Player(Storage& storage, AudioOutput& output, ImageLoader& images);
    ~Player();
    void attach(uint8_t rows);
    void request_artwork(uint16_t edge);
    void request_lyrics(bool visible);
    bool command(const Request& request);
    void tick();
    void save_resume(ResumeState& out) const;
    void restore_resume(const ResumeState& saved);
    bool active() const { return intent_.wanted || catalog_.scanning(); }
    bool audio_open() const { return audio_open_; }
    Artwork* artwork() const { return artwork_; } // Worker-owned; proxy retains under its publication lock.
};
} // namespace music
