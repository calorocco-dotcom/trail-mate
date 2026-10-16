#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace platform::ui::settings_store
{

using ChangeObserver = void (*)(void* context, const char* ns, const char* key);

// One platform may subscribe to durable settings changes (for example, to
// refresh an SD working projection).  A batch coalesces multi-key owners such
// as the Wi-Fi profile set into one notification without retaining values.
void set_change_observer(ChangeObserver observer, void* context);
void begin_change_batch();
void end_change_batch();

class ScopedChangeBatch
{
  public:
    ScopedChangeBatch() { begin_change_batch(); }
    ~ScopedChangeBatch() { end_change_batch(); }

    ScopedChangeBatch(const ScopedChangeBatch&) = delete;
    ScopedChangeBatch& operator=(const ScopedChangeBatch&) = delete;
};

void put_int(const char* ns, const char* key, int value);
void put_bool(const char* ns, const char* key, bool value);
void put_uint(const char* ns, const char* key, uint32_t value);
bool put_string(const char* ns, const char* key, const char* value);
bool put_blob(const char* ns, const char* key, const void* data, std::size_t len);
int get_int(const char* ns, const char* key, int default_value);
bool get_bool(const char* ns, const char* key, bool default_value);
uint32_t get_uint(const char* ns, const char* key, uint32_t default_value);
bool get_string(const char* ns, const char* key, std::string& out);
// Bounded counterpart to get_string.  ESP persistence paths must prefer this
// overload so working-configuration sync never allocates a string sized by
// NVS input.
bool get_string_into(const char* ns,
                     const char* key,
                     char* out,
                     std::size_t capacity,
                     std::size_t* out_len);
enum class ScalarReadResult : uint8_t
{
    Ok,
    Missing,
    Invalid,
    Unavailable
};
// ESP strict projection reads; failed reads reset output and never use defaults.
ScalarReadResult read_int_into(const char* ns, const char* key, int32_t* out);
ScalarReadResult read_uint_into(const char* ns, const char* key, uint32_t* out);
ScalarReadResult read_u8_into(const char* ns, const char* key, uint8_t* out);
ScalarReadResult read_bool_into(const char* ns, const char* key, bool* out);

enum class StringReadResult : uint8_t
{
    Ok,
    Missing,
    Invalid,
    Unavailable
};
// ESP strict projection API: fallback is safe only for Missing.
StringReadResult read_string_into(const char* ns, const char* key, char* out,
                                  std::size_t capacity, std::size_t* out_len);
enum class BlobReadResult : uint8_t
{
    Ok,
    Missing,
    Invalid,
    Unavailable
};
// ESP strict counterpart: empty is valid; only Missing permits a default.
// Uses the caller's buffer and reports zero output length on every failure.
BlobReadResult read_blob_into(const char* ns, const char* key, void* out,
                              std::size_t capacity, std::size_t* out_len);
bool get_blob(const char* ns, const char* key, std::vector<uint8_t>& out);
bool get_blob_into(const char* ns,
                   const char* key,
                   void* out,
                   std::size_t capacity,
                   std::size_t* out_len);
void remove_keys(const char* ns, const char* const* keys, std::size_t key_count);
void clear_namespace(const char* ns);

} // namespace platform::ui::settings_store
