cmake_minimum_required(VERSION 3.16)

# Cross-platform build used by GitHub Actions (Linux/macOS/Windows, x64 & arm64).
# Local macOS development can keep using the Makefile; this file mirrors it.
project(skillboard
    VERSION 1.12.0
    DESCRIPTION "Local-First Agent Skills Manager & Multi-Host Sync Hub"
    LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# CUI options: library only, no examples/tests (set before add_subdirectory so
# CUI's option() calls pick these up).
set(CUI_BUILD_EXAMPLES OFF)
set(CUI_BUILD_TESTS OFF)

add_subdirectory(CUI)
add_subdirectory(vendor)

# SQLite: if a CI-vendored amalgamation exists (third_party/sqlite/), build it
# statically -- the binary then carries no libsqlite3.so dependency, which keeps
# the glibc baseline of the zig-built Linux binaries intact. Local dev without
# that directory falls back to the system library.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite/sqlite3.c)
    add_library(sqlite3_impl STATIC third_party/sqlite/sqlite3.c)
    target_include_directories(sqlite3_impl PUBLIC third_party/sqlite)
    set(AI_SPACE_SQLITE_LIB sqlite3_impl)
else()
    find_package(SQLite3 REQUIRED)
    set(AI_SPACE_SQLITE_LIB SQLite::SQLite3)
endif()

set(AI_SPACE_SRCS
    src/main.c
    src/storage_manager.c
    src/app_storage.c
    src/skill_metadata.c
    src/skill_tokens.c
    src/skill_file_tree.c
    src/file_preview.c
    src/sync_webdav_listing.c
    src/sync_git_repository.c
    src/sync_git_process.c
    src/sync_git_merge.c
    src/app_task.c
    src/app_settings.c
    src/app_sync.c
    src/sync_baseline.c
    src/app_hub_tasks.c
    src/app_diff.c
    src/skill_diff.c
    src/view_about.c
    src/view_progress.c
    src/view_distribute.c
    src/view_uninstall.c
    src/view_sync_settings.c
    src/view_dashboard.c
    src/view_common.c
    src/view_tools.c
    src/view_tool_skills.c
    src/view_hub_matrix.c
    src/view_hub_diff.c
    src/view_hub_diff_preview.c
    src/view_master_hub.c
    src/view_skill_tree.c
    src/view_settings.c
    src/view_sync.c
    src/view_storage.c
    src/view_dependencies.c
    src/sync_manager.c
    src/sync_directory.c
    src/sync_local_files.c
    src/sync_driver_share.c
    src/sync_network_share.c
    src/sync_driver_webdav.c
    src/sync_driver_git.c
    src/db.c
    src/scanner.c
    src/skill_scanner.c
    src/file_tree.c
    src/hub_files.c
    src/hub_paths.c
    src/hub_bulk.c
    src/skill_fingerprint.c
    src/hub_manager.c)

add_executable(skillboard ${AI_SPACE_SRCS})

# Window/taskbar icon on Windows: CUI's win32 backend loads resource ID 1
if(WIN32)
    target_sources(skillboard PRIVATE assets/app.rc)
endif()

target_include_directories(skillboard PRIVATE include CUI/include CUI/src)

target_link_libraries(skillboard PRIVATE cui::cui ${AI_SPACE_SQLITE_LIB} nfs smb2)

if(APPLE)
    set_target_properties(skillboard PROPERTIES BUILD_WITH_INSTALL_RPATH ON INSTALL_RPATH "@executable_path")
elseif(UNIX)
    set_target_properties(skillboard PROPERTIES BUILD_WITH_INSTALL_RPATH ON INSTALL_RPATH "$ORIGIN")
endif()

# Window app niceties: no console window on Windows, gc-sections/dead_strip in release
cui_app_target(skillboard)

if(MSVC)
    target_compile_options(skillboard PRIVATE /W3 /utf-8)
else()
    target_compile_options(skillboard PRIVATE -Wall -Wextra)
endif()

install(TARGETS skillboard nfs smb2
    RUNTIME DESTINATION . COMPONENT skillboard_runtime
    LIBRARY DESTINATION . COMPONENT skillboard_runtime)
install(FILES vendor/README.md DESTINATION licenses COMPONENT skillboard_licenses)
install(FILES vendor/libnfs/COPYING vendor/libnfs/LICENCE-LGPL-2.1.txt
              vendor/libnfs/LICENCE-BSD.txt
        DESTINATION licenses/libnfs COMPONENT skillboard_licenses)
install(FILES vendor/libsmb2/COPYING vendor/libsmb2/LICENCE-LGPL-2.1.txt
        DESTINATION licenses/libsmb2 COMPONENT skillboard_licenses)
