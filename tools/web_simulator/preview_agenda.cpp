// Deterministic service boundary for real widget rendering. Firmware binds the
// target-owned Agenda model instead; this source never enters a device build.
#include "ui/app_runtime.h"
#include "ui/screens/agenda/agenda_page_shell.h"
#include "ui_presentation/agenda/agenda_source.h"
#include <cstdio>
#include <emscripten.h>

namespace
{
bool available = true, storage_ready = true, clock_ready = true;
int queries = 0;
class Source : public ui::agenda::IAgendaSource
{
  public:
    ::agenda::ClockSample currentTime() const override
    {
        ::agenda::ClockSample clock;
        clock.valid = clock_ready;
        clock.calendar_seconds = 1792108800 + 9 * 3600;
        return clock;
    }
    void snapshot(const ui::agenda::AgendaRequest& request, ui::agenda::AgendaSnapshot& out) override
    {
        ++queries;
        out = {};
        out.storage_ready = storage_ready && available;
        out.clock_valid = clock_ready;
        if (!out.storage_ready || !out.clock_valid) return;
        out.page.count = 2;
        const char* titles[] = {"Trail check-in", "Camp resupply"};
        for (int index = 0; index < 2; ++index)
        {
            out.page.rows[index].occurrence.start = request.day_start + (10 + index * 6) * 3600;
            std::snprintf(out.page.rows[index].title, sizeof(out.page.rows[index].title), "%s", titles[index]);
        }
    }
    ::agenda::AgendaResult detail(uint32_t, ::agenda::EventRecord&) override { return ::agenda::AgendaResult::NotFound; }
    bool newDraft(ui::agenda::AgendaEditorModel&) const override { return false; }
    void month(uint16_t, uint8_t, ui::agenda::MonthSnapshot& out) override { out = {}; }
    ui::agenda::CommandResult commandResult() const override { return {}; }
} source;
} // namespace
ui::agenda::IAgendaSource* preview_agenda_source() { return &source; }
extern "C" EMSCRIPTEN_KEEPALIVE void native_preview_agenda_state(int ready, int clock)
{
    storage_ready = ready != 0;
    clock_ready = clock != 0;
}
extern "C" EMSCRIPTEN_KEEPALIVE int native_preview_agenda_queries() { return queries; }

void preview_enter_agenda(lv_obj_t* parent)
{
    class Actions : public ui::agenda::IAgendaActionSink
    {
      public:
        ui::UiActionResult save(const agenda::EventRecord&) override { return ui::UiActionResult::fail(ui::UiActionFailure::Unsupported); }
        ui::UiActionResult remove(uint32_t) override { return ui::UiActionResult::fail(ui::UiActionFailure::Unsupported); }
        ui::UiActionResult dismissReminder(uint32_t) override { return ui::UiActionResult::fail(ui::UiActionFailure::Unsupported); }
        ui::UiActionResult snoozeReminder(uint32_t) override { return ui::UiActionResult::fail(ui::UiActionFailure::Unsupported); }
    };
    static Actions actions;
    static ui::agenda::page::Host host;
    host.source = &source;
    host.actions = &actions;
    host.navigation.request_exit = [](void*)
    { ui_request_exit_to_menu(); };
    ui::agenda::page::enter(&host, parent);
}
