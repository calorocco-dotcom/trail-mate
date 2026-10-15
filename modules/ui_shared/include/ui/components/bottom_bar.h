#pragma once

#include "lvgl.h"

namespace ui::components::bottom_bar
{
inline constexpr lv_coord_t kHeight = 24;
inline constexpr lv_coord_t kButtonHeight = 20;
// A page owns its widgets. These helpers only provide shared taskbar/menu chrome.
lv_obj_t* create(lv_obj_t* parent);
lv_obj_t* add_button(lv_obj_t* bar, const char* text, lv_event_cb_t callback = nullptr, void* context = nullptr);
void style_item(lv_obj_t* item);
void style_button(lv_obj_t* button);
void fit_menu(lv_obj_t* panel, lv_coord_t available_width);

struct MenuOptions
{
    bool anchor_toggles = true;
    bool handle_back = true;
};

// Reuse the page's existing filter widgets instead of retaining a second set of
// menu items. The temporary outside-click surface exists only while open.
class Menu
{
  public:
    using Changed = void (*)(void* context, bool open);
    void init(lv_obj_t* root, lv_obj_t* anchor, lv_obj_t* content,
              Changed changed = nullptr, void* context = nullptr, MenuOptions options = {});
    void reset();
    void open();
    void close();
    void toggle();
    bool is_open() const { return layer_ != nullptr; }
    lv_obj_t* button() const { return anchor_; }
    lv_obj_t* content() const { return content_; }
    bool in_submenu() const { return parent_content_ != nullptr; }
    void show_submenu(lv_obj_t* content);
    bool back();

  private:
    static void anchor_event(lv_event_t* event);
    static void outside_event(lv_event_t* event);
    static void key_event(lv_event_t* event);
    void present();
    void prepare_keys(lv_obj_t* object);
    void park(lv_obj_t* object);
    lv_obj_t* root_ = nullptr;
    lv_obj_t* anchor_ = nullptr;
    lv_obj_t* content_ = nullptr;
    lv_obj_t* parent_content_ = nullptr;
    lv_obj_t* layer_ = nullptr;
    Changed changed_ = nullptr;
    void* context_ = nullptr;
    MenuOptions options_{};
};
} // namespace ui::components::bottom_bar
