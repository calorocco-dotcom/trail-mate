#include "ui/components/dialog_frame.h"

#include "ui/widgets/top_bar.h"
#include <algorithm>

namespace ui::components::dialog_frame
{
namespace
{
void close_key(lv_event_t* event)
{
    if (lv_event_get_key(event) != LV_KEY_ENTER) return;
    lv_event_stop_bubbling(event);
    lv_event_stop_processing(event);
    lv_obj_send_event(static_cast<lv_obj_t*>(lv_event_get_target(event)), LV_EVENT_CLICKED, nullptr);
}
} // namespace
Frame create(lv_obj_t* parent, const Config& config)
{
    Frame frame{};
    frame.overlay = lv_obj_create(parent);
    lv_obj_remove_style_all(frame.overlay);
    lv_obj_set_size(frame.overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(frame.overlay, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(frame.overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(frame.overlay, lv_color_hex(0x1C1812), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(frame.overlay, LV_OPA_70, LV_PART_MAIN);
    lv_obj_clear_flag(frame.overlay, LV_OBJ_FLAG_SCROLLABLE);
    frame.panel = lv_obj_create(frame.overlay);
    lv_obj_set_size(frame.panel, std::min<lv_coord_t>(config.width, lv_obj_get_width(parent) - 12),
                    std::min<lv_coord_t>(config.height, lv_obj_get_height(parent) - 12));
    lv_obj_center(frame.panel);
    lv_obj_set_flex_flow(frame.panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(frame.panel, lv_color_hex(0xFFF3DF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(frame.panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(frame.panel, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(frame.panel, lv_color_hex(0x8A6E43), LV_PART_MAIN);
    lv_obj_set_style_radius(frame.panel, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(frame.panel, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(frame.panel, 0, LV_PART_MAIN);
    lv_obj_clear_flag(frame.panel, LV_OBJ_FLAG_SCROLLABLE);
    ::ui::widgets::top_bar_create_dialog_header(frame.panel, config.title, config.on_close, config.context, &frame.close);
    lv_obj_add_event_cb(frame.close, close_key, LV_EVENT_KEY, nullptr);
    frame.body = lv_obj_create(frame.panel);
    lv_obj_remove_style_all(frame.body);
    lv_obj_set_size(frame.body, LV_PCT(100), 0);
    lv_obj_set_flex_grow(frame.body, 1);
    lv_obj_set_flex_flow(frame.body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(frame.body, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_row(frame.body, 4, LV_PART_MAIN);
    lv_obj_add_flag(frame.body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(frame.body, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(frame.body, LV_SCROLLBAR_MODE_AUTO);
    if (config.on_key)
        for (auto* object : {frame.overlay, frame.panel, frame.body, frame.close})
            lv_obj_add_event_cb(object, config.on_key, LV_EVENT_KEY, config.context);
    return frame;
}
} // namespace ui::components::dialog_frame
