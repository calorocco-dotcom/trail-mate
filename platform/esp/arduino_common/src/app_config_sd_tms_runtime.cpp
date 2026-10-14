#include "platform/esp/arduino_common/app_config_sd_tms_runtime.h"

#include "app/tms_config_codec.h"
#include "platform/esp/arduino_common/app_config_tms_settings_extension.h"
#include "platform/esp/arduino_common/chat/infra/store/fixed_slot_journal.h"
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "platform/ui/device_runtime.h"
#include "platform/ui/reticulum_group_config_runtime.h"
#include "platform/ui/reticulum_network_config_runtime.h"
#include "platform/ui/settings_store.h"

#include "platform/esp/common/config_preferences.h"
#include "platform/esp/common/reticulum_runtime_compat.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#endif

namespace app::sd_tms
{
namespace
{

using ::platform::esp::arduino_common::storage::SdRuntimeFile;

constexpr const char* kConfigDir = "/trailmate";
constexpr const char* kConfigPath = "/trailmate/config.tms";
constexpr const char* kConfigTempPath = "/trailmate/config.tms.new";
constexpr const char* kConfigRecoveryPath = "/trailmate/config.tms.bak";
constexpr const char* kWebCandidatePath = "/trailmate/config.web.tms";
constexpr const char* kBackupDir = "/trailmate/backup";
constexpr const char* kBackupPath = "/trailmate/backup/settings.tms";
constexpr const char* kBackupTempPath = "/trailmate/backup/settings.tms.new";
constexpr const char* kBackupRecoveryPath = "/trailmate/backup/settings.tms.bak";
constexpr const char* kResetNamespace = "cfgreset";
constexpr const char* kResetPendingKey = "working";

// The only NVS state owned by this repository is an operation marker.  It
// contains no configuration and exists solely so a Factory Reset performed
// without an SD card cannot be undone by inserting an old card later.
bool reset_pending()
{
    ::platform::esp::common::ConfigPreferences preferences;
    if (!preferences.begin(kResetNamespace, true))
    {
        return false;
    }
    const bool pending = preferences.getBool(kResetPendingKey, false);
    preferences.end();
    return pending;
}

bool set_reset_pending(bool pending)
{
    ::platform::esp::common::ConfigPreferences preferences;
    if (!preferences.begin(kResetNamespace, false))
    {
        return false;
    }
    const bool ok = pending ? preferences.putBool(kResetPendingKey, true)
                            : preferences.remove(kResetPendingKey);
    preferences.end();
    return ok;
}

bool sd_available()
{
    return ::platform::esp::arduino_common::storage::sd_card_ready();
}

bool ensure_directory(const char* path)
{
    if (::platform::esp::arduino_common::storage::sd_exists(path))
    {
        return ::platform::esp::arduino_common::storage::sd_is_directory(path);
    }
    return ::platform::esp::arduino_common::storage::sd_mkdir(path);
}

bool remove_if_present(const char* path)
{
    return !::platform::esp::arduino_common::storage::sd_exists(path) ||
           ::platform::esp::arduino_common::storage::sd_remove(path);
}

// Operations are serialized by the application owner. Nested candidate
// validation borrows the emission buffer; only the outer lease frees it.
tms::LineScratch* s_line_scratch_storage = nullptr;
class ScopedLineScratch
{
  public:
    ScopedLineScratch()
    {
        if (s_line_scratch_storage) return;
#if defined(ESP_PLATFORM)
        void* raw = heap_caps_malloc(sizeof(tms::LineScratch), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
        void* raw = std::malloc(sizeof(tms::LineScratch));
#endif
        if (raw)
        {
            s_line_scratch_storage = new (raw) tms::LineScratch{};
            owner_ = true;
        }
    }
    ~ScopedLineScratch()
    {
        if (!owner_) return;
#if defined(ESP_PLATFORM)
        heap_caps_free(s_line_scratch_storage);
#else
        std::free(s_line_scratch_storage);
#endif
        s_line_scratch_storage = nullptr;
    }
    ScopedLineScratch(const ScopedLineScratch&) = delete;
    ScopedLineScratch& operator=(const ScopedLineScratch&) = delete;
    bool ready() const { return s_line_scratch_storage != nullptr; }

  private:
    bool owner_ = false;
};
#define s_line_scratch (*s_line_scratch_storage)
const AppConfig* s_working_config = nullptr;
bool s_sync_pending = false;
bool s_reset_in_progress = false;
bool s_repair_required = false;
uint32_t s_next_sync_attempt_ms = 0U;
uint32_t s_web_restart_at = 0U;

bool tracks_working_settings(const char* ns)
{
    return ns == nullptr || std::strcmp(ns, "settings") == 0 ||
           std::strcmp(ns, "power") == 0 || std::strcmp(ns, "a7682e") == 0;
}

void on_settings_store_changed(void*, const char* ns, const char*)
{
    if (!s_reset_in_progress && tracks_working_settings(ns))
    {
        requestWorkingConfigSync();
    }
}

bool consume_physical_line(tms::Decoder& decoder, std::size_t length)
{
    if (!s_line_scratch_storage || length >= sizeof(s_line_scratch.bytes))
    {
        return false;
    }
    if (length > 0U && s_line_scratch.bytes[length - 1U] == '\r')
    {
        --length;
    }
    s_line_scratch.bytes[length] = '\0';
    return decoder.consumeLine(s_line_scratch.bytes);
}

bool read_document(const char* path,
                   tms::DocumentKind kind,
                   AppConfig* target,
                   uint16_t* schema_version,
                   tms::DocumentInfo* info)
{
    if (schema_version)
    {
        *schema_version = 0U;
    }
    if (info)
    {
        *info = {};
    }
    if (!path || !sd_available() ||
        !::platform::esp::arduino_common::storage::sd_exists(path))
    {
        return false;
    }

    SdRuntimeFile file;
    if (!file.open(path, "r"))
    {
        return false;
    }
    const uint64_t size = file.size();
    if (size == 0U || size > tms::kMaxDocumentBytes)
    {
        file.close();
        return false;
    }

    ScopedLineScratch scratch;
    if (!scratch.ready()) return false;
    settings_extension::beginRead(target != nullptr);
    tms::Decoder decoder(target,
                         kind,
                         settings_extension::consumeRecord,
                         nullptr,
                         settings_extension::finishDocument);
    std::size_t line_length = 0U;
    bool ok = true;
    for (uint64_t offset = 0U; offset < size; ++offset)
    {
        const int raw = file.read_byte();
        if (raw < 0)
        {
            ok = false;
            break;
        }
        const char value = static_cast<char>(raw);
        if (value == '\n')
        {
            if (!consume_physical_line(decoder, line_length))
            {
                ok = false;
                break;
            }
            line_length = 0U;
            continue;
        }
        if (line_length + 1U >= sizeof(s_line_scratch.bytes))
        {
            ok = false;
            break;
        }
        s_line_scratch.bytes[line_length++] = value;
    }
    if (ok && line_length > 0U)
    {
        ok = consume_physical_line(decoder, line_length);
    }
    file.close();

    const bool finished = ok && decoder.finish();
    if (schema_version)
    {
        *schema_version = decoder.schemaVersion();
    }
    if (info)
    {
        *info = decoder.info();
    }
    settings_extension::endRead();
    return finished;
}

struct FileOutput
{
    SdRuntimeFile* file = nullptr;
};

bool write_output(void* context, const char* data, std::size_t length)
{
    auto* output = static_cast<FileOutput*>(context);
    return output != nullptr && output->file != nullptr && data != nullptr &&
           output->file->write(data, length) == length;
}

bool write_document_candidate(const AppConfig& config,
                              tms::DocumentKind kind,
                              const char* directory,
                              const char* temporary_path)
{
    ScopedLineScratch scratch;
    if (!scratch.ready()) return false;
    if (!ensure_directory(directory) || !remove_if_present(temporary_path))
    {
        return false;
    }
    SdRuntimeFile file;
    if (!file.open(temporary_path, "w"))
    {
        return false;
    }

    FileOutput output{&file};
    tms::DocumentInfo emitted{};
    const bool wrote = tms::writeDocument(config,
                                          kind,
                                          {&output, write_output},
                                          s_line_scratch,
                                          &emitted,
                                          settings_extension::writeRecords,
                                          const_cast<AppConfig*>(&config));
    const bool flushed = file.flush();
    file.close();
    if (!wrote || !flushed)
    {
        (void)remove_if_present(temporary_path);
        return false;
    }

    tms::DocumentInfo validated{};
    const bool valid = read_document(temporary_path, kind, nullptr, nullptr, &validated);
    if (!valid)
    {
        std::printf("[AppCfg][TMS] candidate invalid path=%s error=%s\n",
                    temporary_path,
                    tms::decodeErrorName(validated.error));
        (void)remove_if_present(temporary_path);
    }
    return valid;
}

bool promote_candidate(const char* temporary_path,
                       const char* final_path,
                       const char* recovery_path)
{
    return chat::storage::v2::replaceFileAtomically(temporary_path,
                                                    final_path,
                                                    recovery_path);
}

bool recover_transaction(const char* final_path,
                         const char* temporary_path,
                         const char* recovery_path)
{
    return chat::storage::v2::recoverAtomicFile(final_path,
                                                temporary_path,
                                                recovery_path);
}

bool copy_backup_as_working_candidate()
{
    ScopedLineScratch scratch;
    if (!scratch.ready()) return false;
    SdRuntimeFile source;
    SdRuntimeFile destination;
    if (!source.open(kBackupPath, "r") ||
        !remove_if_present(kConfigTempPath) ||
        !destination.open(kConfigTempPath, "w"))
    {
        source.close();
        destination.close();
        return false;
    }

    bool ok = true;
    std::size_t line_length = 0U;
    const uint64_t size = source.size();
    for (uint64_t offset = 0U; ok && offset < size; ++offset)
    {
        const int raw = source.read_byte();
        if (raw < 0)
        {
            ok = false;
            break;
        }
        const char value = static_cast<char>(raw);
        if (value != '\n')
        {
            if (line_length + 1U >= sizeof(s_line_scratch.bytes))
            {
                ok = false;
                break;
            }
            s_line_scratch.bytes[line_length++] = value;
            continue;
        }

        std::size_t comparable_length = line_length;
        if (comparable_length > 0U &&
            s_line_scratch.bytes[comparable_length - 1U] == '\r')
        {
            --comparable_length;
        }
        const char terminator = s_line_scratch.bytes[comparable_length];
        s_line_scratch.bytes[comparable_length] = '\0';
        const bool is_backup_kind =
            std::strcmp(s_line_scratch.bytes, "document.kind=enum:backup") == 0;
        s_line_scratch.bytes[comparable_length] = terminator;
        if (is_backup_kind)
        {
            constexpr const char* kWorkingKind = "document.kind=enum:working\n";
            ok = destination.write(kWorkingKind, std::strlen(kWorkingKind)) ==
                 std::strlen(kWorkingKind);
        }
        else
        {
            ok = destination.write(s_line_scratch.bytes, line_length) == line_length &&
                 destination.write("\n", 1U) == 1U;
        }
        line_length = 0U;
    }
    if (line_length != 0U)
    {
        ok = false;
    }
    ok = ok && destination.flush();
    source.close();
    destination.close();
    if (!ok)
    {
        (void)remove_if_present(kConfigTempPath);
    }
    return ok;
}

bool remove_working_documents()
{
    return remove_if_present(kConfigTempPath) && remove_if_present(kConfigRecoveryPath) &&
           remove_if_present(kConfigPath);
}

bool remove_backup_documents()
{
    return remove_if_present(kBackupTempPath) && remove_if_present(kBackupRecoveryPath) &&
           remove_if_present(kBackupPath);
}

} // namespace

LoadResult loadWorkingConfig(AppConfig& config)
{
    s_repair_required = false;
    if (!sd_available())
    {
        return LoadResult::Unavailable;
    }
    if (reset_pending())
    {
        if (!remove_working_documents() || !remove_backup_documents() ||
            !::platform::ui::reticulum_groups::discardLegacySource() ||
            !::platform::ui::reticulum_network_config::discardLegacySource() ||
            !set_reset_pending(false))
        {
            s_repair_required = true;
            return LoadResult::Invalid;
        }
    }
    if (!recover_transaction(kConfigPath, kConfigTempPath, kConfigRecoveryPath))
    {
        s_repair_required = true;
        return LoadResult::Invalid;
    }
    if (!::platform::esp::arduino_common::storage::sd_exists(kConfigPath))
    {
        return LoadResult::Missing;
    }

    uint16_t schema = 0U;
    tms::DocumentInfo info{};
    if (!read_document(kConfigPath,
                       tms::DocumentKind::Working,
                       nullptr,
                       &schema,
                       &info))
    {
        std::printf("[AppCfg][TMS] invalid working file error=%s\n",
                    tms::decodeErrorName(info.error));
        s_repair_required = true;
        return LoadResult::Invalid;
    }
    if (schema != tms::kSchemaVersion)
    {
        return LoadResult::Legacy;
    }

    if (!read_document(kConfigPath,
                       tms::DocumentKind::Working,
                       &config,
                       nullptr,
                       &info))
    {
        std::printf("[AppCfg][TMS] working file changed during apply error=%s\n",
                    tms::decodeErrorName(info.error));
        s_repair_required = true;
        return LoadResult::Invalid;
    }
    return LoadResult::Applied;
}

bool applyLegacyWorkingConfig(AppConfig& config)
{
    uint16_t schema = 0U;
    tms::DocumentInfo info{};
    if (!read_document(kConfigPath,
                       tms::DocumentKind::Working,
                       nullptr,
                       &schema,
                       &info) ||
        schema >= tms::kSchemaVersion)
    {
        return false;
    }
    return read_document(kConfigPath,
                         tms::DocumentKind::Working,
                         &config,
                         nullptr,
                         &info);
}

bool workingConfigRequiresRepair()
{
    return s_repair_required;
}

void requireWorkingConfigRepair()
{
    s_repair_required = true;
}

void bindWorkingConfig(const AppConfig& config)
{
    s_working_config = &config;
    ::platform::ui::settings_store::set_change_observer(on_settings_store_changed,
                                                        nullptr);
}

bool workingConfigIsBound()
{
    return s_working_config != nullptr;
}

void requestWorkingConfigSync()
{
    if (!s_reset_in_progress)
    {
        s_sync_pending = true;
        s_next_sync_attempt_ms = 0U;
    }
}

WorkingConfigSyncResult syncPendingWorkingConfig()
{
    if (s_web_restart_at != 0U || !s_working_config || s_repair_required)
    {
        return WorkingConfigSyncResult::Failed;
    }
    if (!s_sync_pending)
    {
        return WorkingConfigSyncResult::Synchronized;
    }
    if (!s_working_config || s_repair_required)
    {
        return WorkingConfigSyncResult::Failed;
    }
    if (!sd_available())
    {
        return WorkingConfigSyncResult::Deferred;
    }
    if (!syncWorkingConfig(*s_working_config))
    {
        return WorkingConfigSyncResult::Failed;
    }

    s_sync_pending = false;
    s_next_sync_attempt_ms = 0U;
    return WorkingConfigSyncResult::Synchronized;
}

void serviceWorkingConfig()
{
    if (s_web_restart_at != 0U)
    {
        if (static_cast<int32_t>(millis() - s_web_restart_at) >= 0)
        {
            ::platform::ui::device::restart();
        }
        return;
    }
    if (!s_sync_pending || !s_working_config || s_repair_required)
    {
        return;
    }
    const uint32_t now_ms = millis();
    if (s_next_sync_attempt_ms != 0U &&
        static_cast<int32_t>(now_ms - s_next_sync_attempt_ms) < 0)
    {
        return;
    }
    if (syncPendingWorkingConfig() == WorkingConfigSyncResult::Synchronized)
    {
        return;
    }
    s_next_sync_attempt_ms = now_ms + 1000U;
}

bool syncWorkingConfig(const AppConfig& config)
{
    if (s_web_restart_at != 0U || s_repair_required || !sd_available() ||
        !write_document_candidate(config,
                                  tms::DocumentKind::Working,
                                  kConfigDir,
                                  kConfigTempPath))
    {
        return false;
    }
    return promote_candidate(kConfigTempPath, kConfigPath, kConfigRecoveryPath);
}

bool backupWorkingConfig(const AppConfig& config)
{
    if (!sd_available() ||
        !write_document_candidate(config,
                                  tms::DocumentKind::Backup,
                                  kBackupDir,
                                  kBackupTempPath))
    {
        return false;
    }
    return promote_candidate(kBackupTempPath, kBackupPath, kBackupRecoveryPath);
}

bool restoreWorkingConfig()
{
    if (s_web_restart_at != 0U || !sd_available() ||
        !recover_transaction(kBackupPath, kBackupTempPath, kBackupRecoveryPath) ||
        !::platform::esp::arduino_common::storage::sd_exists(kBackupPath))
    {
        return false;
    }
    tms::DocumentInfo info{};
    if (!read_document(kBackupPath,
                       tms::DocumentKind::Backup,
                       nullptr,
                       nullptr,
                       &info) ||
        !copy_backup_as_working_candidate() ||
        !read_document(kConfigTempPath,
                       tms::DocumentKind::Working,
                       nullptr,
                       nullptr,
                       &info))
    {
        (void)remove_if_present(kConfigTempPath);
        return false;
    }
    s_repair_required = false;
    return promote_candidate(kConfigTempPath, kConfigPath, kConfigRecoveryPath);
}

void beginWorkingConfigReset()
{
    s_reset_in_progress = true;
}

void endWorkingConfigReset()
{
    s_reset_in_progress = false;
    s_sync_pending = false;
    s_next_sync_attempt_ms = 0U;
}

const char* webWorkingCandidatePath()
{
    return kWebCandidatePath;
}

bool webConfigurationRestartPending()
{
    return s_web_restart_at != 0U;
}
void scheduleWebManagementRestart()
{
    s_sync_pending = false;
    if (s_web_restart_at != 0U) return;
    s_web_restart_at = millis() + 2000U;
    if (s_web_restart_at == 0U) s_web_restart_at = 1U;
}

bool validateWebWorkingCandidate()
{
    if (s_web_restart_at != 0U || s_repair_required || !sd_available()) return false;
    uint16_t schema = 0;
    tms::DocumentInfo info{};
    return read_document(kWebCandidatePath, tms::DocumentKind::Working, nullptr,
                         &schema, &info) &&
           schema == tms::kSchemaVersion;
}

bool commitWebWorkingCandidate()
{
    if (!validateWebWorkingCandidate() ||
        !promote_candidate(kWebCandidatePath, kConfigPath, kConfigRecoveryPath)) return false;
    s_sync_pending = false;
    s_web_restart_at = millis() + 2000U;
    if (s_web_restart_at == 0U) s_web_restart_at = 1U;
    return true;
}

bool resetWorkingConfig()
{
    if (s_web_restart_at != 0U) return false;
    s_sync_pending = false;
    s_next_sync_attempt_ms = 0U;
    s_repair_required = false;
    if (!sd_available())
    {
        return set_reset_pending(true);
    }
    return remove_working_documents() && remove_backup_documents() &&
           ::platform::ui::reticulum_groups::discardLegacySource() &&
           ::platform::ui::reticulum_network_config::discardLegacySource() &&
           set_reset_pending(false);
}

const char* loadResultName(LoadResult result)
{
    switch (result)
    {
    case LoadResult::Unavailable:
        return "unavailable";
    case LoadResult::Missing:
        return "missing";
    case LoadResult::Legacy:
        return "legacy";
    case LoadResult::Invalid:
        return "invalid";
    case LoadResult::Applied:
        return "applied";
    }
    return "unknown";
}

#undef s_line_scratch
} // namespace app::sd_tms
