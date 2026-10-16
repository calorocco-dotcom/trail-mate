#pragma once
#include "music/service.h"
#include "platform/ui/psram_ui_lease.h"
#include "ui/app_runtime.h"
#include "ui/components/bottom_bar.h"
#include "ui/page/page_host.h"
#include "ui/widgets/top_bar.h"

namespace ui::music_page
{
enum class View : uint8_t
{
    Library,
    Playing,
    Lyrics,
    Details,
    NewPlaylist,
    ChoosePlaylist,
    ConfirmDelete
};
enum UiAction : intptr_t
{
    Toggle = 1,
    Previous,
    Next,
    Library,
    Lyrics,
    OpenMenu,
    Songs = 10,
    Artists,
    Albums,
    Folders,
    Playlists,
    NewPlaylist = 20,
    AddToPlaylist,
    Rescan,
    Details,
    Stop,
    VolumeDown,
    VolumeUp,
    PagePrevious,
    PageNext,
    CreatePlaylist,
    Cancel,
    DeletePlaylist,
    ConfirmDelete,
    PlayMode
};
struct State
{
    platform::ui::PsramUiLease memory;
    ::music::IService* service = nullptr;
    const ui::page::Host* host = nullptr;
    lv_obj_t* root = nullptr;
    lv_obj_t* body = nullptr;
    lv_obj_t* footer = nullptr;
    lv_obj_t* menu_content = nullptr;
    lv_obj_t* title = nullptr;
    lv_obj_t* artist = nullptr;
    lv_obj_t* album = nullptr;
    lv_obj_t* progress = nullptr;
    lv_obj_t* elapsed = nullptr;
    lv_obj_t* duration = nullptr;
    lv_obj_t* play_label = nullptr;
    lv_obj_t* cover = nullptr;
    lv_obj_t* volume = nullptr;
    lv_obj_t* playback_status = nullptr;
    lv_obj_t* mode = nullptr;
    ::music::Artwork* artwork = nullptr;
    lv_image_dsc_t cover_image{};
    lv_obj_t* lyric[3]{};
    lv_obj_t* notice = nullptr;
    lv_obj_t* editor = nullptr;
    lv_obj_t* page_number = nullptr;
    lv_timer_t* timer = nullptr;
    ui::widgets::TopBar bar{};
    ui::components::bottom_bar::Menu menu;
    ::music::Snapshot snapshot{};
    ::music::Request request{};
    View view = View::Library;
    uint64_t selected = 0, rendered_track = 0, adding_track = 0;
    uint32_t rendered_library = 0;
    uint32_t rendered_artwork = UINT32_MAX, rendered_metadata = 0;
    uint16_t artwork_edge = 0;
    uint8_t visible_rows = 4;
    bool wide = false;
    bool waiting_for_library = false;
};
State* state();
State* create_state();
void destroy_state();
void draw();
void update();
void navigate(View view);
void browse(::music::Browse kind, const char* category = "", uint64_t playlist = 0, uint32_t offset = 0);
bool command(::music::Action action, uint64_t id = 0, int32_t value = 0, const char* text = "");
void back(void*);
void action_event(lv_event_t* event);
void row_event(lv_event_t* event);
void key_event(lv_event_t* event);
void editor_ready(lv_event_t* event);
lv_obj_t* button(lv_obj_t* parent, const char* text, intptr_t action, lv_coord_t width = LV_SIZE_CONTENT);
} // namespace ui::music_page
