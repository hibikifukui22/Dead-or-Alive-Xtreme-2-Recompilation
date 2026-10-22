# doax2 - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(doax2 LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
set(DOAX2_SOURCES
    src/main.cpp
    src/doax2_60fps.cpp
)

if(WIN32)
    add_executable(doax2 WIN32 ${DOAX2_SOURCES})
else()
    add_executable(doax2 ${DOAX2_SOURCES})
endif()

rexglue_setup_target(doax2 GPU_PLUGINS xenos)

# Diagnostic guest call census; configure a separate build directory with it.
option(DOAX2_FUNC_CENSUS "Count every recompiled guest function call" OFF)
if(DOAX2_FUNC_CENSUS)
    target_compile_definitions(doax2 PRIVATE DOAX2_FUNC_CENSUS)
    if(TARGET doax2_recomp)
        target_compile_definitions(doax2_recomp PRIVATE DOAX2_FUNC_CENSUS)
    endif()
endif()

# The end-user launcher is intentionally independent from the ReXGlue runtime.
# It writes stable graphics choices to config/launcher.ini and starts doax2 with
# the corresponding runtime CVars.
if(WIN32)
    add_executable(doax2_launcher WIN32 src/launcher.cpp)
    target_link_libraries(doax2_launcher PRIVATE comctl32)
    set_target_properties(doax2_launcher PROPERTIES
        OUTPUT_NAME "DOAX2 Launcher"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}")
    # Tester launcher for sharing: only "60 FPS stable v1" (with logging).
    add_executable(doax2_tester_launcher WIN32 src/launcher.cpp)
    target_compile_definitions(doax2_tester_launcher PRIVATE DOAX2_TESTER_LAUNCHER)
    target_link_libraries(doax2_tester_launcher PRIVATE comctl32)
    set_target_properties(doax2_tester_launcher PROPERTIES
        OUTPUT_NAME "DOAX2 60FPS Test Launcher"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tester")
endif()

# Developer utility: extracts the raw file from an Xbox 360 STFS save package.
# It is deliberately separate from the game executable and is not installed.
add_executable(stfs_extract tools/stfs_extract.cpp)
target_link_libraries(stfs_extract PRIVATE rex::runtime)
rexglue_apply_target_settings(stfs_extract)

# Pure host-side capture tests; no game data or runtime initialization needed.
add_executable(doax2_render_history_test EXCLUDE_FROM_ALL tests/render_history_test.cpp)
add_executable(doax2_render_operation_sequence_test EXCLUDE_FROM_ALL tests/render_operation_sequence_test.cpp)
add_test(NAME doax2_render_operation_sequence COMMAND doax2_render_operation_sequence_test)
add_executable(doax2_render_region_versions_test EXCLUDE_FROM_ALL tests/render_region_versions_test.cpp)
add_test(NAME doax2_render_region_versions COMMAND doax2_render_region_versions_test)
add_executable(doax2_render_owner_lineage_test EXCLUDE_FROM_ALL tests/render_owner_lineage_test.cpp)
add_test(NAME doax2_render_owner_lineage COMMAND doax2_render_owner_lineage_test)
add_executable(doax2_native60_phase_test EXCLUDE_FROM_ALL tests/native60_phase_test.cpp)
add_test(NAME doax2_native60_phase COMMAND doax2_native60_phase_test)
add_executable(doax2_title_playlist_test EXCLUDE_FROM_ALL tests/title_playlist_test.cpp)
target_link_libraries(doax2_title_playlist_test PRIVATE rex::runtime)
add_test(NAME doax2_title_playlist COMMAND doax2_title_playlist_test)
set_tests_properties(doax2_title_playlist PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
if(WIN32)
    add_executable(doax2_frame_bank_gpu_test EXCLUDE_FROM_ALL tests/frame_bank_gpu_test.cpp)
    target_link_libraries(doax2_frame_bank_gpu_test PRIVATE d3d12 dxgi)
endif()

# Keep native crash addresses attributable to recompiled guest functions.
if(WIN32)
    target_link_options(doax2 PRIVATE "LINKER:/MAP" "LINKER:/DEBUG" "LINKER:/OPT:REF" "LINKER:/OPT:ICF")
endif()
