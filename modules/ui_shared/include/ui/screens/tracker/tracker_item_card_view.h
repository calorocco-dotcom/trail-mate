#pragma once

#include "lvgl.h"
#include <algorithm>

namespace tracker::ui::item_card_view
{
struct View
{
    lv_obj_t* root = nullptr;
    lv_obj_t* name = nullptr;
    lv_obj_t* distance = nullptr;
    lv_obj_t* region = nullptr;
    lv_obj_t* format = nullptr;
    lv_obj_t* previous = nullptr;
    lv_obj_t* counter = nullptr;
    lv_obj_t* next = nullptr;
    lv_obj_t* actions = nullptr;
};

inline void fit(lv_event_t* event)
{
    auto* root = static_cast<lv_obj_t*>(lv_event_get_target(event));
    if (lv_obj_get_child_count(root) != 5) return;
    auto* name = lv_obj_get_child(root, 0);
    auto* format = lv_obj_get_child(root, 3);
    const auto line = lv_font_get_line_height(lv_obj_get_style_text_font(name, LV_PART_MAIN));
    int fixed = 4 * lv_obj_get_style_pad_row(root, LV_PART_MAIN);
    for (int index : {1, 2, 3, 4}) fixed += lv_obj_get_height(lv_obj_get_child(root, index));
    const int available = lv_obj_get_content_height(root);
    const bool compact = available < fixed + line;
    if (compact) lv_obj_add_flag(format, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_clear_flag(format, LV_OBJ_FLAG_HIDDEN);
    if (compact) fixed -= lv_obj_get_height(format) + lv_obj_get_style_pad_row(root, LV_PART_MAIN);
    lv_obj_set_height(name, std::max<int>(line, std::min<int>(line * 2 + 2, available - fixed)));
}

inline View create(lv_obj_t* parent, const lv_font_t* body, const lv_font_t* caption,
                   const lv_font_t* heading, lv_coord_t control_height, bool dense)
{
    View view;
    view.root = lv_obj_create(parent);
    lv_obj_set_size(view.root, LV_PCT(100), 0);
    lv_obj_set_flex_grow(view.root, 1);
    lv_obj_clear_flag(view.root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(view.root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(view.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(view.root, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_bg_color(view.root, lv_color_hex(0xFFFCF5), 0);
    lv_obj_set_style_border_color(view.root, lv_color_hex(0xE7C98F), 0);
    lv_obj_set_style_border_width(view.root, 1, 0);
    lv_obj_set_style_border_color(view.root, lv_color_hex(0xEBA341), LV_STATE_FOCUSED);
    lv_obj_set_style_radius(view.root, 10, 0);
    lv_obj_set_style_pad_all(view.root, dense ? 5 : 7, 0);
    lv_obj_set_style_pad_row(view.root, dense ? 2 : 4, 0);
    auto label = [&](const lv_font_t* font, uint32_t color)
    {
        auto* result = lv_label_create(view.root);
        lv_obj_set_width(result, LV_PCT(100));
        lv_obj_set_height(result, lv_font_get_line_height(font));
        lv_obj_set_style_text_font(result, font, 0);
        lv_obj_set_style_text_color(result, lv_color_hex(color), 0);
        lv_label_set_long_mode(result, LV_LABEL_LONG_DOT);
        return result;
    };
    view.name = label(body, 0x6B4A1E);
    lv_label_set_long_mode(view.name, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_line_space(view.name, 2, 0);
    view.distance = label(heading, 0x245B50);
    view.region = label(caption, 0x416F65);
    lv_label_set_long_mode(view.region, LV_LABEL_LONG_SCROLL);
    view.format = label(caption, 0x8A6A3A);
    auto* controls = lv_obj_create(view.root);
    lv_obj_remove_style_all(controls);
    lv_obj_set_size(controls, LV_PCT(100), control_height);
    lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(controls, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(controls, 3, 0);
    lv_obj_clear_flag(controls, LV_OBJ_FLAG_SCROLLABLE);
    auto button = [&](lv_coord_t width, const char* text)
    {
        auto* object = lv_btn_create(controls);
        lv_obj_set_size(object, width, control_height);
        lv_obj_set_style_pad_all(object, 0, 0);
        lv_obj_set_style_bg_color(object, lv_color_hex(0xFAF0D8), 0);
        lv_obj_set_style_border_color(object, lv_color_hex(0xE7C98F), 0);
        lv_obj_set_style_border_width(object, 1, 0);
        lv_obj_set_style_radius(object, 7, 0);
        auto* text_label = lv_label_create(object);
        lv_obj_set_style_text_font(text_label, caption, 0);
        lv_obj_set_style_text_color(text_label, lv_color_hex(0x6B4A1E), 0);
        lv_label_set_text(text_label, text);
        lv_obj_center(text_label);
        return object;
    };
    view.previous = button(control_height, LV_SYMBOL_LEFT);
    view.counter = lv_label_create(controls);
    lv_obj_set_width(view.counter, 0);
    lv_obj_set_flex_grow(view.counter, 1);
    lv_label_set_long_mode(view.counter, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(view.counter, caption, 0);
    lv_obj_set_style_text_color(view.counter, lv_color_hex(0x8A6A3A), 0);
    lv_obj_set_style_text_align(view.counter, LV_TEXT_ALIGN_CENTER, 0);
    view.next = button(control_height, LV_SYMBOL_RIGHT);
    view.actions = button(dense ? 56 : 68, "Actions");
    lv_obj_set_style_bg_color(view.actions, lv_color_hex(0xEBA341), 0);
    lv_obj_add_event_cb(view.root, fit, LV_EVENT_SIZE_CHANGED, nullptr);
    return view;
}
} // namespace tracker::ui::item_card_view
