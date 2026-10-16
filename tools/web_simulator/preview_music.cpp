// Preview fixtures only. This file is never part of firmware or its SD library.
#include "../../modules/ui_shared/src/ui/screens/music/music_state.h"
#include "lvgl.h"
#include "music/service.h"
#include "ui/menu/menu_dashboard.h"
#include "ui/screens/music/music_page_shell.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <emscripten.h>
#include <iterator>
namespace
{
struct Song
{
    const char* title;
    const char* artist;
    const char* album;
    const char* folder;
};
constexpr Song songs[] = {
    {"After the rain", "Aster", "Small horizons", "Field recordings"},
    {"Pines at dusk", "Aster", "Small horizons", "Field recordings"},
    {"A longer way home", "Northbound", "Open country", "Trail mix"},
    {"River stones", "Northbound", "Open country", "Trail mix"},
    {"Morning window", "Linden", "Quiet rooms", "Ambient"},
    {"The last light", "Linden", "Quiet rooms", "Ambient"},
    {"Still water", "Aster", "Small horizons", "Field recordings"},
    {"Campfire radio", "Northbound", "Open country", "Trail mix"},
    {"Footsteps", "Linden", "Quiet rooms", "Ambient"},
    {"Faraway hills", "Aster", "Small horizons", "Field recordings"},
    {"Beneath the clouds", "Northbound", "Open country", "Trail mix"},
    {"Trail memo", "Unknown artist", "Unknown album", "Voice notes"}};
class Source final : public music::IService
{
    uint8_t visible = 4;
    uint32_t started = 0, base_position = 0;
    char playlist_names[8][49]{"Evening trail", "On the move"};
    uint16_t playlist_tracks[8]{3, 12};
    uint8_t playlists = 2;
    music::Artwork* art = nullptr;
    uint16_t art_edge = 0;
    void metadata(unsigned offset)
    {
        static constexpr struct
        {
            const char* id;
            const char* name;
            const char* value;
        } fields[] = {
            {"TIT2", "Title", "After the rain"}, {"TPE1", "Artist", "Aster"}, {"TALB", "Album", "Small horizons"}, {"TPE2", "Album artist", "Aster"}, {"TCOM", "Composer", "Mira Vale"}, {"TDRC", "Recording date", "2026-09-12"}, {"TRCK", "Track number", "1/12"}, {"TPOS", "Disc number", "1/1"}, {"TCON", "Genre", "Ambient"}, {"TCOP", "Copyright", "2026 Aster. Preview fixture."}, {"TENC", "Encoded by", "Trail Mate test encoder"}, {"COMM", "Comment", "Recorded beside the northern trail after the rain."}, {"APIC", "Front cover", "JPEG | 640 x 640 | type 3"}, {"USLT", "Embedded lyrics", "Available in Lyrics"}, {"FILE", "File", "/Music/Field recordings/After the rain.mp3"}};
        auto& page = out.metadata;
        page = {};
        page.track_id = out.current.id;
        page.total = std::size(fields);
        page.offset = offset;
        for (unsigned i = offset; i < std::size(fields) && page.count < music::kMetadataRows; i++)
        {
            auto& dest = page.fields[page.count++];
            std::snprintf(dest.id, sizeof(dest.id), "%s", fields[i].id);
            std::snprintf(dest.name, sizeof(dest.name), "%s", fields[i].name);
            std::snprintf(dest.value, sizeof(dest.value), "%s", fields[i].value);
        }
        ++out.metadata_revision;
    }
    void rows()
    {
        out.row_count = 0;
        const auto append = [&](uint64_t id, const char* title, const char* subtitle, uint32_t count)
        {
            if(out.row_count>=visible)return;
            auto& row=out.rows[out.row_count++];row={};row.id=id;row.count=count;
            std::snprintf(row.title,sizeof(row.title),"%s",title);std::snprintf(row.subtitle,sizeof(row.subtitle),"%s",subtitle); };
        if (out.browse == music::Browse::Playlists)
        {
            out.total = playlists;
            for (unsigned index = out.offset; index < playlists && out.row_count < visible; index++)
            {
                char count[32];
                std::snprintf(count, sizeof(count), "%u songs", playlist_tracks[index]);
                append(100 + index, playlist_names[index], count, playlist_tracks[index]);
            }
        }
        else if (out.browse == music::Browse::Artists || out.browse == music::Browse::Albums || out.browse == music::Browse::Folders)
        {
            const char* artists[] = {"Aster", "Northbound", "Linden", "Unknown artist"};
            const char* albums[] = {"Small horizons", "Open country", "Quiet rooms", "Unknown album"};
            const char* folders[] = {"Field recordings", "Trail mix", "Ambient", "Voice notes"};
            const auto* values = out.browse == music::Browse::Artists ? artists : out.browse == music::Browse::Albums ? albums
                                                                                                                      : folders;
            out.total = 4;
            for (unsigned index = out.offset; index < 4 && out.row_count < visible; index++) append(index + 1, values[index], index == 3 ? "1 song" : "4 songs", index == 3 ? 1 : 4);
        }
        else
        {
            unsigned matched = 0;
            for (unsigned index = 0; index < std::size(songs); index++)
            {
                const auto& song = songs[index];
                const bool match = out.browse == music::Browse::ArtistTracks ? std::strcmp(out.category, song.artist) == 0 : out.browse == music::Browse::AlbumTracks  ? std::strcmp(out.category, song.album) == 0
                                                                                                                         : out.browse == music::Browse::FolderTracks   ? std::strcmp(out.category, song.folder) == 0
                                                                                                                         : out.browse == music::Browse::PlaylistTracks ? index < playlist_tracks[out.playlist_id - 100]
                                                                                                                                                                       : true;
                if (!match) continue;
                if (matched++ < out.offset) continue;
                char subtitle[128];
                std::snprintf(subtitle, sizeof(subtitle), "%s  |  %s", song.artist, song.album);
                append(index + 1, song.title, subtitle, 0);
            }
            out.total = matched;
        }
        ++out.library_revision;
        ++out.revision;
    }
    void play(uint64_t id)
    {
        id = std::clamp<uint64_t>(id, 1, std::size(songs));
        out.current = {};
        out.current.id = id;
        const auto& song = songs[id - 1];
        std::snprintf(out.current.title, sizeof(out.current.title), "%s", song.title);
        std::snprintf(out.current.artist, sizeof(out.current.artist), "%s", song.artist);
        std::snprintf(out.current.album, sizeof(out.current.album), "%s", song.album);
        std::snprintf(out.current.genre, sizeof(out.current.genre), "Ambient");
        std::snprintf(out.current.path, sizeof(out.current.path), "/Music/%s/%s.mp3", song.folder, song.title);
        out.current.duration_ms = 214000;
        out.current.picture_size = 48000;
        out.current.picture_format = music::PictureFormat::Jpeg;
        out.playback = music::Playback::Playing;
        base_position = 74000;
        started = lv_tick_get();
        out.position_ms = base_position;
        out.has_lyrics = out.synchronized_lyrics = true;
        std::snprintf(out.lyric_previous, sizeof(out.lyric_previous), "The evening settles on the pines");
        std::snprintf(out.lyric_current, sizeof(out.lyric_current), "We take the quiet way home");
        std::snprintf(out.lyric_next, sizeof(out.lyric_next), "One more bend before the light is gone");
        ++out.revision;
        if (art)
        {
            art->drop();
            art = nullptr;
        }
        request_artwork(art_edge);
    }

  public:
    music::Snapshot out{};
    uint8_t volume() const override { return out.volume; }
    Source()
    {
        out.storage_ready = out.audio_supported = true;
        rows();
    }
    void attach(uint8_t count) override
    {
        visible = count;
        rows();
    }
    void detach() override { request_artwork(0); }
    void request_artwork(uint16_t edge) override
    {
        art_edge = edge;
        if (!edge)
        {
            if (art)
            {
                art->drop();
                art = nullptr;
                ++out.artwork_revision;
            }
            return;
        }
        if (!out.current.id || (art && art->track_id == out.current.id && art->width == edge)) return;
        if (art)
        {
            art->drop();
            art = nullptr;
        }
        art = music::make_artwork(out.current.id, edge, edge);
        if (art)
            for (unsigned y = 0; y < edge; y++)
                for (unsigned x = 0; x < edge; x++)
                {
                    const int px = x * 240 / edge, py = y * 240 / edge;
                    int r = 214 - py / 4, g = 219 - py / 5, b = 193 - py / 6;
                    if ((px - 173) * (px - 173) + (py - 62) * (py - 62) < 29 * 29)
                    {
                        r = 231;
                        g = 174;
                        b = 96;
                    }
                    if (py > 113 + std::abs(px - 84) / 3)
                    {
                        r = 119;
                        g = 151;
                        b = 142;
                    }
                    if (py > 153 - std::abs(px - 168) / 4)
                    {
                        r = 62;
                        g = 111;
                        b = 108;
                    }
                    if (py > 193 + std::abs(px - 55) / 5)
                    {
                        r = 35;
                        g = 78;
                        b = 80;
                    }
                    if (py > 190 && std::abs(px - (109 + (py - 190) / 3)) < 2)
                    {
                        r = 222;
                        g = 204;
                        b = 162;
                    }
                    art->pixels[y * edge + x] = static_cast<uint16_t>((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3));
                }
        ++out.artwork_revision;
    }
    music::Artwork* acquire_artwork() override
    {
        if (art) art->retain();
        return art;
    }
    bool snapshot(music::Snapshot& value) override
    {
        if (out.playback == music::Playback::Playing) out.position_ms = std::min(out.current.duration_ms, base_position + lv_tick_elaps(started));
        value = out;
        return true;
    }
    bool send(const music::Request& request) override
    {
        out.notice[0] = '\0';
        switch (request.action)
        {
        case music::Action::MetadataPage:
            metadata(std::max(0, request.value));
            break;
        case music::Action::SetPlayMode:
            out.mode = static_cast<music::PlayMode>(std::clamp(request.value, 0, 2));
            break;
        case music::Action::Browse:
            out.browse = request.browse;
            out.offset = std::max(0, request.value);
            out.playlist_id = request.id;
            std::snprintf(out.category, sizeof(out.category), "%s", request.text);
            rows();
            break;
        case music::Action::Play:
            play(request.id);
            break;
        case music::Action::TogglePause:
            if (out.playback == music::Playback::Playing)
            {
                base_position = out.position_ms;
                out.playback = music::Playback::Paused;
            }
            else if (out.current.id)
            {
                started = lv_tick_get();
                out.playback = music::Playback::Playing;
            }
            break;
        case music::Action::Stop:
            out.playback = music::Playback::Stopped;
            break;
        case music::Action::Next:
        {
            const bool paused = out.playback == music::Playback::Paused;
            play(out.current.id % std::size(songs) + 1);
            if (paused) out.playback = music::Playback::Paused;
            break;
        }
        case music::Action::Previous:
        {
            const bool paused = out.playback == music::Playback::Paused;
            play(out.current.id > 1 ? out.current.id - 1 : std::size(songs));
            if (paused) out.playback = music::Playback::Paused;
            break;
        }
        case music::Action::Seek:
            base_position = std::clamp<uint32_t>(request.value, 0, out.current.duration_ms);
            started = lv_tick_get();
            break;
        case music::Action::Volume:
            out.volume = std::clamp(request.value, 0, 100);
            break;
        case music::Action::CreatePlaylist:
            if (playlists < 8 && request.text[0])
            {
                std::snprintf(playlist_names[playlists], 49, "%.48s", request.text);
                playlist_tracks[playlists++] = request.id ? 1 : 0;
            }
            rows();
            break;
        case music::Action::AddToPlaylist:
            if (request.playlist_id >= 100 && request.playlist_id < 100 + playlists) playlist_tracks[request.playlist_id - 100]++;
            rows();
            break;
        case music::Action::Rescan:
            std::snprintf(out.notice, sizeof(out.notice), "Library up to date");
            break;
        default:
            break;
        }
        ++out.revision;
        return true;
    }
    void set_playing(int value)
    {
        if (value)
        {
            play(1);
            if (value == 2) out.playback = music::Playback::Interrupted;
        }
        else out.playback = music::Playback::Stopped;
    }
} source;
} // namespace
void preview_bind_music()
{
    ui::music_page::bind_service(&source);
    ui::menu::dashboard::bindMusicSource(&source);
}
music::IService* preview_music_service() { return &source; }
extern "C" EMSCRIPTEN_KEEPALIVE void native_preview_music_playing(int value) { source.set_playing(value); }
// QA only: inspect the actual shared LVGL objects, including slider knob clearance.
extern "C" EMSCRIPTEN_KEEPALIVE int native_music_geometry()
{
    const auto* self = ui::music_page::state();
    if (!self || !self->cover || !self->progress) return -1;
    lv_obj_update_layout(self->root);
    lv_area_t cover{}, progress{}, top{}, body{}, footer{};
    lv_obj_get_coords(lv_obj_get_parent(self->cover), &cover);
    lv_obj_get_coords(self->progress, &progress);
    lv_obj_get_coords(self->bar.container, &top);
    lv_obj_get_coords(self->body, &body);
    lv_obj_get_coords(self->footer, &footer);
    const auto knob = lv_obj_get_style_pad_top(self->progress, LV_PART_KNOB) + lv_obj_get_height(self->progress);
    if (cover.y2 + knob >= progress.y1) return 1;
    if (top.y2 >= body.y1 || body.y2 >= footer.y1) return 2;
    if (self->playback_status && lv_label_get_text(self->playback_status)[0])
    {
        lv_area_t status{}, elapsed{}, duration{}, controls{};
        lv_obj_get_coords(self->playback_status, &status);
        lv_obj_get_coords(self->elapsed, &elapsed);
        lv_obj_get_coords(self->duration, &duration);
        lv_obj_get_coords(lv_obj_get_parent(self->play_label), &controls);
        auto painted = progress;
        painted.y1 -= lv_obj_get_style_pad_top(self->progress, LV_PART_KNOB) + 1;
        painted.y2 += lv_obj_get_style_pad_bottom(self->progress, LV_PART_KNOB) + 1;
        const auto overlaps = [](const lv_area_t& a, const lv_area_t& b)
        { return a.x1 <= b.x2 && a.x2 >= b.x1 && a.y1 <= b.y2 && a.y2 >= b.y1; };
        if (overlaps(status, painted) || overlaps(status, elapsed) || overlaps(status, duration) || overlaps(status, controls)) return 3;
    }
    return 0;
}
