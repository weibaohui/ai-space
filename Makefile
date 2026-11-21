CC ?= cc
CFLAGS = -std=c99 -O2 -Wall -Wextra -Iinclude -ICUI/include
LDFLAGS = CUI/build/libcui.a -lsqlite3 -framework Cocoa -framework QuartzCore -framework OpenGL

SRCS = src/main.c src/db.c src/scanner.c src/skill_scanner.c
OBJS = $(SRCS:.c=.o)
TARGET = ai_space

.PHONY: all clean run shot

all: $(TARGET)

$(TARGET): $(OBJS) CUI/build/libcui.a
	$(CC) $(OBJS) $(LDFLAGS) -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET) shot*.png

run: $(TARGET)
	./$(TARGET)

shot: $(TARGET)
	CUI_SCREENSHOT=shot.png CUI_SCREENSHOT_FRAMES=10 ./$(TARGET)
