#pragma once
#include "runtime_environment.h"
namespace app
{
inline AppContext& appFacade() { return AppContext::getInstance(); }
}
