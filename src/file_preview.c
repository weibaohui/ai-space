/* 预览模块负责按字节上限读取，并把文本与二进制信息交给展示层。 */
#include "file_preview.h"
#include "portability.h"

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

char *read_file_for_edit(const char *file_path, size_t max_bytes, size_t headroom, size_t min_cap,
                         size_t *out_cap, size_t *out_size, int *out_lines,
                         bool *out_is_binary, bool *out_editable) {
    if (out_cap) *out_cap = 0;
    if (out_editable) *out_editable = false;

    size_t read_len = 0, disk_size = 0;
    int lines = 0;
    bool is_bin = false;
    char *preview = read_file_preview(file_path, max_bytes, &read_len, &disk_size, &lines, &is_bin);
    if (out_size) *out_size = disk_size;
    if (out_lines) *out_lines = lines;
    if (out_is_binary) *out_is_binary = is_bin;
    if (!preview) return NULL;

    bool editable = !is_bin && disk_size <= max_bytes;
    if (out_editable) *out_editable = editable;
    if (!editable) {
        if (out_cap) *out_cap = read_len + 1;
        return preview;
    }

    size_t cap = read_len + headroom + 1;
    if (cap < min_cap) cap = min_cap;
    if (cap > max_bytes + 1) cap = max_bytes + 1;
    if (cap <= read_len) cap = read_len + 1;

    char *buf = (char *)realloc(preview, cap);
    if (!buf) {
        if (out_cap) *out_cap = read_len + 1;
        return preview;
    }
    if (out_cap) *out_cap = cap;
    return buf;
}

int write_file_atomic(const char *file_path, const char *content, size_t len) {
    if (!file_path || !file_path[0] || (!content && len > 0)) return -1;

    char tmp_path[1100];
    int n = snprintf(tmp_path, sizeof(tmp_path), "%s.tmp.%ld", file_path, (long)getpid());
    if (n < 0 || (size_t)n >= sizeof(tmp_path)) return -1;

    FILE *f = fopen(tmp_path, "wb");
    if (!f) return -1;

    bool ok = (len == 0 || fwrite(content, 1, len, f) == len);
    if (fflush(f) != 0) ok = false;
    if (fclose(f) != 0) ok = false;

#ifdef _WIN32
    if (!ok || !MoveFileExA(tmp_path, file_path, MOVEFILE_REPLACE_EXISTING)) {
#else
    if (!ok || rename(tmp_path, file_path) != 0) {
#endif
        remove(tmp_path);
        return -1;
    }
    return 0;
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
