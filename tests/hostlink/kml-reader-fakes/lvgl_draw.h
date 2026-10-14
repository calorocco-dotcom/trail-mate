#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
using lv_coord_t = int32_t;
using lv_color_t = uint32_t;
struct lv_point_t
{
    int32_t x = 0, y = 0;
};
struct lv_point_precise_t
{
    float x = 0, y = 0;
};
struct lv_area_t
{
    int32_t x1 = 0, y1 = 0, x2 = 0, y2 = 0;
};
struct lv_layer_t
{
};
struct lv_obj_t;
struct lv_event_t
{
    lv_obj_t* object;
    lv_layer_t* layer;
    void* user;
};
using lv_event_cb_t = void (*)(lv_event_t*);
struct lv_obj_t
{
    lv_obj_t* parent = nullptr;
    int width = 320, height = 100, x = 0, y = 0, scroll_x = 0, scroll_y = 0;
    unsigned flags = 0;
    lv_event_cb_t callback = nullptr;
    void* user = nullptr;
    std::string label;
};
struct lv_draw_line_dsc_t
{
    struct Base
    {
        lv_layer_t* layer = nullptr;
    } base;
    lv_point_precise_t p1, p2;
};
struct lv_font_t
{
};
inline lv_font_t lv_font_montserrat_14;
constexpr unsigned LV_PART_MAIN = 0, LV_EVENT_DRAW_MAIN = 1, LV_OPA_80 = 204;
constexpr unsigned LV_OBJ_FLAG_IGNORE_LAYOUT = 1, LV_OBJ_FLAG_HIDDEN = 2, LV_OBJ_FLAG_SCROLLABLE = 4;
constexpr unsigned LV_LABEL_LONG_DOT = 0, LV_ALIGN_BOTTOM_MID = 0;
inline int LV_PCT(int value) { return -value; }
namespace draw_fake
{
inline std::vector<std::unique_ptr<lv_obj_t>> objects;
inline std::vector<lv_draw_line_dsc_t> lines;
inline unsigned builtin_points = 0;
inline lv_layer_t layer;
inline void draw(lv_obj_t* object)
{
    lines.clear();
    lv_event_t event{object, &layer, object->user};
    if (object->callback) object->callback(&event);
}
} // namespace draw_fake
inline lv_obj_t* lv_obj_create(lv_obj_t* parent)
{
    auto object = std::make_unique<lv_obj_t>();
    object->parent = parent;
    auto* result = object.get();
    draw_fake::objects.push_back(std::move(object));
    return result;
}
inline lv_obj_t* lv_label_create(lv_obj_t* parent) { return lv_obj_create(parent); }
inline lv_obj_t* lv_line_create(lv_obj_t* parent) { return lv_obj_create(parent); }
inline bool lv_obj_is_valid(lv_obj_t* object) { return object != nullptr; }
inline void lv_obj_set_size(lv_obj_t* object, int width, int height)
{
    object->width = width < 0 && object->parent ? -width * object->parent->width / 100 : width;
    object->height = height < 0 && object->parent ? -height * object->parent->height / 100 : height;
}
inline void lv_obj_set_pos(lv_obj_t* object, int x, int y)
{
    object->x = x;
    object->y = y;
}
inline void lv_obj_set_width(lv_obj_t* object, int width) { object->width = width; }
inline void lv_obj_add_flag(lv_obj_t* object, unsigned flag) { object->flags |= flag; }
inline void lv_obj_clear_flag(lv_obj_t* object, unsigned flag) { object->flags &= ~flag; }
inline lv_color_t lv_color_hex(uint32_t value) { return value; }
inline void lv_obj_set_style_bg_color(lv_obj_t*, lv_color_t, unsigned) {}
inline void lv_obj_set_style_bg_opa(lv_obj_t*, unsigned, unsigned) {}
inline void lv_obj_set_style_border_width(lv_obj_t*, int, unsigned) {}
inline void lv_obj_set_style_border_color(lv_obj_t*, lv_color_t, unsigned) {}
inline void lv_obj_set_style_radius(lv_obj_t*, int, unsigned) {}
inline void lv_obj_set_style_pad_all(lv_obj_t*, int, unsigned) {}
inline void lv_obj_set_style_text_font(lv_obj_t*, const lv_font_t*, unsigned) {}
inline void lv_obj_set_style_text_color(lv_obj_t*, lv_color_t, unsigned) {}
inline void lv_obj_set_style_line_color(lv_obj_t*, lv_color_t, unsigned) {}
inline void lv_obj_set_style_line_width(lv_obj_t*, int, unsigned) {}
inline void lv_obj_set_style_line_rounded(lv_obj_t*, bool, unsigned) {}
inline void lv_label_set_long_mode(lv_obj_t*, unsigned) {}
inline void lv_label_set_text(lv_obj_t* object, const char* text) { object->label = text; }
inline void lv_obj_update_layout(lv_obj_t*) {}
inline lv_coord_t lv_obj_get_content_width(lv_obj_t* object) { return object->width; }
inline lv_obj_t* lv_obj_get_parent(lv_obj_t* object) { return object->parent; }
inline void lv_obj_align(lv_obj_t*, unsigned, int, int) {}
inline void lv_obj_move_foreground(lv_obj_t*) {}
inline void lv_obj_add_event_cb(lv_obj_t* object, lv_event_cb_t callback, unsigned, void* user)
{
    object->callback = callback;
    object->user = user;
}
inline void* lv_event_get_current_target(lv_event_t* event) { return event->object; }
inline lv_layer_t* lv_event_get_layer(lv_event_t* event) { return event->layer; }
inline void* lv_event_get_user_data(lv_event_t* event) { return event->user; }
inline void lv_obj_get_coords(lv_obj_t* object, lv_area_t* area) { *area = {object->x, object->y, object->x + object->width - 1, object->y + object->height - 1}; }
inline int lv_obj_get_scroll_x(lv_obj_t* object) { return object->scroll_x; }
inline int lv_obj_get_scroll_y(lv_obj_t* object) { return object->scroll_y; }
inline void lv_draw_line_dsc_init(lv_draw_line_dsc_t* descriptor) { *descriptor = {}; }
inline void lv_obj_init_draw_line_dsc(lv_obj_t*, unsigned, lv_draw_line_dsc_t*) {}
inline void lv_draw_line(lv_layer_t*, const lv_draw_line_dsc_t* descriptor) { draw_fake::lines.push_back(*descriptor); }
inline void lv_line_set_points(lv_obj_t*, const lv_point_precise_t*, uint16_t) { ++draw_fake::builtin_points; }
