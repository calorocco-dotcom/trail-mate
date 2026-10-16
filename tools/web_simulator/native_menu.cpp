// Native app-shell bindings. Visual creation stays in menu_layout/menu_runtime.
// Names, icons and PIO order follow ui/app_catalog_builder.cpp.
#include "ui/app_catalog.h"
#include "ui/app_catalog_builder.h"
#include "ui/app_runtime.h"
#include "ui/menu/menu_dashboard.h"
#include "ui/menu/menu_layout.h"
#include "ui/menu/menu_runtime.h"
#include "ui/runtime/ui_feedback.h"
#include <cstdio>
#include <cstring>

extern "C" void native_open_page(int page);
namespace ui::agenda
{
class IAgendaSource;
}
ui::agenda::IAgendaSource* preview_agenda_source();
void preview_bind_controls();
void preview_bind_music();
static bool menu_created = false;
static AppScreen* active_app = nullptr;
static ui::AppCatalog catalog;
static int page_for(const char* id)
{
    if (std::strcmp(id, "music") == 0) return 19;
    const char* ids[] = {"calculator", "sky_plot", "usb_mass_storage", "walkie_talkie", "sstv", "chat", "", "", "extensions", "network", "", "settings", "contacts", "team", "tracker", "map", "energy_sweep"};
    for (int i = 0; i < 17; ++i)
        if (std::strcmp(ids[i], id) == 0) return i;
    return -1;
}

void preview_menu_enter()
{
    preview_bind_controls();
    preview_bind_music();
    if (!menu_created)
    {
        auto* previous_group = app_g;
        ui::app_catalog_builder::FeatureFlags flags;
        flags.include_usb = true;
        flags.include_network = true;
        flags.include_walkie_talkie = true;
        flags.include_power_off = true;
#if defined(ARDUINO_T_DECK)
        flags.include_sstv = false;
        flags.include_walkie_talkie = false;
#endif
        catalog = ui::app_catalog_builder::build(flags);
        ui::menu_layout::InitOptions options;
        ui::menu::dashboard::bindAgendaSource(preview_agenda_source());
        options.apps = catalog;
        ui::menu_layout::init(options);
        if (previous_group) lv_group_delete(previous_group);
        ui::menu_runtime::Hooks hooks;
        hooks.format_time = [](char* out, size_t n)
        {std::snprintf(out,n,"09:41");return true; };
        hooks.show_main_menu = []()
        { native_open_page(10); };
        ui::menu_runtime::init(lv_screen_active(), main_screen, ui::menu_layout::menuPanel(), hooks);
        menu_created = true;
    }
    lv_obj_clear_flag(main_screen, LV_OBJ_FLAG_HIDDEN);
    lv_tileview_set_tile_by_index(main_screen, 0, 0, LV_ANIM_OFF);
    ui::menu_layout::setMenuVisible(true);
    ui::menu_runtime::setScene(ui::menu_runtime::Scene::Menu);
    set_default_group(menu_g);
}
void preview_menu_exit()
{
    if (menu_created)
    {
        ui::menu::dashboard::setActive(false);
        ui::menu_runtime::setScene(ui::menu_runtime::Scene::App);
        lv_obj_add_flag(main_screen, LV_OBJ_FLAG_HIDDEN);
    }
}
bool ui_is_interruption_app_active() { return false; }
bool ui_is_overlay_active() { return false; }
AppScreen* ui_get_active_app() { return active_app; }
void ui_switch_to_app(AppScreen* app, lv_obj_t* parent)
{
    active_app = app;
    const int page = page_for(app->stable_id());
    if (page >= 0) native_open_page(page);
    else app->enter(parent);
}
void ui_clear_active_app() { active_app = nullptr; }
void ui_exit_active_app(lv_obj_t* parent)
{
    if (active_app) active_app->exit(parent);
    active_app = nullptr;
}
void menu_show() { native_open_page(10); }

void preview_power_enter()
{
    preview_menu_enter();
    for (size_t i = 0; i < ui::catalogCount(catalog); ++i)
    {
        auto* app = ui::catalogAt(catalog, i);
        if (std::strcmp(app->stable_id(), "shutdown") == 0)
        {
            app->enter(nullptr == main_screen ? lv_screen_active() : main_screen);
            return;
        }
    }
}
static lv_obj_t* cancel_button(lv_obj_t* obj)
{
    if (lv_obj_check_type(obj, &lv_label_class) && std::strcmp(lv_label_get_text(obj), "Cancel") == 0 && lv_obj_check_type(lv_obj_get_parent(obj), &lv_button_class)) return lv_obj_get_parent(obj);
    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); ++i)
        if (auto* found = cancel_button(lv_obj_get_child(obj, i))) return found;
    return nullptr;
}
void preview_power_exit()
{
    if (auto* cancel = cancel_button(lv_screen_active())) lv_obj_send_event(cancel, LV_EVENT_CLICKED, nullptr);
    preview_menu_exit();
}
