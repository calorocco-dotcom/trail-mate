/**
 * Tracker layout aligned with contacts_page:
 * Root(COL) -> Header + Content(ROW)
 * Content -> Filter Panel | List Panel
 * List Panel -> Status Label + List Container + Bottom Bar
 */

#include "ui/screens/tracker/tracker_page_layout.h"
#include "ui/components/two_pane_layout.h"
#include "ui/page/page_profile.h"
#include "ui/widgets/top_bar.h"

namespace tracker
{
namespace ui
{
namespace layout
{

namespace
{
const ::ui::page_profile::PageLayoutProfile& page_profile()
{
    return ::ui::page_profile::current();
}

lv_coord_t panel_pad()
{
    const auto& profile = page_profile();
    if (profile.dense)
    {
        return 1;
    }
    return profile.large_touch_hitbox ? 4 : 3;
}

inline void make_non_scrollable(lv_obj_t* obj)
{
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
}

inline void apply_base_container_style(lv_obj_t* obj)
{
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    make_non_scrollable(obj);
}
} // namespace

lv_obj_t* create_root(lv_obj_t* parent)
{
    const auto& profile = page_profile();
    lv_obj_t* root = lv_obj_create(parent);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(root, profile.top_content_gap, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
    apply_base_container_style(root);
    return root;
}

lv_obj_t* create_header(lv_obj_t* root)
{
    const auto& profile = page_profile();
    lv_obj_t* header = lv_obj_create(root);
    lv_obj_set_size(header, LV_PCT(100), profile.top_bar_height);
    lv_obj_set_style_bg_color(header, lv_color_hex(0xF6E6C6), 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    apply_base_container_style(header);
    return header;
}

lv_obj_t* create_content(lv_obj_t* root)
{
    const auto& profile = page_profile();
    ::ui::components::two_pane_layout::ContentSpec spec;
    spec.pad_left = profile.content_pad_left;
    spec.pad_right = profile.content_pad_right;
    spec.pad_top = profile.content_pad_top;
    spec.pad_bottom = profile.content_pad_bottom;
    return ::ui::components::two_pane_layout::create_content_row(root, spec);
}

lv_obj_t* create_filter_panel(lv_obj_t* content, int width)
{
    const auto& profile = page_profile();
    lv_obj_t* panel = lv_obj_create(content);
    lv_obj_set_width(panel, width);
    lv_obj_set_height(panel, LV_PCT(100));
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(panel, panel_pad(), LV_PART_MAIN);
    lv_obj_set_style_pad_row(panel, profile.filter_panel_pad_row, LV_PART_MAIN);
    lv_obj_set_style_margin_left(panel, 0, LV_PART_MAIN);
    lv_obj_set_style_margin_right(panel, 0, LV_PART_MAIN);
    apply_base_container_style(panel);
    lv_obj_set_style_bg_opa(panel, LV_OPA_TRANSP, 0);
    return panel;
}

lv_obj_t* create_list_panel(lv_obj_t* content)
{
    const auto& profile = page_profile();
    lv_obj_t* panel = lv_obj_create(content);
    lv_obj_set_height(panel, LV_PCT(100));
    lv_obj_set_width(panel, 0);
    lv_obj_set_flex_grow(panel, 1);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_left(panel, profile.list_panel_pad_left, LV_PART_MAIN);
    lv_obj_set_style_pad_right(panel, profile.list_panel_pad_right, LV_PART_MAIN);
    lv_obj_set_style_pad_top(panel, panel_pad(), LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(panel, panel_pad(), LV_PART_MAIN);
    lv_obj_set_style_pad_row(panel, profile.list_panel_pad_row, LV_PART_MAIN);
    lv_obj_set_style_margin_left(panel, 0, LV_PART_MAIN);
    lv_obj_set_style_margin_right(panel, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0xFAF0D8), 0);
    apply_base_container_style(panel);
    return panel;
}

lv_obj_t* create_list_container(lv_obj_t* list_panel)
{
    const auto& profile = page_profile();
    lv_obj_t* container = lv_obj_create(list_panel);
    lv_obj_set_width(container, LV_PCT(100));
    lv_obj_set_height(container, 0);
    lv_obj_set_flex_grow(container, 1);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(container, profile.list_panel_pad_row, LV_PART_MAIN);
    lv_obj_set_style_pad_left(container, profile.list_panel_pad_left, LV_PART_MAIN);
    lv_obj_set_style_pad_right(container, profile.list_panel_pad_right, LV_PART_MAIN);
    lv_obj_set_style_pad_top(container, panel_pad(), LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(container, panel_pad(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(container, lv_color_hex(0xFAF0D8), 0);
    apply_base_container_style(container);
    return container;
}

} // namespace layout
} // namespace ui
} // namespace tracker
