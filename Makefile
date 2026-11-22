CC ?= cc
CFLAGS = -std=c99 -O2 -Wall -Wextra -Iinclude -ICUI/include -ICUI/src
LDFLAGS = CUI/build/libcui.a -lsqlite3 -framework Cocoa -framework QuartzCore -framework OpenGL

SRCS = src/main.c src/view_common.c src/view_tools.c src/view_tool_skills.c \
       src/view_hub_matrix.c src/view_hub_diff.c src/view_master_hub.c src/view_skill_tree.c \
       src/view_settings.c src/db.c src/scanner.c src/skill_scanner.c src/hub_manager.c
OBJS = $(SRCS:.c=.o)
HEADERS = $(wildcard include/*.h)
TARGET = ai_space

.PHONY: all clean run shot

all: $(TARGET)

$(TARGET): $(OBJS) CUI/build/libcui.a
	$(CC) $(OBJS) $(LDFLAGS) -o $@

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET) shot*.png

run: $(TARGET)
	./$(TARGET)

shot: $(TARGET)
	CUI_SCREENSHOT=shot.png CUI_SCREENSHOT_FRAMES=10 ./$(TARGET)
