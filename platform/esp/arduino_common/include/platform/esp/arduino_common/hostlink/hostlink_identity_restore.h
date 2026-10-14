#pragma once
#include "chat/ports/i_mesh_adapter.h"
#include "hostlink/identity_restore_service.h"
#include "hostlink/radio_identity_backup.h"
#include "platform/esp/arduino_common/geocaching/browse_runtime.h"
#include "platform/esp/arduino_common/hostlink/web_config_upload.h"
#include <esp_heap_caps.h>
#include <new>
namespace hostlink::management
{
constexpr size_t kIdentityRestoreLargestWork = sizeof(IdentityRestoreWork) > sizeof(RadioIdentityRestoreWork) ? sizeof(IdentityRestoreWork) : sizeof(RadioIdentityRestoreWork);
constexpr size_t kRestoreRuntimeWorkBytes = kIdentityRestoreLargestWork > sizeof(web_config::Metadata) + 111 ? kIdentityRestoreLargestWork : sizeof(web_config::Metadata) + 111;
static_assert(kRestoreRuntimeWorkBytes <= 512);
inline Status restoreIdentityOperation(chat::IMeshAdapter& adapter, const EnvelopeView& request,
                                       IdentityRestoreActivity activity, uint8_t* output, size_t capacity, size_t& written)
{
    written = 0;
    if (activity.busy()) return Status::Busy;
    void* memory = heap_caps_malloc(kRestoreRuntimeWorkBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!memory) return Status::Busy;
    struct Release
    {
        void* memory;
        ~Release()
        {
            volatile uint8_t* data = static_cast<volatile uint8_t*>(memory);
            for (size_t i = 0; i < kRestoreRuntimeWorkBytes; ++i) data[i] = 0;
            heap_caps_free(memory);
        }
    } release{memory};
    // The journal and request phases reuse the same raw allocation sequentially.
    auto* metadata = new (memory) web_config::Metadata;
    const bool loaded = web_config::load(*metadata, static_cast<uint8_t*>(memory) + sizeof(web_config::Metadata));
    activity.configuration = loaded && metadata->phase < (metadata->kind == 2 ? 4 : 2);
    metadata->~Metadata();
    if (!loaded && (platform::esp::arduino_common::storage::sd_exists(web_config::kMetaPath) ||
                    platform::esp::arduino_common::storage::sd_exists(web_config::kMetaNew) ||
                    platform::esp::arduino_common::storage::sd_exists(web_config::kMetaBackup))) return Status::StorageUnavailable;
    struct Context
    {
        chat::IMeshAdapter& adapter;
        const EnvelopeView& request;
        const IdentityRestoreActivity& activity;
        void* work;
        uint8_t* output;
        size_t capacity;
        size_t& written;
        Status status = Status::Busy;
    } context{adapter, request, activity, memory, output, capacity, written};
    const bool admitted = platform::esp::arduino_common::geocaching::browse_runtime::withIdentityMaintenance(
        [](void* opaque)
        {
            auto& state = *static_cast<Context*>(opaque);
            if (state.request.code == uint16_t(Operation::RadioIdentityBackupRestore))
            {
                auto* work = new (state.work) RadioIdentityRestoreWork;
                state.status = processRadioIdentityRestore(state.adapter, state.request, state.activity, *work,
                                                           state.output, state.capacity, state.written);
                work->~RadioIdentityRestoreWork();
            }
            else
            {
                auto* work = new (state.work) IdentityRestoreWork;
                state.status = processIdentityRestore(state.adapter, state.request, state.activity, *work,
                                                      state.output, state.capacity, state.written);
                work->~IdentityRestoreWork();
            }
            return state.status == Status::Ok;
        },
        &context);
    return admitted ? context.status : Status::Busy;
}
} // namespace hostlink::management
