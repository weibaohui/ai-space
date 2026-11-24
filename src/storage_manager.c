#include "storage_manager.h"
#include "file_tree.h"
#include "portability.h"

#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>

/* 按普通文件统计占用，符号链接不会计入其目标内容。 */
static int64_t compute_dir_size_recursive(const char *dir_path, int *out_file_count) {
    if (!dir_path || !dir_path[0]) return 0;
    DIR *d = opendir(dir_path);
    if (!d) return 0;

    int64_t total = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        char sub[1024];
        if (sync_join(sub, sizeof(sub), dir_path, ent->d_name) != 0) continue;
        struct stat st;
        if (lstat(sub, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                total += compute_dir_size_recursive(sub, out_file_count);
            } else if (S_ISREG(st.st_mode)) {
                total += (int64_t)st.st_size;
                if (out_file_count) (*out_file_count)++;
            }
        }
    }
    closedir(d);
    return total;
}

/* 从快照目录名还原时间、工具和技能名称。 */
static void parse_snapshot_name(const char *dir_name, char *out_time, size_t time_cap,
                                char *out_tool, size_t tool_cap,
                                char *out_skill, size_t skill_cap) {
    if (!dir_name) return;
    if (out_time && time_cap > 0) out_time[0] = '\0';
    if (out_tool && tool_cap > 0) out_tool[0] = '\0';
    if (out_skill && skill_cap > 0) out_skill[0] = '\0';

    /* 例如 20261122_220139_agent_agent-browser。 */
    if (strlen(dir_name) >= 16 && dir_name[8] == '_' && dir_name[15] == '_') {
        if (out_time && time_cap >= 20) {
            snprintf(out_time, time_cap, "%.4s-%.2s-%.2s %.2s:%.2s:%.2s",
                     dir_name, dir_name + 4, dir_name + 6,
                     dir_name + 9, dir_name + 11, dir_name + 13);
        }

        const char *p = dir_name + 16;
        const char *next_us = strchr(p, '_');
        if (next_us) {
            size_t tlen = (size_t)(next_us - p);
            if (out_tool && tool_cap > 0) {
                if (tlen >= tool_cap) tlen = tool_cap - 1;
                strncpy(out_tool, p, tlen);
                out_tool[tlen] = '\0';
            }
            if (out_skill && skill_cap > 0) {
                strncpy(out_skill, next_us + 1, skill_cap - 1);
                out_skill[skill_cap - 1] = '\0';
            }
            return;
        }
    }

    /* 保留旧命名的备份目录，使用原名展示。 */
    if (out_time && time_cap > 0) strncpy(out_time, "快照时间", time_cap - 1);
    if (out_tool && tool_cap > 0) strncpy(out_tool, "本地备份", tool_cap - 1);
    if (out_skill && skill_cap > 0) strncpy(out_skill, dir_name, skill_cap - 1);
}

typedef struct {
    char name[sizeof(((storage_snapshot_item_t *)0)->dir_name)];
    time_t mtime;
} snapshot_entry_t;

static int compare_snapshots_desc(const void *a, const void *b) {
    const snapshot_entry_t *first = a;
    const snapshot_entry_t *second = b;
    if (first->mtime != second->mtime) return first->mtime > second->mtime ? -1 : 1;
    return strcmp(second->name, first->name);
}

static int list_snapshots(const char *root, snapshot_entry_t **items, size_t *count) {
    DIR *dir = opendir(root);
    if (!dir) return errno == ENOENT ? 0 : -1;
    size_t capacity = 0;
    int rc = 0;
    while (true) {
        errno = 0;
        struct dirent *entry = readdir(dir);
        if (!entry) {
            if (errno) rc = -1;
            break;
        }
        if (entry->d_name[0] == '.') continue;
        char path[SYNC_PATH_CAP];
        struct stat st;
        if (!sync_valid_component(entry->d_name) ||
            sync_join(path, sizeof(path), root, entry->d_name) != 0 ||
            lstat(path, &st) != 0) {
            rc = -1;
            break;
        }
        if (!S_ISDIR(st.st_mode)) continue;
        if (strlen(entry->d_name) >= sizeof((*items)[0].name)) {
            rc = -1;
            break;
        }
        if (*count == capacity) {
            size_t next_capacity = capacity ? 2 * capacity : 32;
            void *next = realloc(*items, next_capacity * sizeof(**items));
            if (!next) {
                rc = -1;
                break;
            }
            *items = next;
            capacity = next_capacity;
        }
        snapshot_entry_t *item = &(*items)[(*count)++];
        snprintf(item->name, sizeof(item->name), "%s", entry->d_name);
        item->mtime = st.st_mtime;
    }
    closedir(dir);
    if (rc == 0 && *count > 1)
        qsort(*items, *count, sizeof(**items), compare_snapshots_desc);
    return rc;
}

void storage_probe_directory(const char *path, storage_directory_info_t *info) {
    *info = (storage_directory_info_t){0};
    if (sync_expand(path, info->expanded, sizeof(info->expanded)) != 0) {
        info->expanded[0] = '\0';
        return;
    }

    struct stat st;
    if (stat(info->expanded, &st) != 0 || !S_ISDIR(st.st_mode)) return;
    info->exists = true;
    info->writable = AI_ACCESS(info->expanded, R_OK | W_OK) == 0;

    DIR *dir = opendir(info->expanded);
    if (!dir) return;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (entry->d_name[0] == '.') continue;
        char child[SYNC_PATH_CAP];
        if (sync_join(child, sizeof(child), info->expanded, entry->d_name) == 0 &&
            stat(child, &st) == 0 && S_ISDIR(st.st_mode)) info->subdirs++;
    }
    closedir(dir);
}

void storage_collect(const char *hub_dir, const char *backup_dir, const char *cache_dir,
                     storage_stats_t *st) {
    memset(st, 0, sizeof(storage_stats_t));

    /* 备份统计使用完整快照列表，页面只展示其中最新的一部分。 */
    char exp_backup[SYNC_PATH_CAP] = "";
    if (sync_expand(backup_dir, exp_backup, sizeof(exp_backup)) != 0) exp_backup[0] = '\0';

    snapshot_entry_t *snapshots = NULL;
    size_t count = 0;
    if (exp_backup[0] && list_snapshots(exp_backup, &snapshots, &count) == 0) {
        st->snapshot_total = count;
        for (size_t i = 0; i < count; i++) {
            char path[SYNC_PATH_CAP];
            if (sync_join(path, sizeof(path), exp_backup, snapshots[i].name) != 0) continue;
            int files = 0;
            int64_t size = compute_dir_size_recursive(path, &files);
            st->backup_total_bytes += size;
            st->backup_file_count += files;

            if ((size_t)st->snapshot_count >= sizeof(st->snapshots) / sizeof(st->snapshots[0])) continue;
            storage_snapshot_item_t *item = &st->snapshots[st->snapshot_count++];
            snprintf(item->dir_name, sizeof(item->dir_name), "%s", snapshots[i].name);
            snprintf(item->full_path, sizeof(item->full_path), "%s", path);
            item->mtime = snapshots[i].mtime;
            item->size_bytes = size;
            item->file_count = files;
            parse_snapshot_name(item->dir_name, item->time_str, sizeof(item->time_str),
                                item->tool_name, sizeof(item->tool_name),
                                item->skill_name, sizeof(item->skill_name));
        }
    }
    free(snapshots);

    /* 统计可清理的应用缓存占用。 */
    char exp_cache[SYNC_PATH_CAP] = "";
    if (sync_expand(cache_dir, exp_cache, sizeof(exp_cache)) != 0) exp_cache[0] = '\0';

    DIR *cd = opendir(exp_cache);
    if (cd) {
        struct dirent *ent;
        while ((ent = readdir(cd)) != NULL) {
            if (ent->d_name[0] == '.') continue;
            char item_path[1024];
            if (sync_join(item_path, sizeof(item_path), exp_cache, ent->d_name) != 0) continue;

            struct stat fst;
            if (lstat(item_path, &fst) == 0) {
                if (S_ISDIR(fst.st_mode)) {
                    st->cache_dir_count++;
                    int fc = 0;
                    st->cache_total_bytes += compute_dir_size_recursive(item_path, &fc);
                    st->cache_file_count += fc;
                } else if (S_ISREG(fst.st_mode)) {
                    st->cache_total_bytes += (int64_t)fst.st_size;
                    st->cache_file_count++;
                }
            }
        }
        closedir(cd);
    }

    /* 主仓技能文件单独计入主仓占用。 */
    char exp_hub[SYNC_PATH_CAP] = "";
    if (sync_expand(hub_dir, exp_hub, sizeof(exp_hub)) != 0) exp_hub[0] = '\0';

    DIR *hd = opendir(exp_hub);
    if (hd) {
        struct dirent *ent;
        while ((ent = readdir(hd)) != NULL) {
            if (ent->d_name[0] == '.') continue;
            char item_path[1024];
            if (sync_join(item_path, sizeof(item_path), exp_hub, ent->d_name) != 0) continue;

            struct stat fst;
            if (lstat(item_path, &fst) == 0 && S_ISDIR(fst.st_mode)) {
                st->hub_skill_count++;
                int fc = 0;
                st->hub_total_bytes += compute_dir_size_recursive(item_path, &fc);
            }
        }
        closedir(hd);
    }

    /* 记录统计完成时间，便于界面展示刷新时间。 */
    time_t now = time(NULL);
    struct tm tm_info;
    if (ai_localtime(&now, &tm_info))
        strftime(st->last_checked_at, sizeof(st->last_checked_at), "%Y-%m-%d %H:%M:%S", &tm_info);

    st->loaded = true;
}

/* 清理入口只接受实际目录，防止偏好路径指向文件或链接。 */
static int cleanup_root(const char *path, char *root) {
    if (sync_expand(path, root, SYNC_PATH_CAP) != 0) return -1;
    size_t len = strlen(root);
    while (len > 1 && root[len - 1] == '/') root[--len] = '\0';
    if (strcmp(root, "/") == 0 || strcmp(root, ".") == 0 || strcmp(root, "..") == 0) return -1;
#ifdef _WIN32
    if (len <= 3 && len >= 2 && root[1] == ':') return -1;
#endif
    struct stat st;
    if (lstat(root, &st) != 0) return errno == ENOENT ? 1 : -1;
    return S_ISDIR(st.st_mode) ? 0 : -1;
}

int storage_clear_directory(const char *path) {
    char root[SYNC_PATH_CAP];
    int status = cleanup_root(path, root);
    if (status != 0) return status < 0 ? -1 : 0;

    DIR *dir = opendir(root);
    if (!dir) return -1;
    int rc = 0;
    while (true) {
        errno = 0;
        struct dirent *entry = readdir(dir);
        if (!entry) {
            if (errno) rc = -1;
            break;
        }
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        char child[SYNC_PATH_CAP];
        if (!sync_valid_component(entry->d_name) ||
            sync_join(child, sizeof(child), root, entry->d_name) != 0 ||
            sync_remove_tree(child) != 0) rc = -1;
    }
    closedir(dir);
    return rc;
}

int storage_prune_snapshots(const char *backup_dir, int keep_count) {
    char root[SYNC_PATH_CAP];
    if (keep_count < 0) return -1;
    int status = cleanup_root(backup_dir, root);
    if (status != 0) return status < 0 ? -1 : 0;

    snapshot_entry_t *items = NULL;
    size_t count = 0;
    int rc = list_snapshots(root, &items, &count);
    if (rc == 0) {
        for (size_t i = (size_t)keep_count; i < count; i++) {
            char child[SYNC_PATH_CAP];
            if (sync_join(child, sizeof(child), root, items[i].name) != 0 ||
                sync_remove_tree(child) != 0) rc = -1;
        }
    }
    free(items);
    return rc;
}

int storage_delete_snapshot(const char *backup_dir, const char *name) {
    char root[SYNC_PATH_CAP], child[SYNC_PATH_CAP];
    if (!sync_valid_component(name) || name[0] == '.') return -1;
    int status = cleanup_root(backup_dir, root);
    if (status != 0) return status < 0 ? -1 : 0;
    if (sync_join(child, sizeof(child), root, name) != 0) return -1;
    struct stat st;
    if (lstat(child, &st) != 0) return errno == ENOENT ? 0 : -1;
    return S_ISDIR(st.st_mode) ? sync_remove_tree(child) : -1;
}
