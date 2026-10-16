#include "platform/ui/music_runtime.h"
#include "music/service.h"
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4) || defined(ARDUINO_T_LORA_PAGER) || defined(ARDUINO_T_DECK) || defined(ARDUINO_WIO_TRACKER_L2)
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "music/cover_decoder.h"
#include "music/player.h"
#include "platform/audio/audio_focus.h"
#include "platform/audio/media_volume.h"
#include "platform/esp/common/music/music_ports.h"
#include "platform/ui/device_runtime.h"
#include "platform/ui/settings_store.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <new>
namespace platform::esp::music_runtime
{
namespace
{
class Service;
struct AudioState
{
    std::atomic<bool> opening{false}, active{false};
};
class Output final : public ::music::AudioOutput
{
    AudioState& shared_;
    TickType_t last_write_ = 0;

  public:
    explicit Output(AudioState& state) : shared_(state) {}
    bool supported() const override { return audio_supported(); }
    bool blocked() const override { return ::platform::audio::music_blocked(); }
    bool needs_frame() const override { return last_write_ == 0 || pdTICKS_TO_MS(xTaskGetTickCount() - last_write_) >= 6; }
    bool begin(uint32_t rate) override
    {
        shared_.opening.store(true);
        const bool opened = !blocked() && audio_begin(rate);
        shared_.active.store(opened);
        shared_.opening.store(false);
        return opened;
    }
    bool write(const int16_t* samples, std::size_t count) override
    {
        if (blocked() || !audio_write(samples, count)) return false;
        last_write_ = xTaskGetTickCount();
        return true;
    }
    void end() override
    {
        audio_end();
        shared_.active.store(false);
    }
};
class Images final : public ::music::ImageLoader
{
    Service* service_ = nullptr;
    bool (*pump_)(void*) = nullptr;
    void* context_ = nullptr;
    uint16_t edge_ = 0;
    static bool pump(void* context);

  public:
    explicit Images(Service* service) : service_(service) {}
    ::music::Artwork* decode(::music::Reader& input, const ::music::Track& track, uint16_t edge, bool (*pump)(void*), void* context, ::music::CoverResult& result) override;
};
struct Runtime
{
    SdStorage storage;
    Output output;
    Images images;
    ::music::Player player;
    ::music::Request command{};
    bool volume_dirty = false;
    uint32_t volume_changed = 0;
    explicit Runtime(Service* service, AudioState& audio) : output(audio), images(service), player(storage, output, images) {}
};
class Service final : public ::music::IService
{
    StaticSemaphore_t mutex_storage_{};
    SemaphoreHandle_t mutex_ = nullptr;
    TaskHandle_t worker_ = nullptr;
    Runtime* runtime_ = nullptr;
    ::music::Snapshot* published_ = nullptr;
    uint32_t published_session_ = 0;
    ::music::ResumeState* resume_ = nullptr;
    ::music::Artwork* artwork_ = nullptr;
    ::music::Request* requests_ = nullptr;
    uint8_t read_ = 0, count_ = 0, rows_ = 0;
    bool visible_ = false, lyrics_ = false, properties_dirty_ = false;
    std::atomic<uint16_t> edge_{0};
    std::atomic<uint8_t> volume_{45};
    AudioState audio_;
    enum class Failure : uint8_t
    {
        None,
        Memory,
        Task,
        Queue
    };
    Failure failure_ = Failure::None;
    struct Lock
    {
        Service& service;
        bool held;
        explicit Lock(Service& value, TickType_t wait = portMAX_DELAY) : service(value), held(xSemaphoreTake(value.mutex_, wait) == pdTRUE) {}
        ~Lock()
        {
            if (held) xSemaphoreGive(service.mutex_);
        }
        explicit operator bool() const { return held; }
    };
    void start_locked()
    {
        if (worker_)
        {
            xTaskNotifyGive(worker_);
            return;
        }
        if (xTaskCreate(task, "music", 4096, this, tskIDLE_PRIORITY + 1, &worker_) != pdPASS)
        {
            worker_ = nullptr;
            failure_ = Failure::Task;
        }
    }
    static void task(void* context)
    {
        static_cast<Service*>(context)->run();
        vTaskDelete(nullptr);
    }
    void publish(Runtime& runtime)
    {
        ::music::Artwork* old = nullptr;
        {
            Lock lock(*this);
            if (!published_) published_ = ::music::make<::music::Snapshot>();
            if (published_)
            {
                *published_ = runtime.player.state;
                published_session_ = sd::sd_media_session();
            }
            else failure_ = Failure::Memory;
            auto* art = runtime.player.artwork();
            if (art != artwork_)
            {
                if (art) art->retain();
                old = artwork_;
                artwork_ = art;
            }
        }
        if (old) old->drop();
    }
    Runtime* finish_locked()
    {
        Runtime* retired = nullptr;
        if (runtime_ && !visible_)
        {
            if (runtime_->player.state.playback == ::music::Playback::Paused && runtime_->player.state.current.id)
            {
                if (!resume_) resume_ = ::music::make<::music::ResumeState>();
                if (resume_) runtime_->player.save_resume(*resume_);
            }
            else ::music::destroy(resume_);
            retired = runtime_;
            runtime_ = nullptr;
            ::music::destroy(published_);
            if (artwork_)
            {
                artwork_->drop();
                artwork_ = nullptr;
            }
        }
        ::music::release(requests_);
        requests_ = nullptr;
        read_ = count_ = 0;
        worker_ = nullptr;
        return retired;
    }
    void run()
    {
        Runtime* runtime = nullptr;
        ::music::ResumeState* saved = nullptr;
        {
            // start_locked holds this mutex through xTaskCreate's handle publication.
            Lock lock(*this);
            if (!runtime_) runtime_ = ::music::make<Runtime>(this, audio_);
            runtime = runtime_;
            if (!runtime)
            {
                failure_ = Failure::Memory;
                finish_locked();
                return;
            }
            // Transfer ownership before restore performs SD I/O. A concurrent
            // UI snapshot may invalidate its own saved session after a card change.
            if (resume_ && runtime->player.state.current.id == 0)
            {
                // Publish the existing session before transferring it, so an
                // entry snapshot cannot briefly route a paused user to Library.
                if (!published_) published_ = ::music::make<::music::Snapshot>();
                if (published_)
                {
                    published_->current = resume_->current;
                    published_->position_ms = resume_->position_ms;
                    published_->mode = resume_->mode;
                    published_->playback = ::music::Playback::Paused;
                    published_session_ = resume_->media_session;
                }
                else failure_ = Failure::Memory;
                saved = resume_;
                resume_ = nullptr;
            }
        }
        if (saved) runtime->player.restore_resume(*saved);
        ::music::destroy(saved);
        runtime->player.state.volume = volume_.load();
        for (;;)
        {
            bool command = false, visible = false, lyrics = false;
            uint8_t rows = 0;
            uint16_t edge = 0;
            {
                Lock lock(*this);
                visible = visible_;
                rows = rows_;
                lyrics = lyrics_;
                edge = edge_.load();
                if (count_)
                {
                    runtime->command = requests_[read_];
                    read_ = (read_ + 1) % 8;
                    --count_;
                    command = true;
                }
                properties_dirty_ = false;
            }
            runtime->player.attach(visible ? rows : 0);
            runtime->player.request_artwork(visible ? edge : 0);
            runtime->player.request_lyrics(visible && lyrics);
            if (command)
            {
                if (runtime->command.action == ::music::Action::Volume && std::clamp<int32_t>(runtime->command.value, 0, 100) != volume_.load()) command = false;
            }
            if (command)
            {
                if (!runtime->player.command(runtime->command) && !runtime->player.state.notice[0])
                    ::music::copy_text(runtime->player.state.notice, sizeof(runtime->player.state.notice), "Music request could not be completed");
                if (runtime->command.action == ::music::Action::Volume)
                {
                    runtime->volume_dirty = true;
                    runtime->volume_changed = xTaskGetTickCount();
                    ::platform::ui::device::set_message_tone_volume(runtime->player.state.volume);
                }
            }
            runtime->player.state.volume = volume_.load();
            runtime->player.tick();
            publish(*runtime);
            const bool active = runtime->player.active();
            if (runtime->volume_dirty && !active && !::platform::audio::music_blocked() && (!visible || pdTICKS_TO_MS(xTaskGetTickCount() - runtime->volume_changed) >= 500))
            {
                ::platform::ui::settings_store::put_int("settings", "speaker_volume", volume_.load());
                runtime->volume_dirty = false;
            }
            Runtime* retired = nullptr;
            bool finished = false;
            {
                Lock lock(*this);
                if (!active && !runtime->volume_dirty && !count_ && !properties_dirty_)
                {
                    retired = finish_locked();
                    finished = true;
                }
            }
            // SD handle closure must never run under the UI publication mutex.
            ::music::destroy(retired);
            if (finished) return;
            if (!active || ::platform::audio::music_blocked()) ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
            // The synchronous output already waits for DMA space. An extra
            // tick here consumes the small hardware queue's decode/SD budget.
            // Yield to peers without imposing a second playback clock.
            else taskYIELD();
        }
    }
    void wake()
    {
        // Ending a notification must not instantiate a worker/SD index for a user-paused session.
        Lock lock(*this);
        if (worker_) xTaskNotifyGive(worker_);
    }
    static bool suspend(void* context)
    {
        auto& self = *static_cast<Service*>(context);
        if (!self.audio_.active.load() && !self.audio_.opening.load()) return true;
        self.wake();
        const auto started = xTaskGetTickCount();
        while (self.audio_.active.load() || self.audio_.opening.load())
        {
            if (pdTICKS_TO_MS(xTaskGetTickCount() - started) >= 350) return false;
            vTaskDelay(1);
        }
        return true;
    }
    static void resumed(void* context) { static_cast<Service*>(context)->wake(); }
    static void volume_changed(void* context, uint8_t percent)
    {
        auto& self = *static_cast<Service*>(context);
        percent = ::platform::audio::media_volume(percent);
        self.volume_.store(percent);
        Lock lock(self, pdMS_TO_TICKS(20));
        if (!lock) return;
        if (self.published_) self.published_->volume = percent;
        if (self.resume_) self.resume_->volume = percent;
        if (self.runtime_ && !self.worker_) self.runtime_->player.state.volume = percent;
        if (self.worker_) xTaskNotifyGive(self.worker_);
    }

  public:
    Service()
    {
        mutex_ = xSemaphoreCreateMutexStatic(&mutex_storage_);
        volume_.store(::platform::audio::media_volume(std::clamp(::platform::ui::settings_store::get_int("settings", "speaker_volume", 45), 0, 100)));
        ::platform::audio::observe_media_volume(volume_changed, this);
        ::platform::audio::register_music_focus_client({this, suspend, resumed});
    }
    uint16_t requested_edge() const { return edge_.load(); }
    bool artwork_wanted(uint16_t edge)
    {
        if (edge_.load() != edge) return false;
        Lock lock(*this, pdMS_TO_TICKS(5));
        return lock && visible_ && count_ == 0;
    }
    uint8_t volume() const override { return volume_.load(); }
    void attach(uint8_t rows) override
    {
        Lock lock(*this);
        visible_ = true;
        rows_ = std::min<uint8_t>(rows, ::music::kPageRows);
        properties_dirty_ = true;
        // An idle negative-screen widget observes a proxy without starting SD/audio/task work.
        if (rows_ || runtime_ || resume_) start_locked();
    }
    void detach() override
    {
        Lock lock(*this);
        visible_ = false;
        rows_ = 0;
        lyrics_ = false;
        edge_.store(0);
        properties_dirty_ = true;
        if (runtime_) start_locked();
    }
    void request_artwork(uint16_t edge) override
    {
        Lock lock(*this);
        if (edge_.load() == edge) return;
        edge_.store(edge);
        properties_dirty_ = true;
        if (runtime_ || resume_) start_locked();
    }
    void request_lyrics(bool visible) override
    {
        Lock lock(*this);
        if (lyrics_ == visible) return;
        lyrics_ = visible;
        properties_dirty_ = true;
        if (runtime_ || resume_) start_locked();
    }
    bool snapshot(::music::Snapshot& out) override
    {
        Lock lock(*this, pdMS_TO_TICKS(20));
        if (!lock) return false;
        if (resume_ && resume_->media_session != sd::sd_media_session()) ::music::destroy(resume_);
        if (published_ && published_session_ == sd::sd_media_session()) out = *published_;
        else
        {
            using Snapshot = ::music::Snapshot;
            out.~Snapshot();
            new (&out) Snapshot{};
            if (resume_)
            {
                out.current = resume_->current;
                out.position_ms = resume_->position_ms;
                out.mode = resume_->mode;
                out.playback = ::music::Playback::Paused;
            }
            out.storage_ready = sd::sd_card_ready() && !sd::sd_external_block_owner_active();
            out.audio_supported = audio_supported();
            out.volume = volume_.load();
        }
        out.storage_ready = sd::sd_card_ready() && !sd::sd_external_block_owner_active();
        out.volume = volume_.load();
        if (failure_ != Failure::None) ::music::copy_text(out.notice, sizeof(out.notice), failure_ == Failure::Memory ? "Not enough PSRAM for music" : failure_ == Failure::Task ? "Music worker is unavailable"
                                                                                                                                                                                 : "Music is busy. Try again.");
        return true;
    }
    bool send(const ::music::Request& request) override
    {
        if (request.action == ::music::Action::Volume)
        {
            bool cached = false;
            const auto value = std::clamp<int32_t>(request.value, 0, 100);
            {
                Lock lock(*this);
                if (!worker_)
                {
                    volume_.store(value);
                    if (runtime_) runtime_->player.state.volume = value;
                    if (published_) published_->volume = value;
                    if (resume_) resume_->volume = value;
                    cached = true;
                }
            }
            if (cached)
            {
                // An idle volume gesture needs no decoder, SD handles or task.
                ::platform::ui::device::set_message_tone_volume(value);
                ::platform::ui::settings_store::put_int("settings", "speaker_volume", value);
                return ::platform::ui::settings_store::get_int("settings", "speaker_volume", -1) == value;
            }
        }
        struct VolumePublication
        {
            bool accepted = false;
            uint8_t value = 0;
            ~VolumePublication()
            {
                if (accepted) ::platform::audio::publish_media_volume(value);
            }
        } publication; // Runs after the following lock has been released.
        Lock lock(*this);
        if (count_ == 8)
        {
            failure_ = Failure::Queue;
            return false;
        }
        if (!requests_)
        {
            requests_ = static_cast<::music::Request*>(::music::allocate(sizeof(::music::Request) * 8));
            if (requests_)
                for (unsigned i = 0; i < 8; i++) new (requests_ + i)::music::Request{};
        }
        if (!requests_)
        {
            failure_ = Failure::Memory;
            return false;
        }
        requests_[(read_ + count_) % 8] = request;
        ++count_;
        failure_ = Failure::None;
        start_locked();
        if (!worker_)
        {
            --count_;
            return false;
        }
        if (request.action == ::music::Action::Volume)
        {
            publication.value = std::clamp<int32_t>(request.value, 0, 100);
            volume_.store(publication.value);
            publication.accepted = true;
        }
        return true;
    }
    ::music::Artwork* acquire_artwork() override
    {
        Lock lock(*this);
        if (artwork_) artwork_->retain();
        return artwork_;
    }
};
bool Images::pump(void* context)
{
    auto& self = *static_cast<Images*>(context);
    return self.service_->artwork_wanted(self.edge_) && (!self.pump_ || self.pump_(self.context_));
}
::music::Artwork* Images::decode(::music::Reader& input, const ::music::Track& track, uint16_t edge, bool (*pump_callback)(void*), void* context, ::music::CoverResult& result)
{
    pump_ = pump_callback;
    context_ = context;
    edge_ = edge;
    auto* artwork = ::music::decode_cover(input, track, edge, result, pump, this);
    if (!service_->artwork_wanted(edge) || ::platform::audio::music_blocked())
    {
        if (artwork) artwork->drop();
        result = ::music::CoverResult::Cancelled;
        return nullptr;
    }
    return artwork;
}
} // namespace
::music::IService* resolve_service()
{
    static Service service;
    return &service;
}
} // namespace platform::esp::music_runtime
namespace platform::ui
{
::music::IService* music_service() { return ::platform::esp::music_runtime::resolve_service(); }
} // namespace platform::ui
#else
namespace platform::ui
{
::music::IService* music_service() { return nullptr; }
} // namespace platform::ui
#endif
