/* 预览模块负责按字节上限读取，并把文本与二进制信息交给展示层。 */
#include "file_preview.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *read_file_preview(const char *file_path, size_t max_bytes, size_t *out_read,
                        size_t *out_size, int *out_lines, bool *out_is_binary) {
    if (!file_path) return NULL;
    if (out_read) *out_read = 0;
    if (out_size) *out_size = 0;
    if (out_lines) *out_lines = 0;
    if (out_is_binary) *out_is_binary = false;

    FILE *f = fopen(file_path, "rb");
    if (!f) return NULL;

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long sz = ftell(f);
    if (sz < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }

    size_t to_read = (size_t)sz;
    if (to_read > max_bytes) {
        to_read = max_bytes;
    }

    char *buf = (char *)malloc(to_read + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }

    size_t n = fread(buf, 1, to_read, f);
    if (ferror(f)) {
        free(buf);
        fclose(f);
        return NULL;
    }
    buf[n] = '\0';
    fclose(f);

    /* 只在开头 1024 字节检查 NUL，限制二进制探测的开销。 */
    bool is_bin = false;
    size_t check_len = n < 1024 ? n : 1024;
    for (size_t i = 0; i < check_len; i++) {
        if (buf[i] == '\0') {
            is_bin = true;
            break;
        }
    }

    int lines = 1;
    for (size_t i = 0; i < n; i++) {
        if (buf[i] == '\n') lines++;
    }

    if (out_read) *out_read = n;
    if (out_size) *out_size = (size_t)sz;
    if (out_lines) *out_lines = lines;
    if (out_is_binary) *out_is_binary = is_bin;

    return buf;
}

const char *detect_syntax_lang(const char *filename) {
    if (!filename) return "text";
    const char *dot = strrchr(filename, '.');
    if (!dot) return "text";

    if (strcmp(dot, ".c") == 0 || strcmp(dot, ".h") == 0) return "c";
    if (strcmp(dot, ".cpp") == 0 || strcmp(dot, ".hpp") == 0 || strcmp(dot, ".cc") == 0) return "cpp";
    if (strcmp(dot, ".js") == 0 || strcmp(dot, ".jsx") == 0 || strcmp(dot, ".mjs") == 0) return "js";
    if (strcmp(dot, ".ts") == 0 || strcmp(dot, ".tsx") == 0) return "ts";
    if (strcmp(dot, ".py") == 0) return "python";
    if (strcmp(dot, ".sh") == 0 || strcmp(dot, ".bash") == 0 || strcmp(dot, ".zsh") == 0) return "shell";
    if (strcmp(dot, ".json") == 0) return "json";
    if (strcmp(dot, ".go") == 0) return "go";
    if (strcmp(dot, ".rs") == 0) return "rust";
    if (strcmp(dot, ".sql") == 0) return "sql";
    if (strcmp(dot, ".md") == 0 || strcmp(dot, ".markdown") == 0) return "markdown";
    if (strcmp(dot, ".yaml") == 0 || strcmp(dot, ".yml") == 0) return "shell";
    return "text";
}
