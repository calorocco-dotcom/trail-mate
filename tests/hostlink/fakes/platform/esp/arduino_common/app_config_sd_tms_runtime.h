#pragma once
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
namespace app::sd_tms
{
enum class WorkingConfigSyncResult
{
    Synchronized,
    Deferred,
    Failed
};
inline WorkingConfigSyncResult fake_sync = WorkingConfigSyncResult::Synchronized;
inline bool fake_restart = false;
inline unsigned fake_commits = 0;
inline WorkingConfigSyncResult syncPendingWorkingConfig() { return fake_sync; }
inline const char* webWorkingCandidatePath() { return "/trailmate/config.web.tms"; }
inline bool webConfigurationRestartPending() { return fake_restart; }
inline bool validateWebWorkingCandidate()
{
    const auto& text = platform::esp::arduino_common::storage::fake_files[webWorkingCandidatePath()];
    return text.find("TMSET7\n") == 0 && text.find("END\n") != std::string::npos;
}
inline bool commitWebWorkingCandidate()
{
    using namespace platform::esp::arduino_common::storage;
    if (!validateWebWorkingCandidate() || fake_write_failure) return false;
    fake_file = fake_files[webWorkingCandidatePath()];
    fake_files.erase(webWorkingCandidatePath());
    fake_restart = true;
    ++fake_commits;
    return true;
}
} // namespace app::sd_tms
