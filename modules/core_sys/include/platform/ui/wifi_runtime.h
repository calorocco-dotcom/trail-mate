#pragma once

#include <cstddef>
#include <cstdint>

namespace platform::ui::wifi
{

constexpr std::size_t kMaxSsidLength = 32;
constexpr std::size_t kMaxPasswordLength = 64;
constexpr std::size_t kMaxSavedProfileCount = 10;
constexpr std::size_t kMaxIpLength = 47;
constexpr std::size_t kMaxStatusMessageLength = 95;

enum class ConnectionState : uint8_t
{
    Unsupported = 0,
    Disabled,
    Idle,
    Scanning,
    Connecting,
    Connected,
    Error,
    ResourceDeferred,
};

struct Config
{
    bool enabled = false;
    char ssid[kMaxSsidLength + 1] = {};
    char password[kMaxPasswordLength + 1] = {};
};

struct Status
{
    bool supported = false;
    bool enabled = false;
    bool connected = false;
    bool scanning = false;
    bool has_credentials = false;
    int rssi = -127;
    char ssid[kMaxSsidLength + 1] = {};
    char ip[kMaxIpLength + 1] = {};
    char message[kMaxStatusMessageLength + 1] = {};
    ConnectionState state = ConnectionState::Unsupported;
};

// Ownership token for a temporary external-storage session. This is runtime
// state only; it must never be persisted as the user's Wi-Fi preference.
struct ExternalStorageSuspension
{
    bool resume_required = false;
};

static_assert(sizeof(ExternalStorageSuspension) == 1U,
              "Wi-Fi external-storage suspension must stay metadata-only");

struct ScanResult
{
    char ssid[kMaxSsidLength + 1] = {};
    int rssi = -127;
    bool requires_password = true;
};

// Iteration keeps callers from materialising a ten-profile automatic object
// merely to serialize the persistent network set. `index == 0` is the highest
// connection priority.
using SavedProfileVisitor = bool (*)(void* context, std::size_t index, const Config& profile);

bool is_supported();
bool load_config(Config& out);
bool save_config(const Config& config);
bool find_saved_config(const char* ssid, Config& out);
bool visit_saved_profiles(bool* enabled,
                          std::size_t* count,
                          SavedProfileVisitor visitor,
                          void* context);
// ESP configuration projection: strict persistent reads, caller-owned staging
// of at least kMaxSavedProfileCount. Failure clears staging and output metadata.
// Does not modify connection caches or migrate legacy credential keys.
bool read_saved_profiles_into(bool* enabled, std::size_t* count,
                              Config* profiles, std::size_t capacity);
bool replace_saved_profiles(bool enabled, const Config* profiles, std::size_t count);
bool apply_enabled(bool enabled);
// User switch: preserve profile order/credentials, coordinate runtime clients,
// persist the preference, and roll it back if the transition cannot start.
bool set_user_enabled(bool enabled);
bool suspend_for_external_storage(ExternalStorageSuspension* out_suspension);
void resume_after_external_storage(ExternalStorageSuspension* suspension);
bool connect(const Config* override_config = nullptr);
void disconnect();
bool scan(ScanResult* out_results, std::size_t capacity, std::size_t& out_count);
Status status();

} // namespace platform::ui::wifi
