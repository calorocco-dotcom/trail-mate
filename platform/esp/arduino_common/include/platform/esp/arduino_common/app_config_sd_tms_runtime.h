/**
 * @file app_config_sd_tms_runtime.h
 * @brief SD-authoritative working configuration repository for ESP targets.
 */

#pragma once

#include "app/app_config.h"

#include <cstdint>

namespace app::sd_tms
{

enum class LoadResult : uint8_t
{
    Unavailable,
    Missing,
    Legacy,
    Invalid,
    Applied,
};

// Reads a complete current TMS working document in two passes. A valid legacy
// document is reported as Legacy but is not applied until NVS has supplied the
// migration baseline. Invalid current documents never fall back to NVS.
LoadResult loadWorkingConfig(AppConfig& config);
bool applyLegacyWorkingConfig(AppConfig& config);

// An invalid current document is a repair condition.  Startup must expose the
// SD card for repair instead of starting the normal application services.
bool workingConfigRequiresRepair();
void requireWorkingConfigRepair();

// Binds the one live AppConfig instance.  Independent Settings owners use the
// observer only to schedule a repository write; it never makes NVS authoritative.
void bindWorkingConfig(const AppConfig& config);
bool workingConfigIsBound();
void requestWorkingConfigSync();
void serviceWorkingConfig();

// A caller that must not continue until the current working configuration is
// durable can use this after a settings-store change.  It preserves the
// deferred retry when no SD card is available, while reporting an actual
// storage or repair failure to the caller.
enum class WorkingConfigSyncResult : uint8_t
{
    Synchronized,
    Deferred,
    Failed,
};
WorkingConfigSyncResult syncPendingWorkingConfig();

// Streams and validates a fresh canonical TMS document before a recoverable
// replacement of /trailmate/config.tms.
bool syncWorkingConfig(const AppConfig& config);

// A backup is a complete TMS document at /trailmate/backup/settings.tms.
// Restore validates it, transforms only the document kind into a candidate
// working file, and atomically promotes it.  It deliberately takes effect on
// the next boot, so no second AppConfig instance is needed.
bool backupWorkingConfig(const AppConfig& config);
bool restoreWorkingConfig();

// Web editing stages the existing canonical format on device storage without
// an AppConfig clone. These APIs must run on the application command consumer.
const char* webWorkingCandidatePath();
bool validateWebWorkingCandidate();
// Successful promotion is durable; the regular service tick restarts the
// device after allowing the USB reply to drain. No old-state sync may replace it.
bool commitWebWorkingCandidate();
bool webConfigurationRestartPending();
// Reuse the existing USB reply drain delay for an identity maintenance reboot.
void scheduleWebManagementRestart();

void beginWorkingConfigReset();
void endWorkingConfigReset();
bool resetWorkingConfig();

const char* loadResultName(LoadResult result);

} // namespace app::sd_tms
