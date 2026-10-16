#include "ui/components/air_status_footer.h"

#include "ui/assets/fonts/font_utils.h"
#include "ui/components/two_pane_layout.h"
#include "ui/components/two_pane_styles.h"
#include "ui/localization.h"
#include "ui/page/page_profile.h"
#include "ui/presentation_sources/runtime_device_status_source.h"
#include "ui_presentation/device/device_status_model.h"

namespace ui::components::air_status_footer
{
namespace
{

constexpr lv_coord_t kFooterHeightLarge = 44;

::ui::device::DeviceStatusModel& deviceStatusModel()
{
    static ::ui::device::DeviceStatusModel model(
        ::ui::presentation_sources::runtime_device_status_source());
    return model;
}

} // namespace

Footer create(lv_obj_t* parent)
{
    Footer footer{};
    if (!parent)
    {
        return footer;
    }

    const auto& profile = ::ui::page_profile::current();
    const lv_coord_t footer_height =
        profile.large_touch_hitbox ? kFooterHeightLarge : std::max<lv_coord_t>(22, profile.ime_bar_height);
    const bool dense = !profile.large_touch_hitbox && footer_height <= 24;

    footer.container = lv_obj_create(parent);
    lv_obj_set_size(footer.container, LV_PCT(100), footer_height);
    ::ui::components::two_pane_layout::make_non_scrollable(footer.container);
    ::ui::components::two_pane_styles::apply_container_main(footer.container);
    lv_obj_set_style_border_width(footer.container, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(
        footer.container,
        lv_color_hex(::ui::components::two_pane_styles::kBorder),
        LV_PART_MAIN);
    lv_obj_set_style_radius(footer.container, dense ? 4 : 8, LV_PART_MAIN);
    lv_obj_set_style_pad_left(footer.container, profile.large_touch_hitbox ? 12 : (dense ? 5 : 10), LV_PART_MAIN);
    lv_obj_set_style_pad_right(footer.container, profile.large_touch_hitbox ? 12 : (dense ? 5 : 10), LV_PART_MAIN);
    lv_obj_set_style_pad_top(footer.container, profile.large_touch_hitbox ? 6 : (dense ? 2 : 4), LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(footer.container, profile.large_touch_hitbox ? 6 : (dense ? 2 : 4), LV_PART_MAIN);
    lv_obj_set_style_pad_row(footer.container, dense ? 0 : 1, LV_PART_MAIN);
    lv_obj_set_style_pad_column(footer.container, dense ? 4 : 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(footer.container, dense ? LV_FLEX_FLOW_ROW : LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        footer.container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    footer.summary_label = lv_label_create(footer.container);
    lv_obj_set_width(footer.summary_label, dense ? 0 : LV_PCT(100));
    if (dense) lv_obj_set_flex_grow(footer.summary_label, 1);
    lv_label_set_long_mode(footer.summary_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(footer.summary_label, dense ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_CENTER, 0);
    ::ui::components::two_pane_styles::apply_label_primary(footer.summary_label);
    ::ui::fonts::apply_font(footer.summary_label, dense ? ::ui::page_profile::resolve_tiny_font() : ::ui::page_profile::resolve_body_font());

    footer.detail_label = lv_label_create(footer.container);
    lv_obj_set_width(footer.detail_label, dense ? 118 : LV_PCT(100));
    lv_label_set_long_mode(footer.detail_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(footer.detail_label, dense ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_CENTER, 0);
    ::ui::components::two_pane_styles::apply_label_muted(footer.detail_label);
    ::ui::fonts::apply_font(footer.detail_label, dense ? ::ui::page_profile::resolve_tiny_font() : ::ui::page_profile::resolve_body_font());

    refresh(footer);
    return footer;
}

void refresh(Footer& footer)
{
    if (!footer.summary_label || !footer.detail_label)
    {
        return;
    }

    const auto snapshot = deviceStatusModel().snapshot();
    const char* summary = snapshot.header.valid ? snapshot.status_line.c_str() : "-";
    const char* detail = snapshot.header.valid ? snapshot.modem_preset.c_str() : "-";

    ::ui::i18n::set_label_text_raw(footer.summary_label, summary);
    ::ui::i18n::set_label_text_raw(footer.detail_label, detail);
}

void populate_info(lv_obj_t* parent)
{
    const auto snapshot = deviceStatusModel().snapshot();
    for (const auto* text : {snapshot.header.valid ? snapshot.status_line.c_str() : "-",
                             snapshot.header.valid ? snapshot.modem_preset.c_str() : "-"})
    {
        auto* label = lv_label_create(parent);
        lv_obj_set_width(label, LV_PCT(100));
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        ::ui::components::two_pane_styles::apply_label_primary(label);
        ::ui::fonts::apply_font(label, ::ui::page_profile::resolve_body_font());
        ::ui::i18n::set_label_text_raw(label, text);
    }
}

} // namespace ui::components::air_status_footer
