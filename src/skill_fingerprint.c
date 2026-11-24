#include "skill_fingerprint.h"
#include "skill_metadata.h"
#include "file_tree.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include "portability.h"
#include <time.h>

#define FNV_OFFSET 14695981039346656037ULL
#define FNV_PRIME  1099511628211ULL

static uint64_t fnv1a_update(uint64_t hash, const void *data, size_t len) {
    const unsigned char *p = (const unsigned char *)data;
    for (size_t i = 0; i < len; i++) {
        hash ^= p[i];
        hash *= FNV_PRIME;
    }
    return hash;
}

typedef struct {
    char rel_path[512];
    char abs_path[1024];
} file_hash_item_t;

static int compare_file_items(const void *a, const void *b) {
    const file_hash_item_t *ia = (const file_hash_item_t *)a;
    const file_hash_item_t *ib = (const file_hash_item_t *)b;
    return strcmp(ia->rel_path, ib->rel_path);
}

static int collect_files_recursive(const char *dir_path, const char *rel_dir,
                                   file_hash_item_t **items, size_t *count, size_t *cap,
                                   time_t *latest_mtime, int depth) {
    if (depth >= SYNC_TREE_DEPTH) return -1;
    DIR *dir = opendir(dir_path);
    if (!dir) return -1;

    int rc = 0;
    while (rc == 0) {
        errno = 0;
        struct dirent *ent = readdir(dir);
        if (!ent) {
            if (errno) rc = -1;
            break;
        }
        if (skill_skip_name(ent->d_name)) continue;

        file_hash_item_t child;
        int absolute_len = snprintf(child.abs_path, sizeof(child.abs_path), "%s/%s", dir_path, ent->d_name);
        int relative_len = snprintf(child.rel_path, sizeof(child.rel_path), "%s%s%s",
                                    rel_dir, rel_dir[0] ? "/" : "", ent->d_name);
        struct stat st;
        if (absolute_len < 0 || (size_t)absolute_len >= sizeof(child.abs_path) ||
            relative_len < 0 || (size_t)relative_len >= sizeof(child.rel_path) ||
            lstat(child.abs_path, &st) != 0) {
            rc = -1;
            break;
        }

        if (S_ISDIR(st.st_mode)) {
            if (st.st_mtime > *latest_mtime) *latest_mtime = st.st_mtime;
            rc = collect_files_recursive(child.abs_path, child.rel_path, items, count, cap, latest_mtime, depth + 1);
        } else if (S_ISREG(st.st_mode)) {
            if (*count == *cap) {
                size_t capacity = *cap ? *cap * 2 : 32;
                file_hash_item_t *grown = realloc(*items, capacity * sizeof(**items));
                if (!grown) {
                    rc = -1;
                    break;
                }
                *items = grown;
                *cap = capacity;
            }
            (*items)[(*count)++] = child;
        }
    }
    closedir(dir);
    return rc;
}

int hub_compute_skill_hash(const char *dir_path, char *out_hash_hex, size_t max_len, time_t *out_mtime) {
    if (!dir_path || !out_hash_hex || max_len == 0) return -1;
    out_hash_hex[0] = '\0';

    struct stat root_st;
    if (stat(dir_path, &root_st) != 0 || !S_ISDIR(root_st.st_mode)) return -1;

    size_t capacity = 0, count = 0;
    file_hash_item_t *items = NULL;
    time_t latest_mtime = root_st.st_mtime;
    int rc = collect_files_recursive(dir_path, "", &items, &count, &capacity, &latest_mtime, 0);
    if (rc == 0 && count > 1) qsort(items, count, sizeof(*items), compare_file_items);

    uint64_t hash = FNV_OFFSET;
    for (size_t i = 0; rc == 0 && i < count; i++) {
        file_hash_item_t *item = &items[i];
        struct stat st;
        if (lstat(item->abs_path, &st) != 0 || !S_ISREG(st.st_mode)) {
            rc = -1;
            break;
        }
        FILE *file = fopen(item->abs_path, "rb");
        if (!file) {
            rc = -1;
            break;
        }

        hash = fnv1a_update(hash, item->rel_path, strlen(item->rel_path));
        hash = fnv1a_update(hash, &st.st_size, sizeof(st.st_size));
        if (st.st_mtime > latest_mtime) latest_mtime = st.st_mtime;

        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), file)) > 0) hash = fnv1a_update(hash, buf, n);
        if (ferror(file)) rc = -1;
        fclose(file);
    }
    free(items);
    if (rc != 0) return rc;

    snprintf(out_hash_hex, max_len, "%016llx", (unsigned long long)hash);
    if (out_mtime) *out_mtime = latest_mtime;
    return 0;
}
