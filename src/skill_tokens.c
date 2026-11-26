#include "skill_tokens.h"
#include "skill_metadata.h"
#include "file_tree.h"
#include "portability.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define TOKEN_FILE_LIMIT (512 * 1024)
#define TOKEN_WALK_DEPTH 5

static int decode_utf8(const unsigned char *s, size_t len, unsigned int *out_cp) {
    unsigned char b0 = s[0];
    if (b0 < 0x80) {
        *out_cp = b0;
        return 1;
    }
    if ((b0 & 0xE0) == 0xC0 && len >= 2 && (s[1] & 0xC0) == 0x80) {
        *out_cp = ((unsigned int)(b0 & 0x1F) << 6) | (unsigned int)(s[1] & 0x3F);
        return 2;
    }
    if ((b0 & 0xF0) == 0xE0 && len >= 3 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
        *out_cp = ((unsigned int)(b0 & 0x0F) << 12) |
                  ((unsigned int)(s[1] & 0x3F) << 6) |
                  (unsigned int)(s[2] & 0x3F);
        return 3;
    }
    if ((b0 & 0xF8) == 0xF0 && len >= 4 &&
        (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80 && (s[3] & 0xC0) == 0x80) {
        *out_cp = ((unsigned int)(b0 & 0x07) << 18) |
                  ((unsigned int)(s[1] & 0x3F) << 12) |
                  ((unsigned int)(s[2] & 0x3F) << 6) |
                  (unsigned int)(s[3] & 0x3F);
        return 4;
    }
    *out_cp = b0;
    return 1;
}

static bool is_cjk_codepoint(unsigned int cp) {
    return (cp >= 0x2E80 && cp <= 0x9FFF) ||
           (cp >= 0xF900 && cp <= 0xFAFF) ||
           (cp >= 0xFE30 && cp <= 0xFE4F) ||
           (cp >= 0xFF00 && cp <= 0xFFEF) ||
           (cp >= 0xAC00 && cp <= 0xD7AF) ||
           (cp >= 0x20000 && cp <= 0x2FA1F);
}

int skill_estimate_tokens(const char *text, size_t len) {
    if (!text || len == 0) return 0;

    const unsigned char *p = (const unsigned char *)text;
    size_t i = 0;
    /* 使用 1/100 token 作为内部单位，避免浮点运算并保留混合文本累计精度。 */
    long units = 0;

    while (i < len) {
        unsigned char c = p[i];
        if (c < 0x80) {
            if (c == '\n') {
                units += 100;
                i++;
            } else if (isspace(c)) {
                size_t start = i;
                while (i < len && p[i] < 0x80 && isspace(p[i]) && p[i] != '\n') i++;
                size_t ws = i - start;
                /* 单个空格通常与后续词合并为同一个 BPE token，仅连续缩进空白计入额外开销。 */
                if (ws > 1) units += (long)(((ws - 1) + 3) / 4) * 100;
            } else if (isalnum(c) || c == '_') {
                size_t start = i;
                while (i < len && p[i] < 0x80 && (isalnum(p[i]) || p[i] == '_')) i++;
                size_t word_len = i - start;
                units += (long)((word_len + 3) / 4) * 100;
            } else {
                size_t start = i;
                while (i < len && p[i] < 0x80 && !isspace(p[i]) && !isalnum(p[i]) && p[i] != '_') i++;
                size_t sym_len = i - start;
                units += (long)((sym_len + 1) / 2) * 100;
            }
        } else {
            unsigned int cp = 0;
            int step = decode_utf8(p + i, len - i, &cp);
            i += (size_t)step;
            units += is_cjk_codepoint(cp) ? 160 : 180;
        }
    }

    if (units <= 0) return 0;
    return (int)((units + 99) / 100);
}

int skill_estimate_prompt_tokens(const char *name, const char *description, size_t *out_chars) {
    const char *n = name ? name : "";
    const char *d = description ? description : "";
    size_t nlen = strlen(n);
    size_t dlen = strlen(d);
    size_t total = nlen + 1 + dlen;
    if (out_chars) *out_chars = total;
    if (nlen == 0 && dlen == 0) {
        if (out_chars) *out_chars = 0;
        return 0;
    }

    char *buf = (char *)malloc(total + 1);
    if (!buf) return (int)((total + 3) / 4);
    memcpy(buf, n, nlen);
    buf[nlen] = '\n';
    memcpy(buf + nlen + 1, d, dlen);
    buf[total] = '\0';

    int tokens = skill_estimate_tokens(buf, total);
    free(buf);
    return tokens;
}

static char *read_text_file(const char *file_path, size_t *out_size) {
    if (out_size) *out_size = 0;
    if (!file_path || !file_path[0]) return NULL;

    struct stat st;
    if (stat(file_path, &st) != 0 || !S_ISREG(st.st_mode)) return NULL;
    if (st.st_size <= 0) return NULL;

    size_t to_read = (size_t)st.st_size;
    if (to_read > TOKEN_FILE_LIMIT) to_read = TOKEN_FILE_LIMIT;

    FILE *f = fopen(file_path, "rb");
    if (!f) return NULL;

    char *buf = (char *)malloc(to_read + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t n = fread(buf, 1, to_read, f);
    fclose(f);
    buf[n] = '\0';

    if (memchr(buf, '\0', n) != NULL) {
        free(buf);
        return NULL;
    }
    if (out_size) *out_size = (size_t)st.st_size;
    return buf;
}

int skill_estimate_file_tokens(const char *file_path, size_t *out_bytes) {
    size_t size = 0;
    char *content = read_text_file(file_path, &size);
    if (out_bytes) *out_bytes = size;
    if (!content) return 0;

    size_t len = size > TOKEN_FILE_LIMIT ? TOKEN_FILE_LIMIT : size;
    int tokens = skill_estimate_tokens(content, len);
    free(content);
    return tokens;
}

static int walk_directory_tokens(const char *dir_path, int depth) {
    if (depth > TOKEN_WALK_DEPTH) return 0;
    DIR *dir = opendir(dir_path);
    if (!dir) return 0;

    int total = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        if (skill_skip_name(ent->d_name) || strcmp(ent->d_name, "node_modules") == 0) continue;

        char child[SYNC_PATH_CAP];
        if (sync_join(child, sizeof(child), dir_path, ent->d_name) != 0) continue;

        struct stat st;
        if (lstat(child, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            total += walk_directory_tokens(child, depth + 1);
        } else if (S_ISREG(st.st_mode)) {
            total += skill_estimate_file_tokens(child, NULL);
        }
    }
    closedir(dir);
    return total;
}

int skill_measure_directory_tokens(const char *skill_dir, const char *skill_name,
                                   skill_token_usage_t *out) {
    if (!out) return -1;
    *out = (skill_token_usage_t){0};
    if (!skill_dir || !skill_dir[0]) return -1;

    char skill_md[SYNC_PATH_CAP];
    if (sync_join(skill_md, sizeof(skill_md), skill_dir, SKILL_DOCUMENT) == 0) {
        /* 统计注入开销时读取完整 description，避免被列表展示摘要截断。 */
        char full_desc[4096] = "";
        skill_read_description(skill_md, full_desc, sizeof(full_desc), true);
        out->prompt_tokens = skill_estimate_prompt_tokens(skill_name, full_desc, &out->prompt_chars);
        out->full_tokens = skill_estimate_file_tokens(skill_md, &out->full_bytes);
    } else {
        out->prompt_tokens = skill_estimate_prompt_tokens(skill_name, "", &out->prompt_chars);
    }

    out->total_tokens = walk_directory_tokens(skill_dir, 0);
    if (out->total_tokens < out->full_tokens) out->total_tokens = out->full_tokens;
    return 0;
}

void skill_format_tokens(int tokens, char *out, size_t cap) {
    if (!out || cap == 0) return;
    if (tokens >= 1000) {
        snprintf(out, cap, "%.1fk", (double)tokens / 1000.0);
    } else {
        snprintf(out, cap, "%d", tokens < 0 ? 0 : tokens);
    }
}
