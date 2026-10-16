#include "music/player.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
namespace music
{
Player::Player(Storage& storage, AudioOutput& output, ImageLoader& images)
    : storage_(storage), output_(output), images_(images), catalog_(storage), playlists_(storage, catalog_)
{
    state.audio_supported = output.supported();
    state.storage_ready = storage.ready();
    media_session_ = storage.session();
    catalog_.set_yield(cooperate, this);
}
Player::~Player()
{
    close_decoder();
    clear_lyrics();
    clear_artwork();
}
void Player::save_resume(ResumeState& out) const
{
    out.current = state.current;
    out.media_session = media_session_;
    out.queue = queue_;
    out.queue_kind = queue_kind_;
    out.playlist = queue_playlist_;
    copy_text(out.category, sizeof(out.category), queue_category_);
    out.position_ms = state.position_ms;
    out.queue_count = queue_count_;
    out.mode = state.mode;
    out.volume = state.volume;
}
void Player::restore_resume(const ResumeState& saved)
{
    if (!storage_.ready() || saved.media_session != storage_.session()) return;
    state.current = saved.current;
    queue_ = saved.queue;
    queue_kind_ = saved.queue_kind;
    queue_playlist_ = saved.playlist;
    copy_text(queue_category_, sizeof(queue_category_), saved.category);
    state.position_ms = saved.position_ms;
    queue_count_ = saved.queue_count;
    state.mode = saved.mode;
    state.volume = saved.volume;
    state.playback = state.current.id ? Playback::Paused : Playback::Stopped;
    loaded_ = catalog_.load();
}
void Player::close_audio()
{
    if (audio_open_)
    {
        if (decoder_)
        {
            const auto decoded = submitted_ms_;
            state.position_ms = decoded - std::min(decoded, output_.pending_ms());
        }
        output_.end();
        audio_open_ = false;
        audio_rate_ = 0;
    }
}
void Player::close_decoder()
{
    close_audio();
    pending_frame_ = {};
    destroy(decoder_);
    storage_.close(song_);
}
void Player::clear_lyrics()
{
    destroy(lyrics_);
    storage_.close(lyric_file_);
    lyric_track_ = 0;
    state.has_lyrics = state.synchronized_lyrics = false;
    state.lyric_previous[0] = state.lyric_current[0] = state.lyric_next[0] = 0;
}
void Player::clear_artwork()
{
    const bool changed = artwork_ || decoded_edge_ || artwork_track_;
    if (artwork_)
    {
        artwork_->drop();
        artwork_ = nullptr;
    }
    decoded_edge_ = 0;
    artwork_track_ = 0;
    if (changed) ++state.artwork_revision;
}
void Player::attach(uint8_t rows)
{
    rows = std::min<uint8_t>(rows, kPageRows);
    if (rows != visible_rows_)
    {
        visible_rows_ = rows;
        if (rows) refresh_library_ = true;
        else if (catalog_.scanning())
        {
            catalog_.abort_scan();
            state.busy = false;
        }
    }
}
void Player::request_artwork(uint16_t edge)
{
    const auto next = edge ? std::clamp<uint16_t>(edge, 32, 240) : 0;
    if (next == artwork_edge_) return;
    artwork_edge_ = next;
    if (!artwork_edge_) clear_artwork();
}
void Player::request_lyrics(bool visible)
{
    if (show_lyrics_ == visible) return;
    show_lyrics_ = visible;
    if (!visible) clear_lyrics();
}
bool Player::open_song()
{
    if (song_ && decoder_) return true;
    song_ = storage_.open(state.current.path, FileMode::Read);
    if (!song_) return false;
    if (!tags_.read(*song_, state.current))
    {
        storage_.close(song_);
        return false;
    }
    decoder_ = make<Decoder>();
    if (!decoder_ || !decoder_->ready() || !decoder_->open(*song_, state.current))
    {
        close_decoder();
        return false;
    }
    if (state.position_ms && !decoder_->seek(state.position_ms))
    {
        close_decoder();
        return false;
    }
    return true;
}
bool Player::select_queue(uint32_t index, bool play)
{
    intent_.pause(); // Queue lookup may cooperate; it must never reopen the old decoder.
    close_decoder();
    clear_lyrics();
    clear_artwork();
    bool found = queue_kind_ == Browse::PlaylistTracks ? playlists_.track(queue_playlist_, index, state.current) : catalog_.queue_track(queue_kind_, queue_category_, index, state.current);
    if (!found)
    {
        state.playback = Playback::Error;
        copy_text(state.notice, sizeof(state.notice), "Song missing from library");
        intent_.stop();
        return false;
    }
    state.position_ms = 0;
    state.metadata.~MetadataPage();
    new (&state.metadata) MetadataPage{};
    ++state.metadata_revision;
    if (play) intent_.play();
    state.playback = play ? (output_.blocked() ? Playback::Interrupted : Playback::Playing) : Playback::Paused;
    return true;
}
bool Player::audio_tick()
{
    if (!intent_.wanted || !state.current.id)
    {
        close_audio();
        return true;
    }
    if (output_.blocked())
    {
        // Queued PCM is discarded for a higher-priority owner. Reopen at the
        // audible position afterwards, rather than skipping the read-ahead.
        close_decoder();
        state.playback = Playback::Interrupted;
        return true;
    }
    if (audio_open_ && output_.failed())
    {
        // An asynchronous I2S failure must also surface while EOF is draining,
        // rather than looking like a completed track and silently repeating it.
        intent_.stop();
        close_decoder();
        state.playback = Playback::Error;
        copy_text(state.notice, sizeof(state.notice), "Audio output failed");
        return false;
    }
    if (!open_song())
    {
        if (storage_.last_result() == StorageResult::Busy) return true;
        intent_.stop();
        close_decoder();
        state.playback = Playback::Error;
        copy_text(state.notice, sizeof(state.notice), "Unable to open song");
        return false;
    }
    if (audio_open_ && !output_.needs_frame())
    {
        const auto decoded = submitted_ms_;
        state.position_ms = decoded - std::min(decoded, output_.pending_ms());
        return true;
    }
    PcmFrame frame = pending_frame_;
    const auto result = frame.samples ? DecodeResult::Frame : decoder_->next(frame);
    if (result == DecodeResult::End)
    {
        const bool drained = !audio_open_ || output_.drain();
        const auto decoded = submitted_ms_;
        state.position_ms = decoded - std::min(decoded, output_.pending_ms());
        // A cooperative thumbnail/catalog pump must not reenter those objects to select a song.
        if (pumping_) return false;
        if (!drained) return true;
        return select_queue(queue_.next(state.mode, true));
    }
    if (result != DecodeResult::Frame)
    {
        if (result == DecodeResult::IoError && storage_.last_result() == StorageResult::Busy) return true;
        intent_.stop();
        close_decoder();
        state.playback = Playback::Error;
        copy_text(state.notice, sizeof(state.notice), "Audio or SD read failed");
        return false;
    }
    pending_frame_ = frame; // Borrow decoder scratch until the frame is submitted.
    if (audio_open_ && audio_rate_ != frame.rate)
    {
        // Keep the first frame at the new rate while the old-rate queue drains.
        // Never decode over its scratch or play the old queue at a new clock.
        if (!output_.drain()) return true;
        close_audio();
    }
    if (!audio_open_)
    {
        // The output port publishes opening/active state around the hardware lease.
        if (output_.blocked()) return true;
        if (!output_.begin(frame.rate))
        {
            if (output_.blocked())
            {
                state.playback = Playback::Interrupted;
                return true;
            }
            intent_.pause();
            state.playback = Playback::Error;
            copy_text(state.notice, sizeof(state.notice), "Audio device is unavailable");
            return false;
        }
        audio_open_ = true;
        audio_rate_ = frame.rate;
        submitted_ms_ = state.position_ms;
    }
    if (!output_.applies_volume())
        for (unsigned i = 0; i < frame.count; i++) frame.samples[i] = static_cast<int32_t>(frame.samples[i]) * state.volume / 100;
    if (!output_.write(frame.samples, frame.count))
    {
        if (output_.blocked())
        {
            close_decoder();
            state.playback = Playback::Interrupted;
            return true;
        }
        intent_.stop();
        close_decoder();
        state.playback = Playback::Error;
        copy_text(state.notice, sizeof(state.notice), "Audio output failed");
        return false;
    }
    const auto decoded = submitted_ms_ = decoder_->position_ms();
    pending_frame_ = {};
    state.position_ms = decoded - std::min(decoded, output_.pending_ms());
    state.playback = Playback::Playing;
    return true;
}
bool Player::cooperate(void* context)
{
    auto& self = *static_cast<Player*>(context);
    if (self.pumping_) return true;
    if (!self.output_.blocked() && !self.output_.needs_frame()) return true;
    self.pumping_ = true;
    const bool ok = self.audio_tick();
    self.pumping_ = false;
    return ok;
}
bool Player::cooperate_image(void* context)
{
    auto& self = *static_cast<Player*>(context);
    return cooperate(context) && !self.output_.blocked();
}
void Player::update_library()
{
    if (!visible_rows_ || !refresh_library_ || catalog_.scanning()) return;
    const bool ok = state.browse == Browse::Playlists || state.browse == Browse::PlaylistTracks ? playlists_.browse(state, visible_rows_) : catalog_.browse(state, visible_rows_);
    if (ok)
    {
        refresh_library_ = false;
        ++state.library_revision;
    }
}
void Player::update_lyrics()
{
    if (!show_lyrics_ || !state.current.id) return;
    if (lyric_track_ != state.current.id)
    {
        clear_lyrics();
        lyric_track_ = state.current.id;
        lyrics_ = make<Lyrics>();
        if (!lyrics_) return;
        copy_text(path_, sizeof(path_), state.current.path);
        auto* dot = std::strrchr(path_, '.');
        if (dot) std::strcpy(dot, ".lrc");
        lyric_file_ = storage_.open(path_, FileMode::Read);
        if (lyric_file_ && !lyrics_->open(*lyric_file_, true))
        {
            storage_.close(lyric_file_);
        }
        if (!lyric_file_ && state.current.lyric_size)
        {
            lyric_file_ = storage_.open(state.current.path, FileMode::Read);
            if (lyric_file_ && !lyrics_->open(*lyric_file_, false, state.current.lyric_offset, state.current.lyric_size, state.current.lyric_encoding)) storage_.close(lyric_file_);
        }
        if (!lyric_file_) destroy(lyrics_);
    }
    if (lyrics_) lyrics_->update(state.position_ms, state);
}
void Player::update_artwork()
{
    if (!artwork_edge_ || !state.current.id || (artwork_track_ == state.current.id && decoded_edge_ == artwork_edge_)) return;
    auto* file = storage_.open(state.current.path, FileMode::Read);
    if (!file) return;
    CoverResult result;
    auto* art = images_.decode(*file, state.current, artwork_edge_, cooperate_image, this, result);
    storage_.close(file);
    if (result == CoverResult::Cancelled)
    {
        if (art) art->drop();
        return;
    }
    clear_artwork();
    artwork_ = art;
    artwork_track_ = state.current.id;
    decoded_edge_ = artwork_edge_;
    if (result == CoverResult::TooLarge) copy_text(state.notice, sizeof(state.notice), "Cover exceeds the image budget");
    else if (result == CoverResult::Unsupported) copy_text(state.notice, sizeof(state.notice), "Unsupported cover encoding");
    else if (result == CoverResult::OutOfMemory) copy_text(state.notice, sizeof(state.notice), "Not enough PSRAM for cover");
    ++state.artwork_revision;
}
void Player::tick()
{
    state.storage_ready = storage_.ready();
    if (!state.storage_ready || media_session_ != storage_.session())
    {
        intent_.stop();
        close_decoder();
        clear_lyrics();
        clear_artwork();
        catalog_.abort_scan();
        state.busy = false;
        state.playback = state.current.id ? Playback::Error : Playback::Stopped;
        loaded_ = false;
        media_session_ = storage_.session();
        state.row_count = 0;
        state.total = 0;
        copy_text(state.notice, sizeof(state.notice), "SD card unavailable or changed");
        ++state.revision;
        return;
    }
    if (output_.blocked())
    {
        // Yield the codec first. Optional index/thumbnail/lyric I/O waits for
        // the higher-priority owner instead of delaying a short voice session.
        audio_tick();
        ++state.revision;
        return;
    }
    if (!loaded_ && visible_rows_)
    {
        loaded_ = catalog_.load();
        if (!loaded_ && !catalog_.scanning()) catalog_.begin_scan();
    }
    if (catalog_.scanning())
    {
        state.busy = true;
        const auto result = catalog_.scan_step();
        if (result == StorageResult::End)
        {
            loaded_ = true;
            state.busy = false;
            refresh_library_ = true;
            copy_text(state.notice, sizeof(state.notice), catalog_.truncated() ? "Library indexed with limits" : "Library up to date");
        }
        else if (result != StorageResult::Ok && result != StorageResult::Busy)
        {
            state.busy = false;
            copy_text(state.notice, sizeof(state.notice), "Library scan failed");
        }
    }
    audio_tick();
    // Build an audio reserve before thumbnail/lyric/index work can stall the
    // decoder. The hardware consumer continues independently of that work.
    if (intent_.wanted && (!audio_open_ || !output_.ready_for_optional_work()))
    {
        ++state.revision;
        return;
    }
    update_library();
    update_lyrics();
    update_artwork();
    ++state.revision;
}
bool Player::command(const Request& request)
{
    if (request.action != Action::Browse && request.action != Action::MetadataPage && request.action != Action::ScrollLyrics)
        state.notice[0] = 0;
    switch (request.action)
    {
    case Action::Browse:
        state.browse = request.browse;
        state.offset = std::max<int32_t>(0, request.value);
        state.playlist_id = request.id;
        copy_text(state.category, sizeof(state.category), request.text);
        refresh_library_ = true;
        break;
    case Action::Play:
        queue_kind_ = state.browse >= Browse::PlaylistTracks ? state.browse : Browse::Songs;
        queue_playlist_ = state.playlist_id;
        copy_text(queue_category_, sizeof(queue_category_), state.category);
        queue_count_ = queue_kind_ == Browse::PlaylistTracks ? playlists_.count(queue_playlist_) : catalog_.queue_count(queue_kind_, queue_category_);
        queue_.reset(queue_count_, std::max<int32_t>(0, request.value), static_cast<uint32_t>(request.id));
        if (!queue_count_ || !select_queue(queue_.current())) return false;
        // Confirm the clicked id, rather than trusting a stale library row offset after a rescan.
        if (state.current.id != request.id)
        {
            intent_.stop();
            close_decoder();
            state.playback = Playback::Error;
            copy_text(state.notice, sizeof(state.notice), "Library changed. Select the song again.");
            return false;
        }
        break;
    case Action::TogglePause:
        if (!state.current.id) return false;
        if (intent_.wanted)
        {
            intent_.pause();
            state.playback = Playback::Paused;
            close_decoder();
        }
        else
        {
            intent_.play();
            state.playback = output_.blocked() ? Playback::Interrupted : Playback::Playing;
        }
        break;
    case Action::Stop:
        intent_.stop();
        close_decoder();
        clear_lyrics();
        clear_artwork();
        state.current = {};
        state.position_ms = 0;
        state.playback = Playback::Stopped;
        break;
    case Action::Previous:
        if (!queue_count_) return false;
        return select_queue(queue_.previous(state.mode), intent_.wanted);
    case Action::Next:
        if (!queue_count_) return false;
        return select_queue(queue_.next(state.mode, false), intent_.wanted);
    case Action::Seek:
        close_audio();
        pending_frame_ = {};
        state.position_ms = std::clamp<uint32_t>(std::max<int32_t>(0, request.value), 0, state.current.duration_ms);
        if (decoder_) decoder_->seek(state.position_ms);
        break;
    case Action::Volume:
        state.volume = std::clamp<int32_t>(request.value, 0, 100);
        break;
    case Action::SetPlayMode:
        state.mode = static_cast<PlayMode>(std::clamp<int32_t>(request.value, 0, 2));
        break;
    case Action::Rescan:
        if (intent_.wanted)
        {
            copy_text(state.notice, sizeof(state.notice), "Pause music before scanning");
            return false;
        }
        close_decoder();
        if (!catalog_.begin_scan()) return false;
        state.busy = true;
        break;
    case Action::CreatePlaylist:
    {
        uint64_t id = 0;
        if (!playlists_.create(request.text, request.id, id)) return false;
        refresh_library_ = true;
        break;
    }
    case Action::AddToPlaylist:
        if (!playlists_.add(request.playlist_id, request.id)) return false;
        refresh_library_ = true;
        break;
    case Action::RemoveFromPlaylist:
        if (!playlists_.remove(request.playlist_id, request.id)) return false;
        refresh_library_ = true;
        break;
    case Action::DeletePlaylist:
        if (!playlists_.erase(request.id)) return false;
        refresh_library_ = true;
        break;
    case Action::RenamePlaylist:
        if (!playlists_.rename(request.id, request.text)) return false;
        refresh_library_ = true;
        break;
    case Action::MetadataPage:
    {
        if (request.id != state.current.id) return false;
        auto* file = storage_.open(state.current.path, FileMode::Read);
        if (!file) return false;
        const bool ok = tags_.read(*file, state.current, &state.metadata, std::clamp<int32_t>(request.value, 0, 512));
        storage_.close(file);
        if (!ok) return false;
        ++state.metadata_revision;
        break;
    }
    case Action::ScrollLyrics:
        if (lyrics_) lyrics_->scroll(request.value);
        break;
    }
    ++state.revision;
    return true;
}
} // namespace music
