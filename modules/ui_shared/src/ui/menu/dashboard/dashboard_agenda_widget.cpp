#include "ui/localization.h"
#include "ui/menu/dashboard/dashboard_style.h"
#include "ui/menu/dashboard/dashboard_widgets.h"
#include "ui_presentation/agenda/agenda_source.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace ui::menu::dashboard
{
namespace
{
struct AgendaWidgetUi : WidgetLifetime
{
    DashboardCardChrome chrome{};
    lv_obj_t* text = nullptr;
    ui::agenda::IAgendaSource* source = nullptr;
    ui::agenda::AgendaSnapshot snapshot{};
    uint32_t next_query = 0;
    char buffer[640]{};
};
void refresh_agenda_widget(void* context)
{
    auto& state = *static_cast<AgendaWidgetUi*>(context);
    if (!state.source)
    {
        const auto* unavailable = ui::i18n::tr("Agenda is unavailable on this device.");
        if (std::strcmp(lv_label_get_text(state.text), unavailable) != 0)
            ui::i18n::set_label_text_raw(state.text, unavailable);
        return;
    }
    const uint32_t now = lv_tick_get();
    if (state.next_query != 0 && static_cast<int32_t>(now - state.next_query) < 0) return;
    state.next_query = now + 30000U;
    const auto clock = state.source->currentTime();
    ui::agenda::AgendaRequest request;
    request.day_start = clock.calendar_seconds / 86400 * 86400;
    request.visible_rows = lv_obj_get_height(state.chrome.card) < 230 ? 2 : 3;
    state.source->snapshot(request, state.snapshot);
    if (!state.snapshot.clock_valid) std::snprintf(state.buffer, sizeof(state.buffer), "%s", ui::i18n::tr("Waiting for time"));
    else if (!state.snapshot.storage_ready || state.snapshot.result != ::agenda::StoreResult::Ok)
        std::snprintf(state.buffer, sizeof(state.buffer), "%s", ui::i18n::tr("Storage unavailable"));
    else if (state.snapshot.page.count == 0)
        std::snprintf(state.buffer, sizeof(state.buffer), "%s", ui::i18n::tr("No upcoming events"));
    else
    {
        size_t used = 0;
        for (uint8_t index = 0; index < std::min(state.snapshot.page.count, request.visible_rows); ++index)
        {
            const auto& row = state.snapshot.page.rows[index];
            const auto start = row.occurrence.start;
            const auto days = start / 86400 - clock.calendar_seconds / 86400;
            char day[64];
            if (days == 0 || days == 1) std::snprintf(day, sizeof(day), "%s", ui::i18n::tr(days == 0 ? "Today" : "Tomorrow"));
            else std::snprintf(day, sizeof(day), "%s", ui::i18n::format("In %d days", static_cast<int>(days)).c_str());
            const int bytes = std::snprintf(state.buffer + used, sizeof(state.buffer) - used,
                                            "%s%s %02d:%02d\n%.*s", index ? "\n\n" : "", day,
                                            static_cast<int>((start / 3600) % 24), static_cast<int>((start / 60) % 60),
                                            static_cast<int>(sizeof(row.title)), row.title);
            if (bytes < 0 || static_cast<size_t>(bytes) >= sizeof(state.buffer) - used) break;
            used += static_cast<size_t>(bytes);
        }
    }
    if (std::strcmp(lv_label_get_text(state.text), state.buffer) != 0)
        ui::i18n::set_label_text_raw(state.text, state.buffer);
    set_status_chip(state.chrome, state.snapshot.storage_ready ? "Upcoming" : "--", color_soft_amber(), color_text());
}
} // namespace
WidgetHandle create_agenda_widget(lv_obj_t* parent, lv_coord_t width, lv_coord_t height, ui::agenda::IAgendaSource* source)
{
    auto* state = create_widget_state<AgendaWidgetUi>();
    if (!state) return {};
    state->source = source;
    state->chrome = create_card_chrome(parent, ui::i18n::tr("Agenda"), width, height);
    state->text = lv_label_create(state->chrome.body);
    style_body_label(state->text, &lv_font_montserrat_14, color_text());
    lv_obj_set_width(state->text, LV_PCT(100));
    lv_label_set_long_mode(state->text, LV_LABEL_LONG_WRAP);
    ui::i18n::set_label_text_raw(state->text, "");
    auto* footer = lv_label_create(state->chrome.footer);
    style_footer_label(footer);
    ui::i18n::set_label_text(footer, "Upcoming");
    return own_widget(state->chrome.card, state, refresh_agenda_widget);
}
} // namespace ui::menu::dashboard
