#include "skill_fingerprint.h"
#include "hub_paths.h"
#include "skill_metadata.h"
#include "hub_manager.h"
#include "file_tree.h"
#include "portability.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>

typedef struct {
    char name[128];
    int tool;
} skill_location_t;

static int compare_locations(const void *a, const void *b) {
    const skill_location_t *lhs = a, *rhs = b;
    int names = strcmp(lhs->name, rhs->name);
    return names ? names : lhs->tool - rhs->tool;
}

static int load_tools(sqlite3 *db, hub_matrix_t *matrix) {
    if (!db) return 0;
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, "SELECT name,path FROM local_tools ORDER BY name", -1, &stmt, NULL) != SQLITE_OK)
        return -1;
    int rc, capacity = 0;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char *name = (const char *)sqlite3_column_text(stmt, 0);
        const char *path = (const char *)sqlite3_column_text(stmt, 1);
        if (!name || !path) { rc = SQLITE_ERROR; break; }
        if (strcmp(name, "ai-space") == 0 || strcmp(name, ".ai-space") == 0) continue;
        if (matrix->tool_count == capacity) {
            if (capacity > INT_MAX / 2) { rc = SQLITE_TOOBIG; break; }
            capacity = capacity ? capacity * 2 : 16;
            void *tools = realloc(matrix->tools, (size_t)capacity * sizeof(*matrix->tools));
            if (!tools) { rc = SQLITE_NOMEM; break; }
            matrix->tools = tools;
            void *paths = realloc(matrix->tool_paths, (size_t)capacity * sizeof(*matrix->tool_paths));
            if (!paths) { rc = SQLITE_NOMEM; break; }
            matrix->tool_paths = paths;
        }
        char *tool = strdup(name), *root = strdup(path);
        if (!tool || !root) {
            free(tool);
            free(root);
            rc = SQLITE_NOMEM;
            break;
        }
        matrix->tools[matrix->tool_count] = tool;
        matrix->tool_paths[matrix->tool_count++] = root;
    }
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? 0 : -1;
}

static int collect_locations(const char *root, int tool, skill_location_t **items,
                             size_t *count, size_t *capacity) {
    DIR *dir = opendir(root);
    if (!dir) return errno == ENOENT || errno == ENOTDIR ? 0 : -1;
    int rc = 0;
    for (;;) {
        errno = 0;
        struct dirent *entry = readdir(dir);
        if (!entry) {
            if (errno) rc = -1;
            break;
        }
        if (entry->d_name[0] == '.') continue;
        char path[SYNC_PATH_CAP];
        struct stat st;
        if (sync_join(path, sizeof(path), root, entry->d_name) != 0) { rc = -1; break; }
        if (stat(path, &st) != 0) {
            if (errno == ENOENT) continue;
            rc = -1;
            break;
        }
        if (!S_ISDIR(st.st_mode)) continue;
        if (strlen(entry->d_name) >= sizeof((*items)[0].name)) { rc = -1; break; }
        if (*count == *capacity) {
            if (*capacity > SIZE_MAX / 2 / sizeof(**items)) { rc = -1; break; }
            size_t grown = *capacity ? *capacity * 2 : 64;
            void *locations = realloc(*items, grown * sizeof(**items));
            if (!locations) { rc = -1; break; }
            *items = locations;
            *capacity = grown;
        }
        skill_location_t *item = &(*items)[(*count)++];
        snprintf(item->name, sizeof(item->name), "%s", entry->d_name);
        item->tool = tool;
    }
    closedir(dir);
    return rc;
}

static void read_description(const char *root, char *out, size_t cap) {
    char path[SYNC_PATH_CAP + sizeof(SKILL_DOCUMENT)];
    if (sync_join(path, sizeof(path), root, SKILL_DOCUMENT) == 0)
        skill_read_description(path, out, cap, false);
}

static int build_entry(hub_matrix_t *matrix, const skill_location_t *locations, size_t count, const int *aliases) {
    hub_skill_entry_t *entry = &matrix->skills[matrix->skill_count++];
    snprintf(entry->skill_name, sizeof(entry->skill_name), "%s", locations[0].name);
    if (sync_join(entry->hub_path, sizeof(entry->hub_path), matrix->hub_dir, entry->skill_name) != 0)
        return -1;
    if (locations[0].tool == -1) {
        entry->in_hub = true;
        entry->hub_unreadable = hub_compute_skill_hash(entry->hub_path, entry->hub_hash,
                                                       sizeof(entry->hub_hash), &entry->hub_mtime) != 0;
        read_description(entry->hub_path, entry->description, sizeof(entry->description));
        matrix->hub_managed_skills++;
        locations++;
        count--;
    }

    entry->tool_count = matrix->tool_count;
    if (entry->tool_count) {
        entry->tool_copies = calloc((size_t)entry->tool_count, sizeof(*entry->tool_copies));
        if (!entry->tool_copies) return -1;
    }
    for (int tool = 0; tool < entry->tool_count; tool++) {
        tool_copy_status_t *copy = &entry->tool_copies[tool];
        snprintf(copy->tool_name, sizeof(copy->tool_name), "%s", matrix->tools[tool]);
        snprintf(copy->tool_skills_path, sizeof(copy->tool_skills_path), "%s", matrix->tool_paths[tool]);
        if (sync_join(copy->skill_path, sizeof(copy->skill_path), matrix->tool_paths[tool], entry->skill_name) != 0)
            return -1;
        copy->status = HUB_STATUS_NOT_INSTALLED;
    }

    bool drifted = entry->hub_unreadable;
    for (size_t i = 0; i < count; i++) {
        int tool = locations[i].tool;
        tool_copy_status_t *copy = &entry->tool_copies[tool];
        copy->exists = true;
        entry->installed_count++;
        const tool_copy_status_t *alias = &entry->tool_copies[aliases[tool]];
        bool readable;
        if (alias != copy && alias->exists) {
            memcpy(copy->hash, alias->hash, sizeof(copy->hash));
            copy->mtime = alias->mtime;
            readable = copy->hash[0] != '\0';
        } else {
            readable = hub_compute_skill_hash(copy->skill_path, copy->hash,
                                              sizeof(copy->hash), &copy->mtime) == 0;
        }
        if (!entry->description[0]) read_description(copy->skill_path, entry->description, sizeof(entry->description));
        if (!readable || entry->hub_unreadable) {
            copy->status = HUB_STATUS_UNREADABLE;
            entry->unreadable_count++;
        }
        else if (!entry->in_hub) copy->status = HUB_STATUS_NO_HUB;
        else if (strcmp(copy->hash, entry->hub_hash) == 0) copy->status = HUB_STATUS_IN_SYNC;
        else if (copy->mtime >= entry->hub_mtime) {
            copy->status = HUB_STATUS_LOCAL_MODIFIED;
            entry->modified_count++;
        } else {
            copy->status = HUB_STATUS_OUTDATED;
            entry->outdated_count++;
        }
        if (copy->status != HUB_STATUS_IN_SYNC) drifted = true;
    }
    if (drifted) matrix->drifted_skills_count++;
    return 0;
}

int hub_scan_matrix_ex(sqlite3 *db, hub_matrix_t *matrix, hub_progress_cb cb, void *user) {
    if (!matrix) return -1;
    *matrix = (hub_matrix_t){0};
    skill_location_t *locations = NULL;
    int *aliases = NULL;
    size_t count = 0, capacity = 0;
    int rc = -1;
    if (cb) cb(0.05f, "正在读取主仓和本地工具", user);
    if (hub_init_dirs(matrix->hub_dir, sizeof(matrix->hub_dir), matrix->backup_dir, sizeof(matrix->backup_dir)) != 0 ||
        load_tools(db, matrix) != 0 ||
        collect_locations(matrix->hub_dir, -1, &locations, &count, &capacity) != 0) goto done;
    if (matrix->tool_count) {
        aliases = malloc((size_t)matrix->tool_count * sizeof(*aliases));
        if (!aliases) goto done;
    }
    for (int tool = 0; tool < matrix->tool_count; tool++) {
        aliases[tool] = tool;
        for (int i = 0; i < tool; i++) {
            if (strcmp(matrix->tool_paths[i], matrix->tool_paths[tool]) == 0) {
                aliases[tool] = i;
                break;
            }
        }
        if (collect_locations(matrix->tool_paths[tool], tool, &locations, &count, &capacity) != 0)
            goto done;
    }
    if (count > 1) qsort(locations, count, sizeof(*locations), compare_locations);
    size_t unique = 0;
    for (size_t i = 0; i < count; i++)
        if (i == 0 || strcmp(locations[i - 1].name, locations[i].name) != 0) unique++;
    if (unique > INT_MAX) goto done;
    if (unique) {
        matrix->skills = calloc(unique, sizeof(*matrix->skills));
        if (!matrix->skills) goto done;
    }

    for (size_t i = 0; i < count;) {
        size_t end = i + 1;
        while (end < count && strcmp(locations[i].name, locations[end].name) == 0) end++;
        if (cb && (matrix->skill_count % 4 == 0 || end == count)) {
            char message[192];
            snprintf(message, sizeof(message), "正在比对 (%d/%zu) · %s",
                     matrix->skill_count + 1, unique, locations[i].name);
            cb(0.15f + 0.80f * (float)end / (float)count, message, user);
        }
        if (build_entry(matrix, locations + i, end - i, aliases) != 0) goto done;
        i = end;
    }

    matrix->total_unique_skills = matrix->skill_count;
    time_t now = time(NULL);
    struct tm tm_info;
    if (ai_localtime(&now, &tm_info))
        strftime(matrix->last_scanned_at, sizeof(matrix->last_scanned_at), "%Y-%m-%d %H:%M:%S", &tm_info);
    if (cb) cb(1.0f, "比对分析完成", user);
    rc = 0;

done:
    free(aliases);
    free(locations);
    if (rc != 0) {
        hub_free_matrix(matrix);
        *matrix = (hub_matrix_t){0};
    }
    return rc;
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
