#pragma once
namespace restore_fake { inline bool admitted=true,dropped=false; }
namespace platform::esp::arduino_common::geocaching::browse_runtime
{
inline bool withIdentityMaintenance(bool(*action)(void*),void* context){if(!restore_fake::admitted)return false;restore_fake::dropped=action(context);return true;}
}
