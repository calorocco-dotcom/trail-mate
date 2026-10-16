cmake_minimum_required(VERSION 3.20)
project(p4_hardware_contract LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
get_filename_component(REPO "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(SOURCES
    "${REPO}/boards/t_display_p4/src/t_display_p4_board.cpp"
    "${REPO}/boards/t_display_p4/src/heading_runtime.cpp"
    "${REPO}/boards/t_display_p4/src/rtc_runtime.cpp"
    "${REPO}/platform/esp/idf_components/t_display_p4/trail_mate_t_display_p4_runtime.cpp"
    "${REPO}/platform/esp/idf_common/src/gps_runtime.cpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${SOURCES} extract.py)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/extract.py"
    "${REPO}" "${CMAKE_CURRENT_BINARY_DIR}" COMMAND_ERROR_IS_FATAL ANY)
enable_testing()
foreach(TEST power heading audio rtc haptic display)
    add_executable(p4_${TEST}_test test_${TEST}.cpp)
    target_include_directories(p4_${TEST}_test PRIVATE fakes "${CMAKE_CURRENT_BINARY_DIR}"
        "${REPO}/boards/t_display_p4/include"
        "${REPO}/modules/core_sys/include"
        "${REPO}/platform/esp/idf_components/t_display_p4/include")
    if(MSVC)
        target_compile_options(p4_${TEST}_test PRIVATE /UNDEBUG)
    else()
        target_compile_options(p4_${TEST}_test PRIVATE -UNDEBUG)
    endif()
    add_test(NAME p4_${TEST} COMMAND p4_${TEST}_test)
endforeach()
target_sources(p4_audio_test PRIVATE "${REPO}/modules/core_sys/src/platform/audio/audio_focus.cpp")
target_sources(p4_audio_test PRIVATE "${REPO}/platform/linux/common/src/platform/resource_lock.cpp")
add_executable(p4_keyboard_availability_test test_keyboard_availability.cpp)
target_include_directories(p4_keyboard_availability_test PRIVATE keyboard_fakes
    "${REPO}/platform/esp/idf_components/t_display_p4/include")
target_compile_definitions(p4_keyboard_availability_test PRIVATE TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4=1)
if(MSVC)
    target_compile_options(p4_keyboard_availability_test PRIVATE /UNDEBUG)
else()
    target_compile_options(p4_keyboard_availability_test PRIVATE -UNDEBUG)
endif()
add_test(NAME p4_keyboard_availability COMMAND p4_keyboard_availability_test)
if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(p4_power_test PRIVATE Threads::Threads)
endif()
