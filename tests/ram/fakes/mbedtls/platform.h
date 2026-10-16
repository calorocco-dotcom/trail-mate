#pragma once
#include <cstddef>
inline void* (*installed_calloc)(size_t, size_t) = nullptr;
inline void (*installed_free)(void*) = nullptr;
inline int mbedtls_platform_set_calloc_free(void* (*allocate)(size_t, size_t), void (*release)(void*))
{
    installed_calloc = allocate;
    installed_free = release;
    return 0;
}
