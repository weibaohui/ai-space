CC ?= cc
.DEFAULT_GOAL := all
CFLAGS = -std=c99 -O2 -Wall -Wextra -Iinclude -ICUI/include -ICUI/src
SHARE_LDFLAGS = -Lbuild -lnfs -lsmb2 -Wl,-rpath,@executable_path/build
LDFLAGS = CUI/build/libcui.a -lsqlite3 -framework Cocoa -framework QuartzCore -framework OpenGL

SRCS = src/main.c src/view_dashboard.c src/view_common.c src/view_tools.c src/view_tool_skills.c \
       src/view_hub_matrix.c src/view_hub_diff.c src/view_hub_diff_preview.c src/view_master_hub.c src/view_skill_tree.c \
       src/view_settings.c src/view_sync.c src/view_storage.c src/view_dependencies.c src/sync_manager.c src/sync_directory.c src/sync_local_files.c src/sync_driver_share.c src/sync_network_share.c \
       src/sync_driver_webdav.c src/sync_driver_git.c \
       src/app_task.c src/app_settings.c src/app_sync.c src/sync_baseline.c src/app_hub_tasks.c src/app_diff.c src/skill_diff.c \
       src/view_about.c src/view_progress.c src/view_distribute.c src/view_uninstall.c src/view_file_editor.c src/view_sync_settings.c \
       src/storage_manager.c src/app_storage.c src/skill_metadata.c src/skill_tokens.c src/skill_file_tree.c src/file_preview.c src/sync_webdav_listing.c src/sync_git_repository.c src/sync_git_process.c src/sync_git_merge.c \
       src/db.c src/scanner.c src/skill_scanner.c src/file_tree.c src/hub_files.c src/hub_paths.c src/hub_bulk.c src/skill_fingerprint.c src/hub_manager.c
OBJS = $(SRCS:.c=.o)
HEADERS = $(wildcard include/*.h src/*.h)
TARGET = skillboard
TEST_TARGET = test_sync
SYNC_TEST_OBJS = src/skill_metadata.o src/skill_tokens.o src/sync_webdav_listing.o src/sync_git_repository.o src/sync_git_process.o src/sync_git_merge.o src/sync_manager.o src/sync_directory.o src/sync_local_files.o src/sync_driver_share.o src/sync_network_share.o src/sync_driver_webdav.o src/sync_driver_git.o src/file_tree.o src/hub_files.o src/hub_paths.o src/hub_bulk.o src/skill_fingerprint.o src/hub_manager.o

TEST_OBJS = tests/test_sync_git.o tests/test_sync_backends.o tests/test_storage_preview.o tests/test_git_merge.o tests/test_tool_clear.o tests/test_skill_tokens.o tests/test_file_edit.o
BACKEND_TEST_TARGET = test_sync_backends
STORAGE_TEST_TARGET = test_storage_preview
GIT_MERGE_TEST_TARGET = test_git_merge
TOOL_CLEAR_TEST_TARGET = test_tool_clear
VENDOR_STAMP = build/.sync-client-libraries
VENDOR_SOURCES = $(shell find vendor -type f)

$(VENDOR_STAMP): CMakeLists.txt $(VENDOR_SOURCES)
	cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
	+cmake --build build --target nfs smb2 --parallel
	touch $@

src/sync_network_share.o: vendor/libnfs/include/nfsc/libnfs.h vendor/libsmb2/include/smb2/libsmb2.h
src/sync_network_share.o: CFLAGS += -Ivendor/libnfs/include -Ivendor/libsmb2/include

.PHONY: all clean run shot test

all: $(TARGET) $(TEST_TARGET)

$(TARGET): $(OBJS) CUI/build/libcui.a $(VENDOR_STAMP)
	$(CC) $(OBJS) $(LDFLAGS) $(SHARE_LDFLAGS) -o $@

$(TEST_TARGET): tests/test_sync_git.o $(SYNC_TEST_OBJS) $(VENDOR_STAMP)
	$(CC) $(filter %.o,$^) -lsqlite3 $(SHARE_LDFLAGS) -o $@

$(BACKEND_TEST_TARGET): tests/test_sync_backends.o $(SYNC_TEST_OBJS) src/sync_baseline.o src/db.o src/scanner.o $(VENDOR_STAMP)
	$(CC) $(filter %.o,$^) -lsqlite3 $(SHARE_LDFLAGS) -o $@

$(GIT_MERGE_TEST_TARGET): tests/test_git_merge.o $(SYNC_TEST_OBJS) src/sync_baseline.o src/db.o src/scanner.o $(VENDOR_STAMP)
	$(CC) $(filter %.o,$^) -lsqlite3 $(SHARE_LDFLAGS) -o $@

$(STORAGE_TEST_TARGET): tests/test_storage_preview.o src/app_storage.o src/storage_manager.o src/file_tree.o src/file_preview.o src/db.o src/scanner.o
	$(CC) $^ -lsqlite3 -o $@

$(TOOL_CLEAR_TEST_TARGET): tests/test_tool_clear.o src/hub_files.o src/hub_paths.o src/skill_metadata.o src/file_tree.o src/scanner.o
	$(CC) $^ -o $@

SKILL_TOKENS_TEST_TARGET = test_skill_tokens
$(SKILL_TOKENS_TEST_TARGET): tests/test_skill_tokens.o src/skill_tokens.o src/skill_metadata.o src/file_tree.o
	$(CC) $^ -o $@

FILE_EDIT_TEST_TARGET = test_file_edit
$(FILE_EDIT_TEST_TARGET): tests/test_file_edit.o src/file_preview.o src/file_tree.o
	$(CC) $^ -o $@

tests/%.o: tests/%.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(VENDOR_STAMP) $(OBJS) $(TEST_OBJS) $(TARGET) $(TEST_TARGET) $(BACKEND_TEST_TARGET) $(STORAGE_TEST_TARGET) $(GIT_MERGE_TEST_TARGET) $(TOOL_CLEAR_TEST_TARGET) shot*.png

run: $(TARGET)
	./$(TARGET)

test: $(STORAGE_TEST_TARGET) $(TOOL_CLEAR_TEST_TARGET) $(SKILL_TOKENS_TEST_TARGET) $(FILE_EDIT_TEST_TARGET) $(GIT_MERGE_TEST_TARGET)
	./$(STORAGE_TEST_TARGET)
	./$(TOOL_CLEAR_TEST_TARGET)
	./$(SKILL_TOKENS_TEST_TARGET)
	./$(FILE_EDIT_TEST_TARGET)
	python3 tests/run_git_merge_test.py ./$(GIT_MERGE_TEST_TARGET)

shot: $(TARGET)
	CUI_SCREENSHOT=shot.png CUI_SCREENSHOT_FRAMES=10 ./$(TARGET)
