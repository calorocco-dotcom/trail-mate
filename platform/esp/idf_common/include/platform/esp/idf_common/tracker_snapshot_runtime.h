#pragma once
namespace platform::ui::tracker
{
// ESP implementations: succeeds with no live recording or in-flight mutation.
// Owner must release exactly once after copy or cleanup. While held, start,
// sampling and deletion yield; this does not lock external SD block owners.
bool try_acquire_snapshot_read();
void release_snapshot_read();
// Short-lived metadata listing may run during recording, with sampling yielded.
bool try_acquire_catalog_read();
bool is_recording_filename(const char* filename); // Caller holds catalog gate.
} // namespace platform::ui::tracker
