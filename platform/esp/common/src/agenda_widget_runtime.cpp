#include "platform/ui/agenda_widget_runtime.h"
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
#include "agenda/usecase/agenda_query.h"
#include "agenda/usecase/agenda_service.h"
#include "esp_heap_caps.h"
#include "platform/esp/arduino_common/agenda_clock.h"
#include "platform/esp/arduino_common/storage/agenda_storage.h"
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "platform/esp/common/storage/agenda_file_store.h"
#include "ui_presentation/agenda/agenda_source.h"
#include <cstring>
#include <new>
namespace platform::ui
{
namespace
{
namespace sd = ::platform::esp::arduino_common::storage;
class ReadOnlyFiles final : public ::platform::esp::storage::RecordFileIo
{
    sd::SdRuntimeFile file_{sd::SdRuntimeMemoryPolicy::PsramOnly};
    uint32_t session_ = 0;
    bool usable(Handle file) const { return file == &file_ && session_ == sd::sd_media_session() && sd::sd_card_ready() && !sd::sd_external_block_owner_active() && file_.is_open(); }

  public:
    bool missing = false;
    void bind()
    {
        file_.close();
        session_ = sd::sd_media_session();
        missing = false;
    }
    OpenResult open(const char* path, Mode mode) override
    {
        // A visualization must never create, repair or modify an Agenda store.
        if (mode != Mode::Read || file_.is_open()) return {};
        if (session_ != sd::sd_media_session() || !sd::sd_card_ready() || sd::sd_external_block_owner_active()) return {nullptr, OpenStatus::Unavailable};
        if (file_.open(path, "rb", session_)) return {&file_, OpenStatus::Ready};
        if (file_.read_busy()) return {};
        uint8_t probe = 0;
        const auto status = sd::sd_read_file(path, &probe, 1).status;
        if (session_ != sd::sd_media_session() || !sd::sd_card_ready() || sd::sd_external_block_owner_active()) return {nullptr, OpenStatus::Unavailable};
        missing = status == sd::SdFileReadStatus::Missing;
        return {nullptr, missing ? OpenStatus::Missing : OpenStatus::IoError};
    }
    bool close(Handle file) override
    {
        if (file != &file_) return false;
        const bool ready = usable(file);
        file_.close();
        return ready;
    }
    bool size(Handle file, uint64_t& bytes) override
    {
        bytes = 0;
        if (!usable(file)) return false;
        bytes = file_.size();
        return !file_.read_busy();
    }
    bool seek(Handle file, uint64_t offset) override { return usable(file) && file_.seek(offset); }
    std::size_t read(Handle file, void* out, std::size_t bytes) override
    {
        if (!usable(file)) return 0;
        const auto count = file_.read(out, bytes);
        return count > 0 ? count : 0;
    }
    std::size_t write(Handle, const void*, std::size_t) override { return 0; }
    bool sync(Handle) override { return false; }
    bool publish(const char*, const char*) override { return false; }
};
class Source final : public ::ui::agenda::IAgendaSource
{
    ReadOnlyFiles files_;
    ::platform::esp::storage::AgendaFileStore store_{sd::kAgendaFile, sd::kAgendaInitializationFile, files_};
    ::platform::esp::arduino_common::AgendaClock clock_;
    ::agenda::StoreResult prepare()
    {
        files_.bind();
        return store_.begin();
    }

  public:
    ::agenda::ClockSample currentTime() const override { return clock_.sample(); }
    void snapshot(const ::ui::agenda::AgendaRequest& request, ::ui::agenda::AgendaSnapshot& out) override
    {
        using Snapshot = ::ui::agenda::AgendaSnapshot;
        out.~Snapshot();
        new (&out) Snapshot{};
        const auto time = clock_.sample();
        out.clock_valid = time.valid;
        out.storage_ready = sd::sd_card_ready() && !sd::sd_external_block_owner_active();
        out.revision = sd::sd_media_session();
        if (time.valid) out.today_start = time.calendar_seconds / 86400 * 86400;
        if (!out.storage_ready)
        {
            out.result = ::agenda::StoreResult::IoError;
            return;
        }
        if (!time.valid) return;
        out.result = prepare();
        if (files_.missing)
        {
            out.result = ::agenda::StoreResult::Ok;
            return;
        }
        if (out.result == ::agenda::StoreResult::Ok)
            out.result = ::agenda::queryAgenda(store_, request.day_start, ::agenda::kLastCalendarSecond + 1, request.visible_rows, request.after, out.page);
        if (out.revision != sd::sd_media_session() || !sd::sd_card_ready() || sd::sd_external_block_owner_active())
        {
            out.page = {};
            out.storage_ready = false;
            out.result = ::agenda::StoreResult::IoError;
        }
    }
    ::agenda::AgendaResult detail(uint32_t id, ::agenda::EventRecord& out) override
    {
        if (prepare() != ::agenda::StoreResult::Ok) return ::agenda::AgendaResult::StorageError;
        ::agenda::AgendaService service(store_);
        return service.read(id, out);
    }
    bool newDraft(::ui::agenda::AgendaEditorModel&) const override { return false; }
    void month(uint16_t year, uint8_t month, ::ui::agenda::MonthSnapshot& out) override
    {
        out = {};
        out.year = year;
        out.month = month;
        out.result = prepare();
        if (out.result == ::agenda::StoreResult::Ok) out.result = ::agenda::queryMonth(store_, year, month, out.occupied_days);
    }
    ::ui::agenda::CommandResult commandResult() const override { return {}; }
};
} // namespace
::ui::agenda::IAgendaSource* create_agenda_widget_source()
{
    auto* memory = heap_caps_malloc(sizeof(Source), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return memory ? new (memory) Source{} : nullptr;
}
void destroy_agenda_widget_source(::ui::agenda::IAgendaSource* source)
{
    if (!source) return;
    static_cast<Source*>(source)->~Source();
    heap_caps_free(source);
}
} // namespace platform::ui
#else
namespace platform::ui
{
::ui::agenda::IAgendaSource* create_agenda_widget_source() { return nullptr; }
void destroy_agenda_widget_source(::ui::agenda::IAgendaSource*) {}
} // namespace platform::ui
#endif
