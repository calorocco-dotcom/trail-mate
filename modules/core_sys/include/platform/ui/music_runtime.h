#pragma once
namespace music
{
class IService;
}
namespace platform::ui
{
// Resolving the stable proxy does not create a task, touch SD or initialize audio.
::music::IService* music_service();
} // namespace platform::ui
