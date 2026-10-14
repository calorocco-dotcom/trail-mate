#include "platform/esp/idf_common/storage_runtime.h"

#include "chat/infra/mesh_peer_directory_core.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "platform/esp/arduino_common/chat/infra/store/sd_store.h"
#include "platform/esp/arduino_common/geocaching/browse_runtime.h"
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "platform/esp/boards/board_runtime.h"
#include "platform/esp/common/memory_budget.h"
#include "platform/esp/common/storage/storage_maintenance_owner.h"
#include "platform/ui/screen_runtime.h"

#include <cstdint>
#include <cstring>

namespace platform::esp::idf_common::storage
{
namespace
{

constexpr const char* kTag = "idf-storage";
constexpr uint32_t kStackBytes = 8U * 1024U;
constexpr std::size_t kInternalReservation = kStackBytes;
constexpr std::size_t kInternalFloor = 40U * 1024U;
constexpr std::size_t kMaxPeerPayloadBytes = 768U * 1024U;

using Owner = platform::esp::common::storage::StorageMaintenanceOwner;
using OwnerConfig =
    platform::esp::common::storage::StorageMaintenanceOwnerConfig;
using Adapter = platform::esp::common::storage::ISemanticStorageAdapter;
using Operation = platform::esp::common::storage::StorageOperation;
using OperationGeneration =
    platform::esp::common::storage::StorageOperationGeneration;
using Result = platform::esp::common::storage::StorageOperationResult;
using ResultKind =
    platform::esp::common::storage::StorageOperationResultKind;
namespace geo = platform::esp::arduino_common::geocaching::browse_runtime;

class PsramBlobSink final : public chat::IMeshPeerDirectoryBlobSink
{
  public:
    PsramBlobSink(uint8_t*& out, std::size_t& out_size)
        : out_(out), out_size_(out_size)
    {
    }

    bool begin(std::size_t expected_size) override
    {
        release();
        if (expected_size > kMaxPeerPayloadBytes)
        {
            return false;
        }
        if (expected_size == 0U)
        {
            return true;
        }
        out_ = static_cast<uint8_t*>(
            heap_caps_malloc(expected_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!out_)
        {
            return false;
        }
        capacity_ = expected_size;
        return true;
    }

    bool write(const uint8_t* data, std::size_t len) override
    {
        if (len == 0U)
        {
            return true;
        }
        if (!data)
        {
            return false;
        }
        if (out_size_ > capacity_ || len > capacity_ - out_size_)
        {
            return false;
        }
        std::memcpy(out_ + out_size_, data, len);
        out_size_ += len;
        return true;
    }

    bool finish() override { return out_size_ == capacity_; }

  private:
    void release()
    {
        if (out_)
        {
            heap_caps_free(out_);
            out_ = nullptr;
        }
        out_size_ = 0U;
        capacity_ = 0U;
    }

    uint8_t*& out_;
    std::size_t& out_size_;
    std::size_t capacity_ = 0U;
};

class PsramPayload final
{
  public:
    ~PsramPayload() { release(); }

    bool allocate(std::size_t size)
    {
        release();
        if (size == 0U || size > kMaxPeerPayloadBytes)
        {
            return false;
        }
        data_ = static_cast<uint8_t*>(
            heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!data_)
        {
            return false;
        }
        size_ = size;
        return true;
    }

    void release()
    {
        if (data_)
        {
            heap_caps_free(data_);
            data_ = nullptr;
        }
        size_ = 0U;
    }

    uint8_t* data() { return data_; }
    const uint8_t* data() const { return data_; }
    std::size_t size() const { return size_; }
    bool empty() const { return data_ == nullptr || size_ == 0U; }

  private:
    uint8_t* data_ = nullptr;
    std::size_t size_ = 0U;
};

chat::SdStore* s_store = nullptr;
chat::MeshPeerDirectoryCore* s_peer_directory = nullptr;
Owner s_owner{};

Result makeResult(Operation operation,
                  OperationGeneration generation,
                  bool ok)
{
    if (ok)
    {
        return Result::completedResult(operation, generation);
    }
    if (!platform::esp::arduino_common::storage::sd_card_ready())
    {
        return Result::failure(ResultKind::DeviceUnavailable,
                               operation,
                               generation);
    }
    return Result::failure(ResultKind::IoError, operation, generation);
}

class SdMaintenanceAdapter final : public Adapter
{
  public:
    SdMaintenanceAdapter(chat::SdStore*& store,
                         chat::MeshPeerDirectoryCore*& peer_directory)
        : store_(store), peer_directory_(peer_directory)
    {
    }

    Result begin(Operation operation, OperationGeneration generation) override
    {
        if (operation == Operation::Hydrate)
        {
            return beginHydration(generation);
        }
        if (operation == Operation::Persist)
        {
            // A retry continues its selected owner. Reuse the existing
            // persistence generation field while the peer payload is empty.
            if (geocaching_steps_ && peer_payload_generation_ == generation)
                return Result::inProgressResult(operation, generation);
            const bool peer_pending = peer_directory_ && peer_directory_->persistencePending();
            if (geo::workPending() && (geocaching_turn_ || !peer_pending))
            {
                peer_payload_generation_ = generation;
                geocaching_steps_ = 8;
                geocaching_turn_ = false;
                return Result::inProgressResult(operation, generation);
            }
            geocaching_turn_ = true;
            return beginPeerPersistence(generation);
        }
        return execute(operation, generation);
    }

    Result step(Operation operation,
                OperationGeneration generation,
                const platform::esp::common::storage::StorageOperationBudget&
                    budget) override
    {
        if (operation == Operation::Hydrate)
        {
            return stepHydration(generation, budget);
        }
        if (operation == Operation::Persist)
        {
            if (geocaching_steps_ && peer_payload_generation_ == generation)
            {
                if (geo::workPending()) geo::step();
                if (--geocaching_steps_ && geo::workPending())
                    return Result::inProgressResult(operation, generation);
                geocaching_steps_ = 0;
                peer_payload_generation_ = 0;
                return Result::completedResult(operation, generation);
            }
            return stepPeerPersistence(generation);
        }
        if (!store_)
        {
            return Result::failure(ResultKind::DeviceUnavailable,
                                   operation,
                                   generation);
        }
        return store_->stepMaintenance(operation, generation, budget);
    }

    void cancelAtStepBoundary(Operation operation,
                              OperationGeneration generation) override
    {
        if (store_)
        {
            store_->cancelMaintenance(operation, generation);
        }
        if (operation == Operation::Persist)
        {
            geocaching_steps_ = 0;
            peer_payload_.release();
            peer_payload_generation_ = 0U;
            peer_payload_revision_ = 0U;
        }
        if (operation == Operation::Hydrate)
        {
            releasePeerHydrationPayload();
            peer_hydration_generation_ = 0U;
            peer_hydration_store_in_progress_ = false;
            peer_hydration_pending_ = false;
        }
    }

  private:
    Result beginHydration(OperationGeneration generation)
    {
        const bool resume =
            peer_hydration_generation_ == generation &&
            (peer_hydration_store_in_progress_ || peer_hydration_pending_);
        if (resume)
        {
            if (!peer_hydration_store_in_progress_)
            {
                return Result::inProgressResult(Operation::Hydrate,
                                                generation);
            }
            if (!store_)
            {
                return Result::failure(ResultKind::DeviceUnavailable,
                                       Operation::Hydrate,
                                       generation);
            }
            const Result store_result =
                store_->beginMaintenance(Operation::Hydrate, generation);
            if (!store_result.inProgress() && !store_result.completed())
            {
                if (!store_result.retryable())
                {
                    releasePeerHydrationPayload();
                    peer_hydration_pending_ = false;
                    peer_hydration_store_in_progress_ = false;
                }
                return store_result;
            }
            peer_hydration_store_in_progress_ =
                store_result.inProgress();
            if (!peer_hydration_store_in_progress_ &&
                !peer_hydration_pending_)
            {
                return Result::completedResult(Operation::Hydrate,
                                               generation);
            }
            return Result::inProgressResult(Operation::Hydrate, generation);
        }

        releasePeerHydrationPayload();
        peer_hydration_generation_ = generation;
        peer_hydration_store_in_progress_ = false;
        peer_hydration_pending_ = false;

        if (peer_directory_)
        {
            PsramBlobSink sink(peer_hydration_payload_,
                               peer_hydration_payload_size_);
            const chat::MeshPeerDirectoryBlobLoadResult loaded =
                peer_directory_->streamPersistenceBlob(sink);
            if (loaded == chat::MeshPeerDirectoryBlobLoadResult::Unavailable)
            {
                releasePeerHydrationPayload();
                return Result::failure(ResultKind::DeviceUnavailable,
                                       Operation::Hydrate,
                                       generation);
            }
            if (loaded == chat::MeshPeerDirectoryBlobLoadResult::IoError)
            {
                releasePeerHydrationPayload();
                return Result::failure(ResultKind::IoError,
                                       Operation::Hydrate,
                                       generation);
            }
            if (loaded == chat::MeshPeerDirectoryBlobLoadResult::Loaded &&
                peer_hydration_payload_size_ != 0U)
            {
                peer_hydration_pending_ = true;
            }
        }

        if (!store_)
        {
            if (peer_hydration_pending_)
            {
                return Result::inProgressResult(Operation::Hydrate,
                                                generation);
            }
            return Result::completedResult(Operation::Hydrate, generation);
        }

        const Result store_result =
            store_->beginMaintenance(Operation::Hydrate, generation);
        if (!store_result.inProgress() && !store_result.completed())
        {
            releasePeerHydrationPayload();
            peer_hydration_pending_ = false;
            return store_result;
        }
        peer_hydration_store_in_progress_ = store_result.inProgress();
        if (!peer_hydration_store_in_progress_ && !peer_hydration_pending_)
        {
            return Result::completedResult(Operation::Hydrate, generation);
        }
        return Result::inProgressResult(Operation::Hydrate, generation);
    }

    Result stepHydration(
        OperationGeneration generation,
        const platform::esp::common::storage::StorageOperationBudget& budget)
    {
        if (peer_hydration_generation_ != generation)
        {
            return Result::failure(ResultKind::StaleGeneration,
                                   Operation::Hydrate,
                                   generation);
        }

        if (peer_hydration_store_in_progress_)
        {
            if (!store_)
            {
                return Result::failure(ResultKind::DeviceUnavailable,
                                       Operation::Hydrate,
                                       generation);
            }
            const Result store_result =
                store_->stepMaintenance(Operation::Hydrate, generation, budget);
            if (!store_result.inProgress())
            {
                if (!store_result.completed())
                {
                    if (!store_result.retryable())
                    {
                        releasePeerHydrationPayload();
                        peer_hydration_pending_ = false;
                        peer_hydration_store_in_progress_ = false;
                    }
                    return store_result;
                }
                peer_hydration_store_in_progress_ = false;
            }
            else
            {
                return store_result;
            }
        }

        if (peer_hydration_pending_)
        {
            if (!peer_directory_)
            {
                return Result::failure(ResultKind::DeviceUnavailable,
                                       Operation::Hydrate,
                                       generation);
            }
            const chat::MeshPeerDirectoryStatus status =
                peer_directory_->hydratePersistenceBlob(
                    peer_hydration_payload_,
                    peer_hydration_payload_size_);
            releasePeerHydrationPayload();
            peer_hydration_pending_ = false;
            if (!status.succeeded())
            {
                return Result::failure(ResultKind::IoError,
                                       Operation::Hydrate,
                                       generation);
            }
        }

        return Result::completedResult(Operation::Hydrate, generation);
    }

    Result execute(Operation operation, OperationGeneration generation)
    {
        if (!store_)
        {
            return Result::failure(ResultKind::DeviceUnavailable,
                                   operation,
                                   generation);
        }

        if (operation != Operation::Hydrate &&
            operation != Operation::Compact)
        {
            return Result::failure(ResultKind::Cancelled,
                                   operation,
                                   generation);
        }
        return store_->beginMaintenance(operation, generation);
    }

    Result beginPeerPersistence(OperationGeneration generation)
    {
        if (!peer_directory_)
        {
            return Result::failure(ResultKind::DeviceUnavailable,
                                   Operation::Persist,
                                   generation);
        }
        if (!peer_directory_->persistencePending())
        {
            return Result::completedResult(Operation::Persist, generation);
        }

        const std::size_t snapshot_size =
            peer_directory_->persistenceSnapshotSize();
        if (snapshot_size == 0U || snapshot_size > kMaxPeerPayloadBytes)
        {
            return Result::failure(ResultKind::IoError,
                                   Operation::Persist,
                                   generation);
        }

        if (!peer_payload_.allocate(snapshot_size))
        {
            return Result::failure(ResultKind::RetryLater,
                                   Operation::Persist,
                                   generation);
        }
        uint32_t revision = 0U;
        if (!peer_directory_->encodePersistenceSnapshot(peer_payload_.data(),
                                                        peer_payload_.size(),
                                                        &revision))
        {
            peer_payload_.release();
            return Result::failure(ResultKind::RetryLater,
                                   Operation::Persist,
                                   generation);
        }
        peer_payload_generation_ = generation;
        peer_payload_revision_ = revision;
        return Result::inProgressResult(Operation::Persist, generation);
    }

    Result stepPeerPersistence(OperationGeneration generation)
    {
        if (!peer_directory_ || peer_payload_generation_ != generation ||
            peer_payload_.empty())
        {
            return Result::failure(ResultKind::StaleGeneration,
                                   Operation::Persist,
                                   generation);
        }

        if (peer_directory_->persistenceRevision() !=
            peer_payload_revision_)
        {
            peer_payload_.release();
            return Result::failure(ResultKind::RetryLater,
                                   Operation::Persist,
                                   generation);
        }

        const bool saved = peer_directory_->persistEncodedSnapshot(
            peer_payload_.data(), peer_payload_.size(), peer_payload_revision_);
        peer_payload_.release();
        peer_payload_generation_ = 0U;
        peer_payload_revision_ = 0U;
        if (saved)
        {
            return Result::completedResult(Operation::Persist, generation);
        }
        return makeResult(Operation::Persist, generation, false);
    }

    chat::SdStore*& store_;
    chat::MeshPeerDirectoryCore*& peer_directory_;
    PsramPayload peer_payload_{};
    OperationGeneration peer_payload_generation_ = 0U;
    uint32_t peer_payload_revision_ = 0U;
    uint8_t* peer_hydration_payload_ = nullptr;
    std::size_t peer_hydration_payload_size_ = 0U;
    OperationGeneration peer_hydration_generation_ = 0U;
    bool peer_hydration_store_in_progress_ = false;
    bool peer_hydration_pending_ = false;
    // Occupy the existing tail alignment padding (target object remains 44 B).
    uint8_t geocaching_steps_ = 0;
    bool geocaching_turn_ = false;

    void releasePeerHydrationPayload()
    {
        if (peer_hydration_payload_)
        {
            heap_caps_free(peer_hydration_payload_);
            peer_hydration_payload_ = nullptr;
        }
        peer_hydration_payload_size_ = 0U;
    }
};

SdMaintenanceAdapter s_adapter(s_store, s_peer_directory);

bool admitOwner(void*)
{
    return platform::esp::common::memory::admit("idf_storage_owner",
                                                kInternalReservation,
                                                0,
                                                0,
                                                kInternalFloor,
                                                0);
}

uint32_t ownerNow(void*)
{
    return static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

const char* operationName(Operation operation)
{
    if (operation == Operation::Hydrate)
    {
        return "hydrate";
    }
    if (operation == Operation::Persist)
    {
        return "persist";
    }
    return "compact";
}

void ownerStarted(void*,
                  Operation operation,
                  OperationGeneration generation)
{
    ESP_LOGI(kTag,
             "owner begin mode=%s generation=%lu",
             operationName(operation),
             static_cast<unsigned long>(generation));
}

void ownerFinished(void*,
                   Operation operation,
                   OperationGeneration generation,
                   ResultKind result,
                   uint32_t elapsed_ms,
                   uint32_t stack_free_bytes)
{
    ESP_LOGI(kTag,
             "owner end mode=%s generation=%lu ok=%u result=%u "
             "elapsed_ms=%lu stack_free_bytes=%lu",
             operationName(operation),
             static_cast<unsigned long>(generation),
             (result == ResultKind::Completed ||
              result == ResultKind::InProgress)
                 ? 1U
                 : 0U,
             static_cast<unsigned>(result),
             static_cast<unsigned long>(elapsed_ms),
             static_cast<unsigned long>(stack_free_bytes));
}

bool startupGateSatisfied()
{
    return platform::esp::boards::storageStartupGateSatisfied();
}

} // namespace

void start_deferred_storage(chat::SdStore* store,
                            chat::MeshPeerDirectoryCore* peer_directory)
{
    if ((!store && !peer_directory) || s_owner.isArmed())
    {
        return;
    }

    s_store = store;
    s_peer_directory = peer_directory;

    OwnerConfig config{};
    config.task_name = "idf_storage_owner";
    config.stack_bytes = kStackBytes;
    config.priority = 1;
    config.core = 1;
    config.startup_gate =
        platform::esp::boards::storageCapabilities()
                .requiresDisplayTransactionGate()
            ? platform::esp::common::storage::StorageStartupGate::
                  DisplayTransaction
            : platform::esp::common::storage::StorageStartupGate::Immediate;
    config.context = nullptr;
    config.adapter = &s_adapter;
    config.admit = &admitOwner;
    config.now = &ownerNow;
    config.on_started = &ownerStarted;
    config.on_finished = &ownerFinished;
    s_owner.configure(config);
    (void)s_owner.arm(ownerNow(nullptr), startupGateSatisfied());
}

void tick_deferred_storage()
{
    if (!s_owner.isArmed())
    {
        return;
    }

    platform::esp::common::storage::StorageMaintenanceDemand demand{};
    demand.persistence_pending =
        (s_peer_directory && s_peer_directory->persistencePending()) || geo::workPending();
    demand.compaction_pending =
        s_store && s_store->compactionPending();
    (void)s_owner.submitTick(ownerNow(nullptr),
                             platform::ui::screen::is_sleeping(),
                             platform::ui::screen::is_saver_active(),
                             startupGateSatisfied(),
                             demand);
}

void stop_deferred_storage()
{
    (void)s_owner.requestStop();
}

bool hydration_active()
{
    return s_owner.hydrationActive();
}

bool consume_hydration_ready()
{
    return s_owner.consumeHydrationReady();
}

} // namespace platform::esp::idf_common::storage
