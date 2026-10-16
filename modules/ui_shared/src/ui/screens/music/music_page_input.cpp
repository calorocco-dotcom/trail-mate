#include "music_state.h"
#include "ui/app_runtime.h"
#include "ui/ui_common.h"
#include <algorithm>
#include <cstdio>

namespace ui::music_page
{
bool command(::music::Action action, uint64_t id, int32_t value, const char* text)
{
    auto& self = *state();
    if (!self.service) return false;
    self.request = {};
    self.request.action = action;
    self.request.id = id;
    self.request.value = value;
    self.request.browse = self.snapshot.browse;
    self.request.playlist_id = self.snapshot.playlist_id;
    std::snprintf(self.request.text, sizeof(self.request.text), "%s", text);
    if (!self.service->send(self.request))
    {
        std::snprintf(self.snapshot.notice, sizeof(self.snapshot.notice), "Music is busy. Try again.");
        update();
        return false;
    }
    self.service->snapshot(self.snapshot);
    if (self.view == View::Details && self.rendered_track != self.snapshot.current.id) draw();
    else update();
    return true;
}
void navigate(View view)
{
    auto& self = *state();
    self.menu.close();
    self.view = view;
    draw();
}
void browse(::music::Browse kind, const char* category, uint64_t playlist, uint32_t offset)
{
    auto& self = *state();
    if (!self.service) return;
    self.request = {};
    self.request.action = ::music::Action::Browse;
    self.request.browse = kind;
    self.request.id = playlist;
    self.request.value = static_cast<int32_t>(offset);
    std::snprintf(self.request.text, sizeof(self.request.text), "%s", category);
    self.selected = 0;
    self.waiting_for_library = true;
    if (!self.service->send(self.request))
    {
        self.waiting_for_library = false;
        std::snprintf(self.snapshot.notice, sizeof(self.snapshot.notice), "Music is busy. Try again.");
        update();
        return;
    }
    // Immediate preview/inline sources may already have completed the request.
    const auto revision = self.snapshot.library_revision;
    if (self.service->snapshot(self.snapshot) && self.snapshot.library_revision != revision) self.waiting_for_library = false;
    if (self.waiting_for_library)
    {
        self.snapshot.row_count = 0;
        self.snapshot.busy = true;
    }
    draw();
}
void back(void*)
{
    auto* self = state();
    if (!self) return;
    if (self->menu.is_open())
    {
        self->menu.close();
        return;
    }
    switch (self->view)
    {
    case View::Lyrics:
    case View::Details:
        navigate(View::Playing);
        return;
    case View::NewPlaylist:
        self->view = self->adding_track ? View::ChoosePlaylist : View::Library;
        browse(::music::Browse::Playlists);
        return;
    case View::ConfirmDelete:
        navigate(View::Library);
        return;
    case View::ChoosePlaylist:
        self->adding_track = 0;
        self->view = View::Library;
        browse(::music::Browse::Songs);
        return;
    case View::Library:
        if (self->snapshot.browse >= ::music::Browse::PlaylistTracks)
        {
            browse(::music::Browse::Songs);
            return;
        }
        break;
    default:
        break;
    }
    if (self->host) ui::page::request_exit(self->host);
    else ui_request_exit_to_menu();
}
void editor_ready(lv_event_t*)
{
    auto& self = *state();
    if (!self.editor) return;
    if (!command(::music::Action::CreatePlaylist, self.adding_track, 0, lv_textarea_get_text(self.editor))) return;
    self.adding_track = 0;
    self.view = View::Library;
    browse(::music::Browse::Playlists);
}
void action_event(lv_event_t* event)
{
    auto* self = state();
    if (!self) return;
    const auto action = reinterpret_cast<intptr_t>(lv_event_get_user_data(event));
    if (action == 99)
    {
        if (self->progress && self->snapshot.current.duration_ms)
            command(::music::Action::Seek, 0, static_cast<int64_t>(lv_slider_get_value(self->progress)) * self->snapshot.current.duration_ms / 1000);
        return;
    }
    switch (action)
    {
    case PlayMode:
        command(::music::Action::SetPlayMode, 0, (static_cast<unsigned>(self->snapshot.mode) + 1) % 3);
        break;
    case Toggle:
        if (self->view != View::Playing) navigate(View::Playing);
        else command(::music::Action::TogglePause);
        break;
    case Previous:
        command(::music::Action::Previous);
        break;
    case Next:
        command(::music::Action::Next);
        break;
    case Library:
        if (self->view == View::Library || self->view == View::ChoosePlaylist) self->menu.toggle();
        else
        {
            self->view = View::Library;
            browse(::music::Browse::Songs);
        }
        break;
    case Lyrics:
        navigate(View::Lyrics);
        break;
    case OpenMenu:
        // bottom_bar::Menu owns its anchor's click callback.
        break;
    case Songs:
    case Artists:
    case Albums:
    case Folders:
    case Playlists:
        self->view = View::Library;
        self->adding_track = 0;
        browse(static_cast<::music::Browse>(action - Songs));
        break;
    case NewPlaylist:
        navigate(View::NewPlaylist);
        break;
    case AddToPlaylist:
    {
        const auto index = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(lv_event_get_target_obj(event)));
        self->adding_track = self->view == View::Playing ? self->snapshot.current.id : index && index <= self->snapshot.row_count ? self->snapshot.rows[index - 1].id
                                                                                                                                  : self->selected;
        self->view = View::ChoosePlaylist;
        browse(::music::Browse::Playlists);
        break;
    }
    case Details:
        command(::music::Action::MetadataPage, self->snapshot.current.id);
        if (self->service) self->service->snapshot(self->snapshot);
        navigate(View::Details);
        break;
    case Rescan:
        command(::music::Action::Rescan);
        self->menu.close();
        break;
    case Stop:
        command(::music::Action::Stop);
        self->view = View::Library;
        browse(::music::Browse::Songs);
        break;
    case VolumeDown:
    case VolumeUp:
        command(::music::Action::Volume, 0, std::clamp<int>(self->snapshot.volume + (action == VolumeUp ? 5 : -5), 0, 100));
        self->menu.close();
        break;
    case PagePrevious:
    case PageNext:
    {
        if (self->view == View::Details)
        {
            const int32_t offset = std::clamp<int32_t>(static_cast<int32_t>(self->snapshot.metadata.offset) + (action == PageNext ? ::music::kMetadataRows : -static_cast<int32_t>(::music::kMetadataRows)), 0, self->snapshot.metadata.total ? ((self->snapshot.metadata.total - 1) / ::music::kMetadataRows) * ::music::kMetadataRows : 0);
            command(::music::Action::MetadataPage, self->snapshot.current.id, offset);
            return;
        }
        const int32_t offset = std::clamp<int32_t>(static_cast<int32_t>(self->snapshot.offset) + (action == PageNext ? self->visible_rows : -self->visible_rows), 0,
                                                   self->snapshot.total ? ((self->snapshot.total - 1) / self->visible_rows) * self->visible_rows : 0);
        browse(self->snapshot.browse, self->snapshot.category, self->snapshot.playlist_id, offset);
        break;
    }
    case CreatePlaylist:
        editor_ready(nullptr);
        break;
    case Cancel:
        back(nullptr);
        break;
    case DeletePlaylist:
        navigate(View::ConfirmDelete);
        break;
    case ConfirmDelete:
        command(::music::Action::DeletePlaylist, self->snapshot.playlist_id);
        self->view = View::Library;
        browse(::music::Browse::Playlists);
        break;
    default:
        break;
    }
}
void row_event(lv_event_t* event)
{
    auto& self = *state();
    const auto index = reinterpret_cast<uintptr_t>(lv_event_get_user_data(event));
    if (index >= self.snapshot.row_count) return;
    const auto& row = self.snapshot.rows[index];
    self.selected = row.id;
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || self.menu.is_open()) return;
    if (self.view == View::ChoosePlaylist)
    {
        self.snapshot.playlist_id = row.id;
        if (!command(::music::Action::AddToPlaylist, self.adding_track)) return;
        self.adding_track = 0;
        self.view = View::Library;
        browse(::music::Browse::Playlists);
        return;
    }
    using Browse = ::music::Browse;
    switch (self.snapshot.browse)
    {
    case Browse::Artists:
        browse(Browse::ArtistTracks, row.title);
        break;
    case Browse::Albums:
        browse(Browse::AlbumTracks, row.title);
        break;
    case Browse::Folders:
        browse(Browse::FolderTracks, row.title);
        break;
    case Browse::Playlists:
        browse(Browse::PlaylistTracks, row.title, row.id);
        break;
    default:
        if (!command(::music::Action::Play, row.id, static_cast<int32_t>(self.snapshot.offset + index))) return;
        navigate(View::Playing);
        break;
    }
}
void key_event(lv_event_t* event)
{
    auto* self = state();
    if (!self) return;
    const auto key = lv_event_get_key(event);
    if (key == LV_KEY_ESC || key == LV_KEY_BACKSPACE)
    {
        back(nullptr);
        lv_event_stop_processing(event);
        return;
    }
    if (self->menu.is_open()) return;
    if (key == ' ')
    {
        command(::music::Action::TogglePause);
        return;
    }
    if (key == 'l' || key == 'L')
    {
        navigate(View::Lyrics);
        return;
    }
    if (key == 'b' || key == 'B')
    {
        self->view = View::Library;
        browse(::music::Browse::Songs);
        return;
    }
    if (self->view == View::Playing)
    {
        if (key == 'a' || key == LV_KEY_LEFT) command(::music::Action::Previous);
        else if (key == 'd' || key == LV_KEY_RIGHT) command(::music::Action::Next);
        else if (key == 'w' || key == LV_KEY_UP) command(::music::Action::Volume, 0, std::min(100, static_cast<int>(self->snapshot.volume) + 5));
        else if (key == 's' || key == LV_KEY_DOWN) command(::music::Action::Volume, 0, std::max(0, static_cast<int>(self->snapshot.volume) - 5));
    }
    else if (self->view == View::Details && (key == 'w' || key == LV_KEY_UP || key == 's' || key == LV_KEY_DOWN))
        lv_obj_scroll_by(lv_event_get_target_obj(event), 0, (key == 'w' || key == LV_KEY_UP) ? 40 : -40, LV_ANIM_OFF);
    else if (self->view == View::Lyrics && !self->snapshot.synchronized_lyrics && (key == 'w' || key == LV_KEY_UP || key == 's' || key == LV_KEY_DOWN))
        command(::music::Action::ScrollLyrics, 0, (key == 'w' || key == LV_KEY_UP) ? -1 : 1);
    else if (key == 'w' || key == LV_KEY_UP) lv_group_focus_prev(app_g);
    else if (key == 's' || key == LV_KEY_DOWN) lv_group_focus_next(app_g);
}
} // namespace ui::music_page
