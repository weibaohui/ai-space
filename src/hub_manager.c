#include "hub_manager.h"
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

static const char *get_user_home(void) {
    return ai_home_dir();
}

static char g_custom_hub_dir[512] = "";
static char g_custom_backup_dir[512] = "";

static void expand_path(const char *in, char *out, size_t cap) {
    if (!in || in[0] == '\0') {
        if (out && cap > 0) out[0] = '\0';
        return;
    }
    if (in[0] == '~' && (in[1] == '/' || in[1] == '\0')) {
        const char *home = get_user_home();
        if (home) {
            snprintf(out, cap, "%s%s", home, in + 1);
            return;
        }
    }
    strncpy(out, in, cap - 1);
    out[cap - 1] = '\0';
}

void hub_set_custom_dirs(const char *hub_dir, const char *backup_dir) {
    if (hub_dir && hub_dir[0] != '\0') {
        expand_path(hub_dir, g_custom_hub_dir, sizeof(g_custom_hub_dir));
    } else {
        g_custom_hub_dir[0] = '\0';
    }
    if (backup_dir && backup_dir[0] != '\0') {
        expand_path(backup_dir, g_custom_backup_dir, sizeof(g_custom_backup_dir));
    } else {
        g_custom_backup_dir[0] = '\0';
    }
}

void hub_get_custom_dirs(char *out_hub_dir, size_t hub_cap, char *out_backup_dir, size_t backup_cap) {
    if (out_hub_dir && hub_cap > 0) {
        strncpy(out_hub_dir, g_custom_hub_dir, hub_cap - 1);
        out_hub_dir[hub_cap - 1] = '\0';
    }
    if (out_backup_dir && backup_cap > 0) {
        strncpy(out_backup_dir, g_custom_backup_dir, backup_cap - 1);
        out_backup_dir[backup_cap - 1] = '\0';
    }
}

    int hub_init_dirs(char *out_hub_dir, size_t hub_cap, char *out_backup_dir, size_t backup_cap) {
    const char *home = get_user_home();
    if (!home) return -1;

    char base[512];
    snprintf(base, sizeof(base), "%s/.ai-space", home);
    AI_MKDIR(base);

    char hub[512];
    if (g_custom_hub_dir[0] != '\0') {
        strncpy(hub, g_custom_hub_dir, sizeof(hub) - 1);
        hub[sizeof(hub) - 1] = '\0';
    } else {
        snprintf(hub, sizeof(hub), "%s/.ai-space/skills", home);
    }
    AI_MKDIR(hub);

    char backup[512];
    if (g_custom_backup_dir[0] != '\0') {
        strncpy(backup, g_custom_backup_dir, sizeof(backup) - 1);
        backup[sizeof(backup) - 1] = '\0';
    } else {
        snprintf(backup, sizeof(backup), "%s/.ai-space/backups", home);
    }
    AI_MKDIR(backup);

    if (out_hub_dir && hub_cap > 0) {
        strncpy(out_hub_dir, hub, hub_cap - 1);
        out_hub_dir[hub_cap - 1] = '\0';
    }
    if (out_backup_dir && backup_cap > 0) {
        strncpy(out_backup_dir, backup, backup_cap - 1);
        out_backup_dir[backup_cap - 1] = '\0';
    }
    return 0;
}

static int copy_single_file(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) return -1;
    FILE *out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        return -1;
    }

    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            fclose(in);
            fclose(out);
            return -1;
        }
    }
    fclose(in);
    fclose(out);
    return 0;
}

int hub_copy_dir_recursive(const char *src_dir, const char *dst_dir) {
    if (!src_dir || !dst_dir) return -1;

    AI_MKDIR(dst_dir);
    DIR *d = opendir(src_dir);
    if (!d) return -1;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        if (strcmp(ent->d_name, "__pycache__") == 0) continue;

        char s_path[1024];
        char d_path[1024];
        snprintf(s_path, sizeof(s_path), "%s/%s", src_dir, ent->d_name);
        snprintf(d_path, sizeof(d_path), "%s/%s", dst_dir, ent->d_name);

        struct stat st;
        if (stat(s_path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                hub_copy_dir_recursive(s_path, d_path);
            } else {
                copy_single_file(s_path, d_path);
            }
        }
    }
    closedir(d);
    return 0;
}

static int backup_tool_skill(const char *tool_name, const char *skill_name, const char *skill_dir_path) {
    char backup_dir[512];
    hub_init_dirs(NULL, 0, backup_dir, sizeof(backup_dir));

    time_t now = time(NULL);
    struct tm tm_info;
    ai_localtime(&now, &tm_info);

    char time_str[32];
    strftime(time_str, sizeof(time_str), "%Y%m%d_%H%M%S", &tm_info);

    char dest_backup[1024];
    snprintf(dest_backup, sizeof(dest_backup), "%s/%s_%s_%s",
             backup_dir, time_str, tool_name, skill_name);

    return hub_copy_dir_recursive(skill_dir_path, dest_backup);
}

static int remove_dir_recursive(const char *dir_path) {
    if (!dir_path || dir_path[0] == '\0') return -1;
    DIR *d = opendir(dir_path);
    if (!d) return -1;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        char sub[1024];
        snprintf(sub, sizeof(sub), "%s/%s", dir_path, ent->d_name);
        struct stat st;
        if (stat(sub, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                remove_dir_recursive(sub);
            } else {
                unlink(sub);
            }
        }
    }
    closedir(d);
    return rmdir(dir_path);
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
                                   time_t *latest_mtime) {
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
        if (ent->d_name[0] == '.' || strcmp(ent->d_name, "__pycache__") == 0) continue;

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
            rc = collect_files_recursive(child.abs_path, child.rel_path, items, count, cap, latest_mtime);
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
    int rc = collect_files_recursive(dir_path, "", &items, &count, &capacity, &latest_mtime);
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

static void extract_desc(const char *skill_md_path, char *out_desc, size_t max_len) {
    out_desc[0] = '\0';
    FILE *f = fopen(skill_md_path, "r");
    if (!f) return;

    char line[512];
    bool in_fm = false;
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = '\0';
        if (strcmp(line, "---") == 0) {
            in_fm = !in_fm;
            continue;
        }
        if (in_fm) {
            if (strncmp(line, "description:", 12) == 0 || strncmp(line, "description_zh:", 15) == 0) {
                const char *p = strchr(line, ':') + 1;
                while (*p == ' ' || *p == '\t' || *p == '"' || *p == '\'') p++;
                strncpy(out_desc, p, max_len - 1);
                out_desc[max_len - 1] = '\0';
                size_t dlen = strlen(out_desc);
                while (dlen > 0 && (out_desc[dlen - 1] == '"' || out_desc[dlen - 1] == '\'')) out_desc[--dlen] = '\0';
                break;
            }
        }
    }
    fclose(f);
}

static int compare_skill_entries(const void *a, const void *b) {
    const hub_skill_entry_t *ea = (const hub_skill_entry_t *)a;
    const hub_skill_entry_t *eb = (const hub_skill_entry_t *)b;
    return strcmp(ea->skill_name, eb->skill_name);
}

int hub_scan_matrix_ex(sqlite3 *db, hub_matrix_t *out_matrix, hub_progress_cb cb, void *user_data) {
    if (!out_matrix) return -1;
    memset(out_matrix, 0, sizeof(hub_matrix_t));

    if (cb) cb(0.02f, "初始化技能主版本中心...", user_data);

    hub_init_dirs(out_matrix->hub_dir, sizeof(out_matrix->hub_dir),
                  out_matrix->backup_dir, sizeof(out_matrix->backup_dir));

    /* 1. Query registered tools from DB */
    if (cb) cb(0.05f, "正在读取已识别的本地工具...", user_data);
    sqlite3_stmt *stmt = NULL;
    const char *sql = "SELECT name, path FROM local_tools ORDER BY name ASC;";
    if (db && sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        int t_cap = 16;
        out_matrix->tools = (char **)malloc(sizeof(char *) * t_cap);
        out_matrix->tool_paths = (char **)malloc(sizeof(char *) * t_cap);

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            if (out_matrix->tool_count >= t_cap) {
                t_cap *= 2;
                out_matrix->tools = (char **)realloc(out_matrix->tools, sizeof(char *) * t_cap);
                out_matrix->tool_paths = (char **)realloc(out_matrix->tool_paths, sizeof(char *) * t_cap);
            }
            const char *name = (const char *)sqlite3_column_text(stmt, 0);
            const char *path = (const char *)sqlite3_column_text(stmt, 1);
            if (name && (strcmp(name, "ai-space") == 0 || strcmp(name, ".ai-space") == 0)) {
                continue; /* Skip Master Hub itself from being treated as a tool */
            }
            out_matrix->tools[out_matrix->tool_count] = strdup(name ? name : "");
            out_matrix->tool_paths[out_matrix->tool_count] = strdup(path ? path : "");
            out_matrix->tool_count++;
        }
        sqlite3_finalize(stmt);
    }

    /* 2. Gather all unique skill names from Hub + all registered Tools */
    if (cb) cb(0.12f, "正在检索各工具与主仓中的技能目录...", user_data);
    int name_cap = 64;
    int name_count = 0;
    char **unique_names = (char **)malloc(sizeof(char *) * name_cap);

    /* From Hub */
    DIR *hd = opendir(out_matrix->hub_dir);
    if (hd) {
        struct dirent *ent;
        while ((ent = readdir(hd)) != NULL) {
            if (ent->d_name[0] == '.') continue;
            char s_dir[1024];
            snprintf(s_dir, sizeof(s_dir), "%s/%s", out_matrix->hub_dir, ent->d_name);
            struct stat st;
            if (stat(s_dir, &st) == 0 && S_ISDIR(st.st_mode)) {
                if (name_count >= name_cap) {
                    name_cap *= 2;
                    unique_names = (char **)realloc(unique_names, sizeof(char *) * name_cap);
                }
                unique_names[name_count++] = strdup(ent->d_name);
            }
        }
        closedir(hd);
    }

    /* From each tool */
    for (int t = 0; t < out_matrix->tool_count; t++) {
        DIR *td = opendir(out_matrix->tool_paths[t]);
        if (!td) continue;

        struct dirent *ent;
        while ((ent = readdir(td)) != NULL) {
            if (ent->d_name[0] == '.') continue;
            char s_dir[1024];
            snprintf(s_dir, sizeof(s_dir), "%s/%s", out_matrix->tool_paths[t], ent->d_name);
            struct stat st;
            if (stat(s_dir, &st) == 0 && S_ISDIR(st.st_mode)) {
                /* Check if already in unique_names */
                bool found = false;
                for (int k = 0; k < name_count; k++) {
                    if (strcmp(unique_names[k], ent->d_name) == 0) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    if (name_count >= name_cap) {
                        name_cap *= 2;
                        unique_names = (char **)realloc(unique_names, sizeof(char *) * name_cap);
                    }
                    unique_names[name_count++] = strdup(ent->d_name);
                }
            }
        }
        closedir(td);
    }

    /* 3. Build detailed skill matrix entries */
    out_matrix->skills = (hub_skill_entry_t *)calloc(name_count, sizeof(hub_skill_entry_t));
    out_matrix->skill_count = name_count;
    out_matrix->total_unique_skills = name_count;

    for (int i = 0; i < name_count; i++) {
        hub_skill_entry_t *entry = &out_matrix->skills[i];
        strncpy(entry->skill_name, unique_names[i], sizeof(entry->skill_name) - 1);

        if (cb && (i % 4 == 0 || i == name_count - 1)) {
            float p = 0.15f + 0.80f * ((float)(i + 1) / (float)(name_count ? name_count : 1));
            char p_msg[128];
            snprintf(p_msg, sizeof(p_msg), "正在比对分析 (%d/%d): %s", i + 1, name_count, entry->skill_name);
            cb(p, p_msg, user_data);
        }

        /* Check Hub */
        snprintf(entry->hub_path, sizeof(entry->hub_path), "%s/%s",
                 out_matrix->hub_dir, entry->skill_name);

        struct stat h_st;
        if (stat(entry->hub_path, &h_st) == 0 && S_ISDIR(h_st.st_mode)) {
            entry->in_hub = true;
            hub_compute_skill_hash(entry->hub_path, entry->hub_hash, sizeof(entry->hub_hash), &entry->hub_mtime);
            out_matrix->hub_managed_skills++;

            char h_md[1200];
            snprintf(h_md, sizeof(h_md), "%s/SKILL.md", entry->hub_path);
            extract_desc(h_md, entry->description, sizeof(entry->description));
        } else {
            entry->in_hub = false;
            entry->hub_hash[0] = '\0';
            entry->hub_mtime = 0;
        }

        /* Check each Tool */
        entry->tool_count = out_matrix->tool_count;
        entry->tool_copies = (tool_copy_status_t *)calloc(out_matrix->tool_count, sizeof(tool_copy_status_t));

        bool is_drifted = false;

        for (int t = 0; t < out_matrix->tool_count; t++) {
            tool_copy_status_t *tc = &entry->tool_copies[t];
            strncpy(tc->tool_name, out_matrix->tools[t], sizeof(tc->tool_name) - 1);
            strncpy(tc->tool_skills_path, out_matrix->tool_paths[t], sizeof(tc->tool_skills_path) - 1);
            snprintf(tc->skill_path, sizeof(tc->skill_path), "%s/%s",
                     out_matrix->tool_paths[t], entry->skill_name);

            struct stat t_st;
            if (stat(tc->skill_path, &t_st) == 0 && S_ISDIR(t_st.st_mode)) {
                tc->exists = true;
                entry->installed_count++;
                hub_compute_skill_hash(tc->skill_path, tc->hash, sizeof(tc->hash), &tc->mtime);

                if (entry->description[0] == '\0') {
                    char t_md[1200];
                    snprintf(t_md, sizeof(t_md), "%s/SKILL.md", tc->skill_path);
                    extract_desc(t_md, entry->description, sizeof(entry->description));
                }

                if (!entry->in_hub) {
                    tc->status = HUB_STATUS_NO_HUB;
                    is_drifted = true;
                } else if (strcmp(tc->hash, entry->hub_hash) == 0) {
                    tc->status = HUB_STATUS_IN_SYNC;
                } else if (tc->mtime >= entry->hub_mtime) {
                    tc->status = HUB_STATUS_LOCAL_MODIFIED;
                    entry->modified_count++;
                    is_drifted = true;
                } else {
                    tc->status = HUB_STATUS_OUTDATED;
                    entry->outdated_count++;
                    is_drifted = true;
                }
            } else {
                tc->exists = false;
                tc->status = HUB_STATUS_NOT_INSTALLED;
            }
        }

        if (is_drifted) {
            out_matrix->drifted_skills_count++;
        }

        free(unique_names[i]);
    }
    free(unique_names);

    if (out_matrix->skill_count > 1) {
        qsort(out_matrix->skills, out_matrix->skill_count, sizeof(hub_skill_entry_t), compare_skill_entries);
    }

    time_t now = time(NULL);
    struct tm tm_info;
    ai_localtime(&now, &tm_info);
    strftime(out_matrix->last_scanned_at, sizeof(out_matrix->last_scanned_at),
             "%Y-%m-%d %H:%M:%S", &tm_info);

    if (cb) cb(1.0f, "比对分析完成", user_data);
    return 0;
}

int hub_scan_matrix(sqlite3 *db, hub_matrix_t *out_matrix) {
    return hub_scan_matrix_ex(db, out_matrix, NULL, NULL);
}

void hub_free_matrix(hub_matrix_t *matrix) {
    if (!matrix) return;

    if (matrix->tools) {
        for (int i = 0; i < matrix->tool_count; i++) {
            if (matrix->tools[i]) free(matrix->tools[i]);
            if (matrix->tool_paths[i]) free(matrix->tool_paths[i]);
        }
        free(matrix->tools);
        free(matrix->tool_paths);
        matrix->tools = NULL;
        matrix->tool_paths = NULL;
    }

    if (matrix->skills) {
        for (int i = 0; i < matrix->skill_count; i++) {
            if (matrix->skills[i].tool_copies) {
                free(matrix->skills[i].tool_copies);
            }
        }
        free(matrix->skills);
        matrix->skills = NULL;
    }
    matrix->skill_count = 0;
}

int hub_promote_to_master(const char *tool_skills_path, const char *skill_name) {
    if (!tool_skills_path || !skill_name) return -1;

    char hub_dir[512];
    hub_init_dirs(hub_dir, sizeof(hub_dir), NULL, 0);

    char src[1024];
    char dst[1024];
    snprintf(src, sizeof(src), "%s/%s", tool_skills_path, skill_name);
    snprintf(dst, sizeof(dst), "%s/%s", hub_dir, skill_name);

    struct stat st;
    if (stat(src, &st) != 0 || !S_ISDIR(st.st_mode)) {
        return -1;
    }

    /* Backup existing master before replacing */
    if (stat(dst, &st) == 0 && S_ISDIR(st.st_mode)) {
        backup_tool_skill("master", skill_name, dst);
        remove_dir_recursive(dst);
    }

    return hub_copy_dir_recursive(src, dst);
}

int hub_sync_master_to_tool(const char *tool_skills_path, const char *skill_name, const char *tool_name) {
    if (!tool_skills_path || !skill_name) return -1;

    char hub_dir[512];
    hub_init_dirs(hub_dir, sizeof(hub_dir), NULL, 0);

    char src[1024];
    char dst[1024];
    snprintf(src, sizeof(src), "%s/%s", hub_dir, skill_name);
    snprintf(dst, sizeof(dst), "%s/%s", tool_skills_path, skill_name);

    struct stat st;
    if (stat(src, &st) != 0 || !S_ISDIR(st.st_mode)) {
        return -1; /* Hub does not have this skill */
    }

    /* Backup existing tool skill before overwriting */
    if (stat(dst, &st) == 0 && S_ISDIR(st.st_mode)) {
        backup_tool_skill(tool_name ? tool_name : "tool", skill_name, dst);
        remove_dir_recursive(dst);
    }

    return hub_copy_dir_recursive(src, dst);
}

int hub_broadcast_skill(const hub_matrix_t *matrix, const char *skill_name, int *out_synced_count) {
    if (!matrix || !skill_name) return -1;

    int synced = 0;
    for (int i = 0; i < matrix->skill_count; i++) {
        if (strcmp(matrix->skills[i].skill_name, skill_name) == 0) {
            hub_skill_entry_t *entry = &matrix->skills[i];
            if (!entry->in_hub) return -1;

            for (int t = 0; t < entry->tool_count; t++) {
                if (entry->tool_copies[t].exists) {
                    if (hub_sync_master_to_tool(entry->tool_copies[t].tool_skills_path,
                                                skill_name, entry->tool_copies[t].tool_name) == 0) {
                        synced++;
                    }
                }
            }
            break;
        }
    }

    if (out_synced_count) *out_synced_count = synced;
    return 0;
}

int hub_distribute_skill_to_all_tools(const hub_matrix_t *matrix, const char *skill_name, int *out_synced_count) {
    if (!matrix || !skill_name) return -1;

    int synced = 0;
    for (int t = 0; t < matrix->tool_count; t++) {
        const char *t_name = matrix->tools[t];
        const char *t_path = matrix->tool_paths[t];
        if (!t_path || t_path[0] == '\0') continue;

        if (hub_sync_master_to_tool(t_path, skill_name, t_name) == 0) {
            synced++;
        }
    }

    if (out_synced_count) *out_synced_count = synced;
    return 0;
}

int hub_distribute_all_to_all_tools(const hub_matrix_t *matrix, int *out_distributed_count,
                                    hub_progress_cb cb, void *user_data) {
    if (!matrix) return -1;

    int total_actions = 0;
    int master_count = 0;
    for (int i = 0; i < matrix->skill_count; i++) {
        if (matrix->skills[i].in_hub) master_count++;
    }

    int current = 0;
    for (int i = 0; i < matrix->skill_count; i++) {
        hub_skill_entry_t *entry = &matrix->skills[i];
        if (!entry->in_hub) continue;

        current++;
        if (cb) {
            float p = (float)current / (float)(master_count ? master_count : 1);
            char p_msg[128];
            snprintf(p_msg, sizeof(p_msg), "正在全量分发主仓技能 (%d/%d): %s", current, master_count, entry->skill_name);
            cb(p, p_msg, user_data);
        }

        int synced = 0;
        hub_distribute_skill_to_all_tools(matrix, entry->skill_name, &synced);
        total_actions += synced;
    }

    if (out_distributed_count) *out_distributed_count = total_actions;
    return 0;
}

int hub_uninstall_skill_from_tool(const char *tool_skills_path, const char *skill_name, const char *tool_name) {
    if (!tool_skills_path || !skill_name) return -1;

    char target[1024];
    snprintf(target, sizeof(target), "%s/%s", tool_skills_path, skill_name);

    struct stat st;
    if (stat(target, &st) != 0 || !S_ISDIR(st.st_mode)) {
        return 0; /* Not installed */
    }

    /* Backup before uninstalling */
    backup_tool_skill(tool_name ? tool_name : "tool", skill_name, target);
    return remove_dir_recursive(target);
}

int hub_uninstall_skill_from_all_tools(const hub_matrix_t *matrix, const char *skill_name) {
    if (!matrix || !skill_name || skill_name[0] == '\0') return -1;
    int uninstalled = 0;
    for (int i = 0; i < matrix->skill_count; i++) {
        const hub_skill_entry_t *entry = &matrix->skills[i];
        if (strcmp(entry->skill_name, skill_name) == 0) {
            for (int t = 0; t < entry->tool_count; t++) {
                if (entry->tool_copies[t].exists) {
                    if (hub_uninstall_skill_from_tool(entry->tool_copies[t].tool_skills_path,
                                                      skill_name,
                                                      entry->tool_copies[t].tool_name) == 0) {
                        uninstalled++;
                    }
                }
            }
            break;
        }
    }
    return uninstalled;
}

int hub_adopt_all_to_master_ex(const hub_matrix_t *matrix, int *out_adopted_count,
                              hub_progress_cb cb, void *user_data) {
    if (!matrix) return -1;

    int adopted = 0;
    int total = matrix->skill_count;
    for (int i = 0; i < total; i++) {
        hub_skill_entry_t *entry = &matrix->skills[i];
        if (cb && (i % 3 == 0 || i == total - 1)) {
            float p = (float)(i + 1) / (float)(total ? total : 1);
            char p_msg[128];
            snprintf(p_msg, sizeof(p_msg), "正在收录主库 (%d/%d): %s", i + 1, total, entry->skill_name);
            cb(p, p_msg, user_data);
        }
        if (!entry->in_hub && entry->installed_count > 0) {
            /* Find first existing tool copy to promote */
            for (int t = 0; t < entry->tool_count; t++) {
                if (entry->tool_copies[t].exists) {
                    if (hub_promote_to_master(entry->tool_copies[t].tool_skills_path,
                                              entry->skill_name) == 0) {
                        adopted++;
                    }
                    break;
                }
            }
        }
    }

    if (cb) cb(1.0f, "全量收录主库完成", user_data);
    if (out_adopted_count) *out_adopted_count = adopted;
    return 0;
}

int hub_adopt_all_to_master(const hub_matrix_t *matrix, int *out_adopted_count) {
    return hub_adopt_all_to_master_ex(matrix, out_adopted_count, NULL, NULL);
}

int hub_delete_master_skill(const char *skill_name) {
    if (!skill_name || skill_name[0] == '\0') return -1;
    char hub_dir[512];
    hub_init_dirs(hub_dir, sizeof(hub_dir), NULL, 0);

    char target[1024];
    snprintf(target, sizeof(target), "%s/%s", hub_dir, skill_name);

    struct stat st;
    if (stat(target, &st) == 0 && S_ISDIR(st.st_mode)) {
        backup_tool_skill("master", skill_name, target);
        return remove_dir_recursive(target);
    }
    return -1;
}
