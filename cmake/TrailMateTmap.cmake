include_guard(GLOBAL)
include(FetchContent)

# Keep this release/commit aligned with PlatformIO's lib_deps pin. No sibling
# checkout or copied implementation is used by a normal build.
set(TRAILMATE_TMAP_VERSION "0.2.0")
set(TRAILMATE_TMAP_COMMIT "d74b084acb61653149dec1ac161df58a92278cff")
set(TRAILMATE_TMAP_ARCHIVE_SHA256 "50cda6657a184587a1901aba965b885b335d7bc332793fcbd90cbab2bcfce371")

function(trailmate_add_tmap)
    if(TARGET tmap::tmap)
        return()
    endif()
    if(POLICY CMP0135)
        cmake_policy(SET CMP0135 NEW)
    endif()
    set(TMAP_BUILD_TESTS OFF CACHE BOOL "Build SDK tests" FORCE)
    set(TMAP_BUILD_EXAMPLES OFF CACHE BOOL "Build SDK desktop examples" FORCE)
    set(TMAP_NATIVE_FILE OFF CACHE BOOL "Use application file adapters" FORCE)
    set(TMAP_INSTALL_TOOLS OFF CACHE BOOL "Do not install SDK host producer tools" FORCE)
    set(TMAP_EMBEDDED_CHECKS ON CACHE BOOL "No exceptions/RTTI; bounded stack reports" FORCE)
    FetchContent_Declare(trailmate_tmap
        URL "https://github.com/vicliu624/tmap/releases/download/v${TRAILMATE_TMAP_VERSION}/tmap-${TRAILMATE_TMAP_VERSION}-source.tar.gz"
        URL_HASH "SHA256=${TRAILMATE_TMAP_ARCHIVE_SHA256}")
    # ESP-IDF adds its runtime through directory-level link_libraries(). Keep
    # those private platform dependencies out of the portable SDK's exports,
    # then restore them for the application's subsequent targets.
    get_directory_property(trailmate_tmap_parent_links LINK_LIBRARIES)
    set_property(DIRECTORY PROPERTY LINK_LIBRARIES "")
    FetchContent_MakeAvailable(trailmate_tmap)
    set_property(DIRECTORY PROPERTY LINK_LIBRARIES "${trailmate_tmap_parent_links}")
endfunction()
