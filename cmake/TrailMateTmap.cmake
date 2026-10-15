include_guard(GLOBAL)
include(FetchContent)

# Keep this release/commit aligned with PlatformIO's lib_deps pin. No sibling
# checkout or copied implementation is used by a normal build.
set(TRAILMATE_TMAP_VERSION "0.1.1")
set(TRAILMATE_TMAP_COMMIT "d8fc21550859bce1c242e6330b0e9ae2ddf9eecc")
set(TRAILMATE_TMAP_ARCHIVE_SHA256 "d8377999b30703e870f9e5dab09f7de89eb6474cc01930fc3d330de6e85acb77")

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
    FetchContent_MakeAvailable(trailmate_tmap)
endfunction()
