#if !defined(ARDUINO_T_WATCH_S3)
/**
 * @file chat_message_list_layout.cpp
 * @brief Layout (structure only) for ChatMessageListScreen
 */

#include "ui/screens/chat/chat_message_list_layout.h"
#include "ui/assets/fonts/font_utils.h"
#include "ui/components/info_card.h"
#include "ui/components/two_pane_layout.h"
#include "ui/localization.h"
#include "ui/page/page_profile.h"
#include "ui/ui_common.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace chat::ui::layout
{
namespace
{

constexpr int kPanelGap = 0;

struct Metrics
{
    int filter_panel_width = 80;
    int filter_button_height = 28;
    int list_item_height = 28;
    int name_x = 10;
    int name_width = 96;
    int preview_x = 112;
    int preview_width = 112;
    int unread_x = 42;
    int unread_width = 28;
    int time_x = 10;
    int time_width = 38;
};

Metrics current_metrics()
{
    const auto& profile = ::ui::page_profile::current();
    Metrics metrics{};
    metrics.filter_panel_width = profile.filter_panel_width;
    metrics.filter_button_height = profile.filter_button_height;
    metrics.list_item_height = profile.list_item_height;
    if (profile.large_touch_hitbox)
    {
        metrics.name_x = 16;
        metrics.name_width = 146;
        metrics.preview_x = 170;
        metrics.preview_width = 236;
        metrics.unread_x = 72;
        metrics.unread_width = 44;
        metrics.time_x = 16;
        metrics.time_width = 52;
    }
    else
    {
        metrics.filter_panel_width =
            ::ui::page_profile::is_dense()
                ? metrics.filter_panel_width
                : std::max(metrics.filter_panel_width, 104);
    }
    return metrics;
}

const char* broadcast_filter_text()
{
    return ::ui::page_profile::current().large_touch_hitbox ? "Broadcast" : "Bcast";
}

std::string truncate_preview(const std::string& text)
{
    std::string one_line = text;
    for (char& ch : one_line)
    {
        if (ch == '\r' || ch == '\n' || ch == '\t')
        {
            ch = ' ';
        }
    }

    const size_t kMaxPreviewBytes = ::ui::page_profile::current().large_touch_hitbox ? 42U : 24U;
    if (one_line.size() <= kMaxPreviewBytes)
    {
        return one_line;
    }

    auto utf8_char_bytes = [](unsigned char lead) -> size_t
    {
        if ((lead & 0x80U) == 0)
        {
            return 1;
        }
        if ((lead & 0xE0U) == 0xC0U)
        {
            return 2;
        }
        if ((lead & 0xF0U) == 0xE0U)
        {
            return 3;
        }
        if ((lead & 0xF8U) == 0xF0U)
        {
            return 4;
        }
        return 1;
    };

    size_t safe_len = 0;
    while (safe_len < one_line.size())
    {
        const size_t next = utf8_char_bytes(static_cast<unsigned char>(one_line[safe_len]));
        if (safe_len + next > kMaxPreviewBytes)
        {
            break;
        }
        safe_len += next;
    }
    if (safe_len == 0)
    {
        safe_len = kMaxPreviewBytes;
    }

    std::string out = one_line.substr(0, safe_len);
    out.append("...");
    return out;
}

void replace_all(std::string& text, const char* from, const char* to)
{
    if (!from || !to || !*from)
    {
        return;
    }

    std::string::size_type pos = 0;
    const std::string from_text(from);
    const std::string to_text(to);
    while ((pos = text.find(from_text, pos)) != std::string::npos)
    {
        text.replace(pos, from_text.size(), to_text);
        pos += to_text.size();
    }
}

std::string compact_list_name(const std::string& name)
{
    std::string compact = name;
    for (char& ch : compact)
    {
        if (ch == '\r' || ch == '\n' || ch == '\t')
        {
            ch = ' ';
        }
    }

    if (!::ui::components::info_card::use_tdeck_layout())
    {
        return compact;
    }

    replace_all(compact, ::ui::i18n::tr("Broadcast"), ::ui::i18n::tr("Bcast"));
    replace_all(compact, "Primary", "Pri");
    replace_all(compact, "Secondary", "Sec");
    return compact;
}

std::string build_list_title(const chat::ConversationMeta& conv)
{
    return compact_list_name(conv.name);
}

void style_filter_label(lv_obj_t* label)
{
    if (!label)
    {
        return;
    }

    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    ::ui::fonts::apply_localized_font(label, lv_label_get_text(label), ::ui::fonts::ui_chrome_font());
    lv_obj_set_style_text_color(label, lv_color_hex(0x3A2A1A), 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
}

void apply_single_line(lv_obj_t* label)
{
    if (!label)
    {
        return;
    }

    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    ::ui::components::info_card::apply_single_line_label(label);
}

lv_obj_t* create_root(lv_obj_t* parent)
{
    return ::ui::components::two_pane_layout::create_root(parent);
}

lv_obj_t* create_content(lv_obj_t* parent)
{
    return ::ui::components::two_pane_layout::create_content_row(parent);
}

lv_obj_t* create_filter_panel(lv_obj_t* parent,
                              lv_obj_t** direct_btn,
                              lv_obj_t** broadcast_btn,
                              lv_obj_t** team_btn)
{
    const auto& profile = ::ui::page_profile::current();
    const Metrics metrics = current_metrics();

    ::ui::components::two_pane_layout::SidePanelSpec panel_spec;
    panel_spec.width = metrics.filter_panel_width;
    panel_spec.pad_row = profile.filter_panel_pad_row;
    panel_spec.margin_left = 0;
    panel_spec.margin_right = 0;
    lv_obj_t* panel = ::ui::components::two_pane_layout::create_side_panel(parent, panel_spec);

    lv_obj_t* direct = lv_btn_create(panel);
    lv_obj_set_size(direct, LV_PCT(100), metrics.filter_button_height);
    ::ui::components::two_pane_layout::make_non_scrollable(direct);
    lv_obj_t* direct_label = lv_label_create(direct);
    ::ui::i18n::set_label_text(direct_label, "Direct");
    style_filter_label(direct_label);
    lv_obj_center(direct_label);

    lv_obj_t* broadcast = lv_btn_create(panel);
    lv_obj_set_size(broadcast, LV_PCT(100), metrics.filter_button_height);
    ::ui::components::two_pane_layout::make_non_scrollable(broadcast);
    lv_obj_t* broadcast_label = lv_label_create(broadcast);
    ::ui::i18n::set_label_text(broadcast_label, broadcast_filter_text());
    style_filter_label(broadcast_label);
    lv_obj_center(broadcast_label);

    lv_obj_t* team = lv_btn_create(panel);
    lv_obj_set_size(team, LV_PCT(100), metrics.filter_button_height);
    ::ui::components::two_pane_layout::make_non_scrollable(team);
    lv_obj_t* team_label = lv_label_create(team);
    ::ui::i18n::set_label_text(team_label, "Team");
    style_filter_label(team_label);
    lv_obj_center(team_label);
    lv_obj_add_flag(team, LV_OBJ_FLAG_HIDDEN);

    if (direct_btn) *direct_btn = direct;
    if (broadcast_btn) *broadcast_btn = broadcast;
    if (team_btn) *team_btn = team;
    return panel;
}

lv_obj_t* create_list_panel(lv_obj_t* parent)
{
    const auto& profile = ::ui::page_profile::current();

    ::ui::components::two_pane_layout::MainPanelSpec panel_spec;
    panel_spec.pad_all = profile.large_touch_hitbox ? 6 : 3;
    panel_spec.pad_row = profile.list_panel_pad_row;
    panel_spec.pad_left = profile.list_panel_pad_left;
    panel_spec.pad_right = profile.list_panel_pad_right;
    panel_spec.scrollbar_mode = LV_SCROLLBAR_MODE_AUTO;
    return ::ui::components::two_pane_layout::create_main_panel(parent, panel_spec);
}

} // namespace

MessageListLayout create_layout(lv_obj_t* parent)
{
    MessageListLayout w{};
    w.root = create_root(parent);
    w.content = create_content(w.root);
    w.filter_panel = create_filter_panel(w.content, &w.direct_btn, &w.broadcast_btn, &w.team_btn);
    w.list_panel = create_list_panel(w.content);
    return w;
}

MessageItemWidgets create_message_item(lv_obj_t* parent)
{
    const Metrics metrics = current_metrics();

    MessageItemWidgets w{};
    w.btn = lv_obj_create(parent);
    lv_obj_add_flag(w.btn, LV_OBJ_FLAG_CLICKABLE);
    if (::ui::components::info_card::use_tdeck_layout())
    {
        ::ui::components::info_card::ContentOptions options{};
        options.header_meta = true;
        options.body_meta = true;
        ::ui::components::info_card::configure_item(w.btn, metrics.list_item_height);
        const auto slots = ::ui::components::info_card::create_content(w.btn, options);
        w.name_label = slots.header_main_label;
        w.time_label = slots.header_meta_label;
        w.preview_label = slots.body_main_label;
        w.unread_label = slots.body_meta_label;
        if (w.time_label)
        {
            lv_obj_add_flag(w.time_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
    else
    {
        lv_obj_set_size(w.btn, LV_PCT(100), metrics.list_item_height);
        ::ui::components::two_pane_layout::make_non_scrollable(w.btn);
        lv_obj_set_flex_flow(w.btn, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(w.btn,
                              LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_left(w.btn, metrics.name_x, LV_PART_MAIN);
        lv_obj_set_style_pad_right(w.btn, 6, LV_PART_MAIN);
        lv_obj_set_style_pad_column(w.btn, 6, LV_PART_MAIN);

        w.name_label = lv_label_create(w.btn);
        lv_obj_add_flag(w.name_label, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_set_width(w.name_label, metrics.name_width);
        apply_single_line(w.name_label);

        w.preview_label = lv_label_create(w.btn);
        lv_obj_add_flag(w.preview_label, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_set_width(w.preview_label, 0);
        lv_obj_set_flex_grow(w.preview_label, 1);
        apply_single_line(w.preview_label);

        w.time_label = lv_label_create(w.btn);
        lv_obj_add_flag(w.time_label, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_add_flag(w.time_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_width(w.time_label, 0);
        lv_obj_set_style_text_align(w.time_label, LV_TEXT_ALIGN_RIGHT, 0);
        apply_single_line(w.time_label);

        w.unread_label = lv_label_create(w.btn);
        lv_obj_add_flag(w.unread_label, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_set_width(w.unread_label, LV_SIZE_CONTENT);
        lv_obj_set_style_text_align(w.unread_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(w.unread_label, LV_LABEL_LONG_CLIP);
    }

    return w;
}

void populate_message_item(const MessageItemWidgets& widgets,
                           const chat::ConversationMeta& conv)
{
    const bool use_info_card = ::ui::components::info_card::use_tdeck_layout();

    const std::string title = build_list_title(conv);
    lv_label_set_text(widgets.name_label, title.c_str());
    ::ui::fonts::apply_chat_content_font(widgets.name_label, title.c_str());
    if (use_info_card)
    {
        ::ui::components::info_card::apply_single_line_label(widgets.name_label);
    }
    else
    {
        apply_single_line(widgets.name_label);
    }

    const std::string preview = truncate_preview(conv.preview);
    lv_label_set_text(widgets.preview_label, preview.c_str());
    ::ui::fonts::apply_chat_content_font(widgets.preview_label, preview.c_str());
    if (use_info_card)
    {
        ::ui::components::info_card::apply_single_line_label(widgets.preview_label);
    }
    else
    {
        apply_single_line(widgets.preview_label);
    }

    if (widgets.time_label)
    {
        lv_label_set_text(widgets.time_label, "");
        lv_obj_add_flag(widgets.time_label, LV_OBJ_FLAG_HIDDEN);
    }

    if (conv.unread > 0)
    {
        char unread_str[16];
        std::snprintf(unread_str, sizeof(unread_str), "%d", conv.unread);
        lv_label_set_text(widgets.unread_label, unread_str);
        lv_obj_clear_flag(widgets.unread_label, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_label_set_text(widgets.unread_label, "");
        lv_obj_add_flag(widgets.unread_label, LV_OBJ_FLAG_HIDDEN);
    }
    ::ui::fonts::apply_localized_font(
        widgets.unread_label, lv_label_get_text(widgets.unread_label), ::ui::fonts::ui_chrome_font());
    lv_label_set_long_mode(widgets.unread_label, LV_LABEL_LONG_CLIP);
}

lv_obj_t* create_placeholder(lv_obj_t* parent)
{
    lv_obj_t* label = lv_label_create(parent);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    return label;
}

} // namespace chat::ui::layout

#endif
