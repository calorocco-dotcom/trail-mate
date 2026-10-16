#include "platform/esp/idf_common/reticulum_call_runtime_support.h"

#include "platform/esp/common/reticulum_call_audio_engine.h"
#include "platform/ui/audio/call_notification_tone.h"
#include "platform/ui/audio/pager_notification_tone.h"
#include "platform/ui/gps_runtime.h"
#include "platform/ui/reticulum_call_runtime.h"
#include "platform/ui/wifi_access_runtime.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>

#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
#include "boards/t_display_p4/t_display_p4_board.h"
#include "bsp/trail_mate_t_display_p4_runtime.h"
#endif

namespace platform::esp::idf_common::reticulum_call_support
{
namespace
{

constexpr uint32_t kIncomingToneGapMs = 600;
constexpr uint32_t kIncomingToneStopTimeoutMs = 500;
constexpr uint32_t kIncomingToneTaskStackBytes = 5 * 1024;
constexpr UBaseType_t kIncomingToneTaskPriority = tskIDLE_PRIORITY + 1;
constexpr std::size_t kToneChunkFrames = 128;

bool s_registered = false;
bool s_resources_suspended = false;
TaskHandle_t s_incoming_tone_task = nullptr;
volatile bool s_incoming_tone_stop_requested = false;

#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
constexpr auto kCallOwner = TRAIL_MATE_T_DISPLAY_P4_AUDIO_OWNER_RETICULUM_CALL;
constexpr auto kNotificationOwner = TRAIL_MATE_T_DISPLAY_P4_AUDIO_OWNER_NOTIFICATION;
std::atomic<uint8_t> s_call_volume{100};
uint8_t call_volume() { return s_call_volume.load(); }

bool media_supported()
{
    return ::boards::t_display_p4::TDisplayP4Board::hasAudio();
}

bool open_call_audio(uint32_t sample_rate_hz)
{
    return trail_mate_t_display_p4_audio_begin(kCallOwner, sample_rate_hz);
}

void close_call_audio()
{
    trail_mate_t_display_p4_audio_end(kCallOwner);
}

bool read_call_audio(int16_t* pcm, std::size_t sample_count)
{
    return trail_mate_t_display_p4_audio_read_mono(kCallOwner, pcm, sample_count);
}

bool write_call_audio(const int16_t* pcm, std::size_t sample_count)
{
    return trail_mate_t_display_p4_audio_write_mono(kCallOwner, pcm, sample_count);
}

uint8_t speaker_volume()
{
    return ::boards::t_display_p4::TDisplayP4Board::instance().getMessageToneVolume();
}

void set_backend_volume(uint8_t volume_percent)
{
    const uint8_t volume = volume_percent > 100U ? 100U : volume_percent;
    s_call_volume.store(volume);
    (void)trail_mate_t_display_p4_audio_set_session_volume(kCallOwner, volume);
}
#else
bool media_supported()
{
    return false;
}

bool open_call_audio(uint32_t)
{
    return false;
}

void close_call_audio()
{
}

bool read_call_audio(int16_t*, std::size_t)
{
    return false;
}

bool write_call_audio(const int16_t*, std::size_t)
{
    return false;
}

uint8_t speaker_volume()
{
    return 0;
}
uint8_t call_volume() { return 0; }

void set_backend_volume(uint8_t)
{
}
#endif

void suspend_resources()
{
    if (s_resources_suspended)
    {
        return;
    }
    ::platform::ui::gps::suspend_runtime();
    s_resources_suspended = true;
}

void resume_resources()
{
    if (!s_resources_suspended)
    {
        return;
    }
    ::platform::ui::gps::resume_runtime();
    s_resources_suspended = false;
}

void incoming_tone_task(void*)
{
    while (!s_incoming_tone_stop_requested &&
           ::platform::ui::reticulum_call::realtime_phase() ==
               ::platform::ui::reticulum_call::RealtimePhase::IncomingRinging)
    {
        (void)play_incoming_notification(&s_incoming_tone_stop_requested);
        const TickType_t gap_started = xTaskGetTickCount();
        while (!s_incoming_tone_stop_requested &&
               ::platform::ui::reticulum_call::realtime_phase() ==
                   ::platform::ui::reticulum_call::RealtimePhase::IncomingRinging &&
               pdTICKS_TO_MS(xTaskGetTickCount() - gap_started) < kIncomingToneGapMs)
        {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
    s_incoming_tone_task = nullptr;
    vTaskDelete(nullptr);
}

bool start_incoming_tone()
{
    if (s_incoming_tone_task)
    {
        return true;
    }
    s_incoming_tone_stop_requested = false;
    const BaseType_t result = xTaskCreate(incoming_tone_task,
                                          "rt_call_ring",
                                          kIncomingToneTaskStackBytes,
                                          nullptr,
                                          kIncomingToneTaskPriority,
                                          &s_incoming_tone_task);
    if (result != pdPASS)
    {
        s_incoming_tone_task = nullptr;
        return false;
    }
    return true;
}

bool stop_incoming_tone()
{
    s_incoming_tone_stop_requested = true;
    const TaskHandle_t task = s_incoming_tone_task;
    if (!task)
    {
        return true;
    }
    if (xTaskGetCurrentTaskHandle() == task)
    {
        return false;
    }

    const TickType_t started = xTaskGetTickCount();
    while (s_incoming_tone_task &&
           pdTICKS_TO_MS(xTaskGetTickCount() - started) < kIncomingToneStopTimeoutMs)
    {
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    return s_incoming_tone_task == nullptr;
}

bool begin_soft_preempt(
    const uint8_t link_id[::platform::ui::reticulum_call::kHashSize])
{
    suspend_resources();
    return ::platform::ui::wifi_access::enter_call_ringing(link_id);
}

bool begin_ringing_alert(
    const uint8_t link_id[::platform::ui::reticulum_call::kHashSize])
{
    (void)link_id;
    const bool started = start_incoming_tone();
    if (!started)
    {
        std::printf("[Reticulum][CallTone] start failed\n");
    }
    return started;
}

bool begin_exclusive(const uint8_t link_id[::platform::ui::reticulum_call::kHashSize])
{
    const bool was_ringing =
        ::platform::ui::reticulum_call::realtime_phase() ==
        ::platform::ui::reticulum_call::RealtimePhase::IncomingRinging;
    if (!stop_incoming_tone())
    {
        std::printf("[Reticulum][CallTone] stop timeout\n");
        return false;
    }
    if (!::platform::ui::wifi_access::enter_call_exclusive(link_id))
    {
        if (was_ringing)
        {
            (void)start_incoming_tone();
        }
        return false;
    }
    suspend_resources();
    return true;
}

void begin_closing(const uint8_t link_id[::platform::ui::reticulum_call::kHashSize],
                   bool keep_exclusive)
{
    (void)stop_incoming_tone();
    suspend_resources();
    ::platform::ui::wifi_access::enter_call_closing(link_id, keep_exclusive);
}

void end_realtime(const uint8_t link_id[::platform::ui::reticulum_call::kHashSize])
{
    (void)stop_incoming_tone();
    ::platform::ui::wifi_access::exit_call(link_id);
    resume_resources();
}

} // namespace

void ensure_registered()
{
    if (s_registered)
    {
        return;
    }

    ::platform::esp::common::reticulum_call_audio::Backend audio_backend{};
    audio_backend.is_supported = media_supported;
    audio_backend.open = open_call_audio;
    audio_backend.close = close_call_audio;
    audio_backend.read_mono = read_call_audio;
    audio_backend.write_mono = write_call_audio;
    audio_backend.speaker_volume = call_volume;
    audio_backend.set_speaker_volume = set_backend_volume;
    if (!::platform::esp::common::reticulum_call_audio::install_backend(audio_backend))
    {
        std::printf("[Reticulum][CallAudio] backend registration failed\n");
        return;
    }

    ::platform::ui::reticulum_call::RealtimeHooks realtime_hooks{};
    realtime_hooks.begin_soft_preempt = begin_soft_preempt;
    realtime_hooks.begin_ringing_alert = begin_ringing_alert;
    realtime_hooks.begin_exclusive = begin_exclusive;
    realtime_hooks.begin_closing = begin_closing;
    realtime_hooks.end = end_realtime;
    ::platform::ui::reticulum_call::set_realtime_hooks(realtime_hooks);
    s_registered = true;
}

void set_speaker_volume(uint8_t volume_percent)
{
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    const auto volume = std::min<uint8_t>(volume_percent, 100);
    ::boards::t_display_p4::TDisplayP4Board::instance().setMessageToneVolume(volume);
    trail_mate_t_display_p4_audio_set_volume_percent(volume);
#else
    (void)volume_percent;
#endif
}

bool play_message_notification()
{
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    namespace tone = ::platform::ui::audio::pager_notification;
    if (speaker_volume() == 0 ||
        !trail_mate_t_display_p4_audio_begin(kNotificationOwner,
                                             tone::kPlaybackSampleRateHz))
    {
        return false;
    }

    std::array<int16_t, kToneChunkFrames> pcm{};
    tone::AdpcmPlaybackState state{};
    bool success = true;
    while (tone::hasMore(state))
    {
        std::size_t frames = 0;
        while (frames < pcm.size() && tone::nextPlaybackSample(state, pcm[frames]))
        {
            ++frames;
        }
        if (frames == 0)
        {
            break;
        }
        if (!trail_mate_t_display_p4_audio_write_mono(kNotificationOwner,
                                                      pcm.data(),
                                                      frames))
        {
            success = false;
            break;
        }
    }
    trail_mate_t_display_p4_audio_end(kNotificationOwner);
    return success;
#else
    return false;
#endif
}

bool play_incoming_notification(const volatile bool* stop_requested)
{
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    namespace tone = ::platform::ui::audio::call_notification;
    if ((stop_requested && *stop_requested) || speaker_volume() == 0 ||
        !trail_mate_t_display_p4_audio_begin(kNotificationOwner,
                                             tone::kPlaybackSampleRateHz))
    {
        return false;
    }

    std::array<int16_t, kToneChunkFrames> pcm{};
    tone::AdpcmPlaybackState state{};
    bool success = true;
    while (tone::hasMore(state) && (!stop_requested || !*stop_requested))
    {
        std::size_t frames = 0;
        while (frames < pcm.size() && tone::nextPlaybackSample(state, pcm[frames]))
        {
            ++frames;
        }
        if (frames == 0)
        {
            break;
        }
        if (!trail_mate_t_display_p4_audio_write_mono(kNotificationOwner,
                                                      pcm.data(),
                                                      frames))
        {
            success = false;
            break;
        }
    }
    trail_mate_t_display_p4_audio_end(kNotificationOwner);
    return success;
#else
    (void)stop_requested;
    return false;
#endif
}

} // namespace platform::esp::idf_common::reticulum_call_support
