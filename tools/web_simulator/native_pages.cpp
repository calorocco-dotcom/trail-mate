#include "lvgl.h"
#include "ui/app_runtime.h"
#include "ui/menu/menu_runtime.h"
#include "ui/screens/agenda/agenda_page_shell.h"
#include "ui/screens/calculator/calculator_page_runtime.h"
#include "ui/screens/contacts/contacts_page_runtime.h"
#include "ui/screens/energy_sweep/energy_sweep_page_runtime.h"
#include "ui/screens/extensions/extensions_page_runtime.h"
#include "ui/screens/geocaching/geocaching_page_shell.h"
#include "ui/screens/gnss/gnss_skyplot_page_runtime.h"
#include "ui/screens/gps/gps_page_runtime.h"
#include "ui/screens/music/music_page_shell.h"
#include "ui/screens/network/network_page_shell.h"
#include "ui/screens/settings/settings_page_runtime.h"
#include "ui/screens/sstv/sstv_page_runtime.h"
#include "ui/screens/team/team_page_runtime.h"
#include "ui/screens/tracker/tracker_page_runtime.h"
#include "ui/screens/usb/usb_page_runtime.h"
#include "ui/screens/walkie_talkie/walkie_talkie_page_runtime.h"
#include <emscripten.h>

static int current_page = -1;
int preview_current_page() { return current_page; }
void preview_menu_enter();
void preview_menu_exit();
void preview_power_enter();
void preview_power_exit();
void preview_chat_enter(int page);
void preview_chat_exit();
void preview_bind_music();
void preview_select_page(int page);
void preview_enter_agenda(lv_obj_t* parent);

void preview_exit_page()
{
    switch (current_page)
    {
    case 20:
        geocaching::ui::shell::exit(nullptr, lv_screen_active());
        break;
    case 21:
        ui::agenda::page::exit(nullptr, lv_screen_active());
        break;
    case 19:
        ui::music_page::exit(nullptr, lv_screen_active());
        break;
    case 0:
        calculator::ui::runtime::exit(lv_screen_active());
        break;
    case 1:
        gnss::ui::runtime::exit(lv_screen_active());
        break;
    case 2:
        usb_storage::ui::runtime::exit(lv_screen_active());
        break;
    case 3:
        walkie_page::ui::runtime::exit(lv_screen_active());
        break;
    case 4:
        sstv_page::ui::runtime::exit(lv_screen_active());
        break;
    case 8:
        extensions::ui::runtime::exit(lv_screen_active());
        break;
    case 9:
        network::ui::shell::exit(nullptr, lv_screen_active());
        break;
    case 11:
        settings::ui::runtime::exit(lv_screen_active());
        break;
    case 12:
        contacts::ui::runtime::exit(lv_screen_active());
        break;
    case 13:
        team::ui::runtime::exit(lv_screen_active());
        break;
    case 14:
        tracker::ui::runtime::exit(lv_screen_active());
        break;
    case 15:
        gps::ui::runtime::exit(lv_screen_active());
        break;
    case 16:
        energy_sweep::ui::runtime::exit(lv_screen_active());
        break;
    case 17:
        preview_menu_exit();
        break;
    case 18:
        preview_power_exit();
        break;
    case 10:
        preview_menu_exit();
        break;
    case 5:
    case 6:
    case 7:
        preview_chat_exit();
        break;
    }
    current_page = -1;
}

extern "C" EMSCRIPTEN_KEEPALIVE void native_open_page(int page)
{
    preview_exit_page();
    preview_select_page(page);
    lv_group_remove_all_objs(app_g);
    lv_group_set_editing(app_g, false);
    if (page != 10 && page != 17 && page != 18) ui::menu_runtime::setScene(ui::menu_runtime::Scene::App);
    switch (page)
    {
    case 20:
    {
        static const ui::page::Host host{nullptr, [](void*)
                                         { ui_request_exit_to_menu(); }};
        geocaching::ui::shell::enter(const_cast<ui::page::Host*>(&host), lv_screen_active());
        break;
    }
    case 21:
        preview_enter_agenda(lv_screen_active());
        break;
    case 19:
        preview_bind_music();
        ui::music_page::enter(nullptr, lv_screen_active());
        break;
    case 0:
        calculator::ui::runtime::enter(nullptr, lv_screen_active());
        break;
    case 1:
        gnss::ui::runtime::enter(nullptr, lv_screen_active());
        break;
    case 2:
        usb_storage::ui::runtime::enter(nullptr, lv_screen_active());
        break;
    case 3:
        walkie_page::ui::runtime::enter(nullptr, lv_screen_active());
        break;
    case 4:
        sstv_page::ui::runtime::enter(nullptr, lv_screen_active());
        break;
    case 8:
        extensions::ui::runtime::enter(nullptr, lv_screen_active());
        break;
    case 9:
        network::ui::shell::enter(nullptr, lv_screen_active());
        break;
    case 11:
        settings::ui::runtime::enter(nullptr, lv_screen_active());
        break;
    case 12:
        contacts::ui::runtime::enter(nullptr, lv_screen_active());
        break;
    case 13:
        team::ui::runtime::enter(nullptr, lv_screen_active());
        break;
    case 14:
        tracker::ui::runtime::enter(nullptr, lv_screen_active());
        break;
    case 15:
        gps::ui::runtime::enter(nullptr, lv_screen_active());
        break;
    case 16:
        energy_sweep::ui::runtime::enter(nullptr, lv_screen_active());
        break;
    case 17:
        preview_menu_enter();
        ui::menu_runtime::handleShortcutKey('h', 1);
        break;
    case 18:
        preview_power_enter();
        break;
    case 10:
        preview_menu_enter();
        break;
    case 5:
    case 6:
    case 7:
        preview_chat_enter(page);
        break;
    default:
        return;
    }
    current_page = page;
    lv_obj_update_layout(lv_screen_active());
    lv_refr_now(nullptr);
}

void ui_request_rebuild_active_app()
{
    lv_async_call([](void*)
                  {const int page=current_page;native_open_page(page); },
                  nullptr);
}
