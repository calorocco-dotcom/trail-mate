cmake_minimum_required(VERSION 3.20)
project(tmap_platform_link_isolation LANGUAGES CXX)

# Model ESP-IDF's directory-level runtime dependencies. An inherited link here
# makes the SDK's install(EXPORT) fail because this target is not exported.
add_library(platform_runtime INTERFACE)
link_libraries(platform_runtime)
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/TrailMateTmap.cmake")
trailmate_add_tmap()
get_directory_property(application_links LINK_LIBRARIES)
if(NOT application_links STREQUAL "platform_runtime")
    message(FATAL_ERROR "The application's platform links were not restored")
endif()
get_target_property(sdk_links tmap LINK_LIBRARIES)
if("platform_runtime" IN_LIST sdk_links)
    message(FATAL_ERROR "Platform links leaked into the portable SDK")
endif()
