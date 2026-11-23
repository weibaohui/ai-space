CC ?= cc
CFLAGS = -std=c99 -O2 -Wall -Wextra -Iinclude -ICUI/include -ICUI/src
LDFLAGS = CUI/build/libcui.a -lsqlite3 -framework Cocoa -framework QuartzCore -framework OpenGL

SRCS = src/main.c src/view_dashboard.c src/view_common.c src/view_tools.c src/view_tool_skills.c \
       src/view_hub_matrix.c src/view_hub_diff.c src/view_master_hub.c src/view_skill_tree.c \
       src/view_settings.c src/view_sync.c src/view_storage.c src/view_dependencies.c src/sync_manager.c src/sync_directory.c src/sync_driver_nfs.c \
       src/sync_driver_webdav.c src/sync_driver_git.c \
       src/db.c src/scanner.c src/skill_scanner.c src/hub_manager.c
OBJS = $(SRCS:.c=.o)
HEADERS = $(wildcard include/*.h)
TARGET = ai_space
TEST_TARGET = test_sync
SYNC_TEST_OBJS = src/sync_manager.o src/sync_directory.o src/sync_driver_nfs.o src/sync_driver_webdav.o src/sync_driver_git.o src/hub_manager.o

TEST_OBJS = tests/test_sync_git.o tests/test_sync_backends.o
BACKEND_TEST_TARGET = test_sync_backends

.PHONY: all clean run shot test

all: $(TARGET) $(TEST_TARGET)

$(TARGET): $(OBJS) CUI/build/libcui.a
	$(CC) $(OBJS) $(LDFLAGS) -o $@

$(TEST_TARGET): tests/test_sync_git.o $(SYNC_TEST_OBJS)
	$(CC) $^ -lsqlite3 -o $@

$(BACKEND_TEST_TARGET): tests/test_sync_backends.o $(SYNC_TEST_OBJS)
	$(CC) $^ -lsqlite3 -o $@

tests/%.o: tests/%.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TEST_OBJS) $(TARGET) $(TEST_TARGET) $(BACKEND_TEST_TARGET) shot*.png

run: $(TARGET)
	./$(TARGET)

shot: $(TARGET)
	CUI_SCREENSHOT=shot.png CUI_SCREENSHOT_FRAMES=10 ./$(TARGET)
