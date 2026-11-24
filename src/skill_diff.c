#include "file_preview.h"
#include "skill_metadata.h"
#include "skill_diff.h"
#include "file_tree.h"
#include <errno.h>
#include <cui/cui.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "portability.h"

typedef struct {
    char rel_path[256];
} rel_file_t;

static int collect_rel_files(const char *root, const char *sub, rel_file_t **items,
                             int *count, int *capacity, int depth) {
    char dir_path[SYNC_PATH_CAP];
    int n = sub[0] ? snprintf(dir_path, sizeof(dir_path), "%s/%s", root, sub)
                   : snprintf(dir_path, sizeof(dir_path), "%s", root);
    if (depth >= SYNC_TREE_DEPTH || n < 0 || (size_t)n >= sizeof(dir_path)) return -1;

    DIR *dir = opendir(dir_path);
    if (!dir) return -1;
    int rc = 0;
    while (rc == 0) {
        errno = 0;
        struct dirent *entry = readdir(dir);
        if (!entry) {
            if (errno) rc = -1;
            break;
        }
        if (skill_skip_name(entry->d_name)) continue;

        rel_file_t candidate;
        char path[SYNC_PATH_CAP];
        n = snprintf(candidate.rel_path, sizeof(candidate.rel_path), "%s%s%s",
                     sub, sub[0] ? "/" : "", entry->d_name);
        struct stat st;
        if (n < 0 || (size_t)n >= sizeof(candidate.rel_path) ||
            !sync_valid_component(entry->d_name) ||
            sync_join(path, sizeof(path), root, candidate.rel_path) != 0 ||
            lstat(path, &st) != 0) {
            rc = -1;
        } else if (S_ISDIR(st.st_mode)) {
            rc = collect_rel_files(root, candidate.rel_path, items, count, capacity, depth + 1);
        } else if (S_ISREG(st.st_mode)) {
            if (*count == *capacity) {
                int next_capacity = *capacity ? *capacity * 2 : 64;
                void *grown = realloc(*items, (size_t)next_capacity * sizeof(**items));
                if (!grown) {
                    rc = -1;
                    break;
                }
                *items = grown;
                *capacity = next_capacity;
            }
            (*items)[(*count)++] = candidate;
        }
    }
    closedir(dir);
    return rc;
}

static int compare_relative_paths(const void *a, const void *b) {
    return strcmp(((const rel_file_t *)a)->rel_path, ((const rel_file_t *)b)->rel_path);
}

static int compare_manifest_items(const void *a, const void *b) {
    const hub_diff_file_item_t *ia = (const hub_diff_file_item_t *)a;
    const hub_diff_file_item_t *ib = (const hub_diff_file_item_t *)b;

    /* SKILL.md always first */
    bool a_is_skill = strcmp(ia->rel_path, "SKILL.md") == 0;
    bool b_is_skill = strcmp(ib->rel_path, "SKILL.md") == 0;
    if (a_is_skill != b_is_skill) return a_is_skill ? -1 : 1;

    /* Changed before in-sync */
    bool ia_changed = (ia->status != DIFF_FILE_IN_SYNC);
    bool ib_changed = (ib->status != DIFF_FILE_IN_SYNC);
    if (ia_changed != ib_changed) {
        return ia_changed ? -1 : 1;
    }

    return strcmp(ia->rel_path, ib->rel_path);
}

void free_skill_diff_manifest(hub_skill_diff_manifest_t *m) {
    if (!m) return;
    if (m->items) {
        free(m->items);
        m->items = NULL;
    }
    m->count = 0;
    m->modified_count = 0;
    m->added_count = 0;
    m->deleted_count = 0;
    m->in_sync_count = 0;
    m->total_added_lines = 0;
    m->total_removed_lines = 0;
}

int skill_diff_compute(const char *master, const char *tool_path, hub_skill_diff_manifest_t *manifest) {
    int rel_cap = 0;
    int rel_count = 0;
    rel_file_t *rel_files = NULL;
    if ((master && collect_rel_files(master, "", &rel_files, &rel_count, &rel_cap, 0) != 0) ||
        (tool_path && collect_rel_files(tool_path, "", &rel_files, &rel_count, &rel_cap, 0) != 0)) {
        free(rel_files);
        return -1;
    }

    if (rel_count > 1) qsort(rel_files, (size_t)rel_count, sizeof(*rel_files), compare_relative_paths);
    int unique = 0;
    for (int i = 0; i < rel_count; i++) {
        if (unique == 0 || strcmp(rel_files[i].rel_path, rel_files[unique - 1].rel_path) != 0)
            rel_files[unique++] = rel_files[i];
    }
    rel_count = unique;

    manifest->items = rel_count ? calloc((size_t)rel_count, sizeof(*manifest->items)) : NULL;
    if (rel_count && !manifest->items) {
        free(rel_files);
        return -1;
    }
    manifest->count = rel_count;

    for (int i = 0; i < rel_count; i++) {
        hub_diff_file_item_t *item = &manifest->items[i];
        strncpy(item->rel_path, rel_files[i].rel_path, sizeof(item->rel_path) - 1);

        const char *fname = strrchr(item->rel_path, '/');
        strncpy(item->file_name, fname ? (fname + 1) : item->rel_path, sizeof(item->file_name) - 1);

        char m_full[1200];
        char t_full[1200];
        snprintf(m_full, sizeof(m_full), "%s/%s", master ? master : "", item->rel_path);
        snprintf(t_full, sizeof(t_full), "%s/%s", tool_path ? tool_path : "", item->rel_path);

        struct stat m_st, t_st;
        bool has_m = (master && lstat(m_full, &m_st) == 0 && S_ISREG(m_st.st_mode));
        bool has_t = (tool_path && lstat(t_full, &t_st) == 0 && S_ISREG(t_st.st_mode));

        if (has_m) item->master_size = m_st.st_size;
        if (has_t) item->tool_size = t_st.st_size;

        if (has_m && !has_t) {
            item->status = DIFF_FILE_DELETED;
            manifest->deleted_count++;
            int m_ln = 0;
            bool m_bin = false;
            char *m_text = read_file_preview(m_full, 1024 * 1024, NULL, NULL, &m_ln, &m_bin);
            item->is_binary = m_bin;
            item->removed_lines = (m_ln > 0) ? m_ln : 1;
            item->added_lines = 0;
            manifest->total_removed_lines += item->removed_lines;
            if (m_text) free(m_text);
        } else if (!has_m && has_t) {
            item->status = DIFF_FILE_ADDED;
            manifest->added_count++;
            int t_ln = 0;
            bool t_bin = false;
            char *t_text = read_file_preview(t_full, 1024 * 1024, NULL, NULL, &t_ln, &t_bin);
            item->is_binary = t_bin;
            item->added_lines = (t_ln > 0) ? t_ln : 1;
            item->removed_lines = 0;
            manifest->total_added_lines += item->added_lines;
            if (t_text) free(t_text);
        } else if (has_m && has_t) {
            /* Check if identical bytes */
            bool same = false;
            if (m_st.st_size == t_st.st_size) {
                FILE *fm = fopen(m_full, "rb");
                FILE *ft = fopen(t_full, "rb");
                same = true;
                if (fm && ft) {
                    char b1[4096], b2[4096];
                    size_t n1, n2;
                    while ((n1 = fread(b1, 1, sizeof(b1), fm)) > 0) {
                        n2 = fread(b2, 1, sizeof(b2), ft);
                        if (n1 != n2 || memcmp(b1, b2, n1) != 0) {
                            same = false;
                            break;
                        }
                    }
                } else {
                    same = false;
                }
                if (fm && ferror(fm)) same = false;
                if (ft && ferror(ft)) same = false;
                if (fm) fclose(fm);
                if (ft) fclose(ft);
            }

            if (same) {
                item->status = DIFF_FILE_IN_SYNC;
                item->added_lines = 0;
                item->removed_lines = 0;
                manifest->in_sync_count++;
            } else {
                item->status = DIFF_FILE_MODIFIED;
                manifest->modified_count++;

                size_t m_sz = 0, t_sz = 0;
                int m_ln = 0, t_ln = 0;
                bool m_bin = false, t_bin = false;
                char *m_text = read_file_preview(m_full, 1024 * 1024, &m_sz, NULL, &m_ln, &m_bin);
                char *t_text = read_file_preview(t_full, 1024 * 1024, &t_sz, NULL, &t_ln, &t_bin);

                if (m_bin || t_bin) {
                    item->is_binary = true;
                    item->added_lines = 0;
                    item->removed_lines = 0;
                } else if (m_text && t_text) {
                    cui_diff_stats stats;
                    memset(&stats, 0, sizeof(stats));
                    cui_diff_compute(m_text, m_sz, t_text, t_sz, 0, NULL, 0, &stats);
                    item->added_lines = stats.added;
                    item->removed_lines = stats.removed;
                    manifest->total_added_lines += stats.added;
                    manifest->total_removed_lines += stats.removed;
                } else {
                    item->added_lines = t_ln;
                    item->removed_lines = m_ln;
                    manifest->total_added_lines += t_ln;
                    manifest->total_removed_lines += m_ln;
                }

                if (m_text) free(m_text);
                if (t_text) free(t_text);
            }
        }
    }
    free(rel_files);

    /* Sort manifest: SKILL.md first, then modified, then added/deleted, then in_sync */
    if (manifest->count > 1) {
        qsort(manifest->items, manifest->count, sizeof(hub_diff_file_item_t), compare_manifest_items);
    }

    return 0;
}
