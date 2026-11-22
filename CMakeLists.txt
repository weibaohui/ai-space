cmake_minimum_required(VERSION 3.15)

# Cross-platform build used by GitHub Actions (Linux/macOS/Windows, x64 & arm64).
# Local macOS development can keep using the Makefile; this file mirrors it.
project(ai_space
    VERSION 0.1.0
    DESCRIPTION "Local-First Agent Skills Manager & Multi-Host Sync Hub"
    LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# CUI options: library only, no examples/tests (set before add_subdirectory so
# CUI's option() calls pick these up).
set(CUI_BUILD_EXAMPLES OFF)
set(CUI_BUILD_TESTS OFF)

add_subdirectory(CUI)

# System sqlite3 everywhere (apt/brew/MSYS2 packages it); see CI workflow
find_package(SQLite3 REQUIRED)

set(AI_SPACE_SRCS
    src/main.c
    src/view_dashboard.c
    src/view_common.c
    src/view_tools.c
    src/view_tool_skills.c
    src/view_hub_matrix.c
    src/view_hub_diff.c
    src/view_master_hub.c
    src/view_skill_tree.c
    src/view_settings.c
    src/view_sync.c
    src/sync_manager.c
    src/sync_driver_nfs.c
    src/sync_driver_webdav.c
    src/sync_driver_git.c
    src/db.c
    src/scanner.c
    src/skill_scanner.c
    src/hub_manager.c)

add_executable(ai_space ${AI_SPACE_SRCS})

target_include_directories(ai_space PRIVATE include CUI/include CUI/src)

target_link_libraries(ai_space PRIVATE cui::cui SQLite::SQLite3)

# Window app niceties: no console window on Windows, gc-sections/dead_strip in release
cui_app_target(ai_space)

if(MSVC)
    target_compile_options(ai_space PRIVATE /W3 /utf-8)
else()
    target_compile_options(ai_space PRIVATE -Wall -Wextra)
endif()
