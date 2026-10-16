#include "platform/sync/resource_lock.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
namespace platform::sync
{
namespace
{
portMUX_TYPE locks[] = {portMUX_INITIALIZER_UNLOCKED, portMUX_INITIALIZER_UNLOCKED};
}
void enter(Resource resource) { portENTER_CRITICAL(&locks[static_cast<unsigned>(resource)]); }
void leave(Resource resource) { portEXIT_CRITICAL(&locks[static_cast<unsigned>(resource)]); }
} // namespace platform::sync
