#pragma once
#include "music/artwork.h"
#include <cstddef>
#include <cstdint>

namespace music
{
constexpr std::size_t kPageRows = 8;
enum class Browse : uint8_t
{
    Songs,
    Artists,
    Albums,
    Folders,
    Playlists,
    PlaylistTracks,
    ArtistTracks,
    AlbumTracks,
    FolderTracks
};
enum class Playback : uint8_t
{
    Stopped,
    Playing,
    Paused,
    Interrupted,
    Error
};
enum class PlayMode : uint8_t
{
    RepeatAll,
    RepeatOne,
    Shuffle
};
enum class Action : uint8_t
{
    Browse,
    Play,
    TogglePause,
    Stop,
    Previous,
    Next,
    Seek,
    Volume,
    Rescan,
    CreatePlaylist,
    AddToPlaylist,
    RemoveFromPlaylist,
    DeletePlaylist,
    RenamePlaylist,
    MetadataPage,
    ScrollLyrics,
    SetPlayMode
};
struct Track
{
    uint64_t id = 0;
    char path[256]{};
    char title[96]{};
    char artist[64]{};
    char album[64]{};
    char genre[32]{};
    uint32_t duration_ms = 0;
    uint32_t audio_offset = 0;
    uint32_t lyric_offset = 0;
    uint32_t lyric_size = 0;
    uint8_t lyric_encoding = 0;
    bool estimated_duration = false;
    uint32_t picture_offset = 0, picture_size = 0;
    PictureFormat picture_format = PictureFormat::None;
    uint8_t picture_type = 0;
};
constexpr std::size_t kMetadataRows = 6;
struct MetadataField
{
    char id[5]{}, name[48]{}, value[256]{};
    bool truncated = false;
};
struct MetadataPage
{
    uint64_t track_id = 0;
    uint16_t offset = 0, total = 0;
    uint8_t count = 0;
    bool unsupported_flags = false, truncated = false;
    MetadataField fields[kMetadataRows]{};
};
struct Row
{
    uint64_t id = 0;
    char title[96]{};
    char subtitle[96]{};
    uint32_t count = 0;
};
struct Snapshot
{
    uint32_t revision = 0, library_revision = 0;
    bool storage_ready = false, audio_supported = false, busy = false;
    Playback playback = Playback::Stopped;
    PlayMode mode = PlayMode::RepeatAll;
    Browse browse = Browse::Songs;
    uint32_t offset = 0, total = 0, position_ms = 0;
    uint8_t row_count = 0, volume = 35;
    uint64_t playlist_id = 0;
    char category[96]{};
    char notice[96]{};
    Track current{};
    Row rows[kPageRows]{};
    char lyric_previous[192]{}, lyric_current[192]{}, lyric_next[192]{};
    bool has_lyrics = false, synchronized_lyrics = false;
    uint32_t artwork_revision = 0, metadata_revision = 0;
    MetadataPage metadata{};
};
struct Request
{
    Action action = Action::Browse;
    Browse browse = Browse::Songs;
    uint64_t id = 0, playlist_id = 0;
    int32_t value = 0;
    char text[96]{};
};
class IService
{
  public:
    virtual ~IService() = default;
    virtual void attach(uint8_t visible_rows) = 0;
    virtual void detach() = 0;
    // Bounded copy into caller-owned storage. Never performs SD or audio I/O.
    virtual bool snapshot(Snapshot& out) = 0;
    virtual bool send(const Request& request) = 0;
    // Zero disables artwork work while the player is hidden. Decoding runs on the service task.
    virtual void request_artwork(uint16_t edge) { (void)edge; }
    // Returns one retained, immutable PSRAM image, or nullptr. Caller must drop it.
    virtual Artwork* acquire_artwork() { return nullptr; }
    virtual void request_lyrics(bool visible) { (void)visible; }
    virtual uint8_t volume() const = 0;
};
inline bool has_session(const Snapshot& value)
{
    return value.current.id != 0 && value.playback != Playback::Stopped && value.playback != Playback::Error;
}
} // namespace music
