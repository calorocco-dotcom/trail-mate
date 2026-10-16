#pragma once
#include <cstddef>
namespace app
{
struct IAppLifecycleFacade
{
    void updateCoreServices();
    void tickEventRuntime();
    void dispatchPendingEvents(std::size_t);
};
bool hasAppFacade();
IAppLifecycleFacade& lifecycleFacade();
} // namespace app
