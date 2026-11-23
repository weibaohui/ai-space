#include "sync_directory.h"
#include "hub_manager.h"
#include "portability.h"

#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <sys/utime.h>
#else
#include <utime.h>
#endif

bool sync_valid_component(const char *name) {
    if (!name || !name[0] || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return false;
    for (const unsigned char *p = (const unsigned char *)name; *p; p++) {
        if (*p < 32 || *p == 127 || strchr("/\\:", *p)) return false;
    }
    return true;
}

bool sync_skip_name(const char *name) {
    return name[0] == '.' || strcmp(name, "__pycache__") == 0;
}

int sync_join(char *out, size_t cap, const char *parent, const char *name) {
    int n = snprintf(out, cap, "%s/%s", parent, name);
    return n >= 0 && (size_t)n < cap ? 0 : -1;
}

int sync_expand(const char *path, char *out, size_t cap) {
    if (!path || !path[0]) return -1;
    int n;
    if (path[0] == '~' && (path[1] == '/' || path[1] == '\0')) {
        const char *home = ai_home_dir();
        if (!home || !home[0]) return -1;
        n = snprintf(out, cap, "%s%s", home, path + 1);
    } else {
        n = snprintf(out, cap, "%s", path);
    }
    return n >= 0 && (size_t)n < cap ? 0 : -1;
}

bool sync_is_skill(const char *path) {
    char skill_md[SYNC_PATH_CAP];
    struct stat st;
    return lstat(path, &st) == 0 && S_ISDIR(st.st_mode) &&
           sync_join(skill_md, sizeof(skill_md), path, "SKILL.md") == 0 &&
           lstat(skill_md, &st) == 0 && S_ISREG(st.st_mode);
}

int sync_make_stage(const char *root, char *out, size_t cap) {
    for (int attempt = 0; attempt < 128; attempt++) {
        int n = snprintf(out, cap, "%s/.ai-space-stage-%ld-%d", root, (long)getpid(), attempt);
        if (n < 0 || (size_t)n >= cap) return -1;
#ifdef _WIN32
        if (AI_MKDIR(out) == 0) return 0;
#else
        if (mkdir(out, 0700) == 0) return 0;
#endif
        if (errno != EEXIST) return -1;
    }
    return -1;
}

int sync_remove_tree(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0) return errno == ENOENT ? 0 : -1;
    if (!S_ISDIR(st.st_mode)) return remove(path);
    DIR *dir = opendir(path);
    if (!dir) return -1;

    int rc = 0;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        char child[SYNC_PATH_CAP];
        if (sync_join(child, sizeof(child), path, entry->d_name) != 0 ||
            sync_remove_tree(child) != 0) rc = -1;
    }
    closedir(dir);
#ifdef _WIN32
    if (_rmdir(path) != 0) rc = -1;
#else
    if (rmdir(path) != 0) rc = -1;
#endif
    return rc;
}

int sync_set_mtime(const char *path, time_t mtime) {
#ifdef _WIN32
    struct _utimbuf times = {mtime, mtime};
    return _utime(path, &times);
#else
    struct utimbuf times = {mtime, mtime};
    return utime(path, &times);
#endif
}

static int copy_file(const char *source, const char *target) {
    FILE *in = fopen(source, "rb");
    if (!in) return -1;
    FILE *out = fopen(target, "wb");
    if (!out) {
        fclose(in);
        return -1;
    }

    char buf[16384];
    size_t len;
    int rc = 0;
    while ((len = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, len, out) != len) {
            rc = -1;
            break;
        }
    }
    if (ferror(in)) rc = -1;
    fclose(in);
    if (fclose(out) != 0) rc = -1;
    return rc;
}

int sync_copy_tree(const char *source, const char *target, int depth) {
    struct stat st;
    if (depth >= SYNC_TREE_DEPTH || lstat(source, &st) != 0) return -1;
    if (S_ISREG(st.st_mode)) {
        if (copy_file(source, target) != 0) return -1;
        return sync_set_mtime(target, st.st_mtime);
    }
    if (!S_ISDIR(st.st_mode)) return -1;

    DIR *dir = opendir(source);
    if (!dir) return -1;
    int rc = AI_MKDIR(target);
    while (rc == 0) {
        errno = 0;
        struct dirent *entry = readdir(dir);
        if (!entry) {
            if (errno) rc = -1;
            break;
        }
        if (sync_skip_name(entry->d_name)) continue;
        char src[SYNC_PATH_CAP], dst[SYNC_PATH_CAP];
        if (!sync_valid_component(entry->d_name) ||
            sync_join(src, sizeof(src), source, entry->d_name) != 0 ||
            sync_join(dst, sizeof(dst), target, entry->d_name) != 0 ||
            sync_copy_tree(src, dst, depth + 1) != 0) rc = -1;
    }
    closedir(dir);
    if (rc == 0) rc = sync_set_mtime(target, st.st_mtime);
    return rc;
}

int sync_replace_tree(const char *stage, const char *replacement, const char *target) {
    char old[SYNC_PATH_CAP];
    if (sync_join(old, sizeof(old), stage, "previous") != 0) return -1;

    struct stat st;
    bool backed_up = lstat(target, &st) == 0;
    if (backed_up) {
        if (!S_ISDIR(st.st_mode) || rename(target, old) != 0) return -1;
    } else if (errno != ENOENT) {
        return -1;
    }

    if (rename(replacement, target) == 0) return 0;
    /* Keep the previous copy on disk if the rollback itself fails. */
    if (backed_up && rename(old, target) != 0) return -2;
    return -1;
}

int sync_scan_directory(const char *root, bool allow_missing, sync_remote_manifest_t *out,
                        char *error, size_t cap) {
    *out = (sync_remote_manifest_t){0};
    snprintf(out->remote_root, sizeof(out->remote_root), "%s", root);
    out->fetched_at = time(NULL);
    DIR *dir = opendir(root);
    if (!dir) {
        if (allow_missing && errno == ENOENT) return 0;
        if (error) snprintf(error, cap, "无法读取同步目录：%s", root);
        return -1;
    }

    int allocated = 0;
    while (true) {
        errno = 0;
        struct dirent *entry = readdir(dir);
        if (!entry) {
            if (errno) goto failed;
            break;
        }
        if (sync_skip_name(entry->d_name)) continue;
        char child[SYNC_PATH_CAP];
        if (sync_join(child, sizeof(child), root, entry->d_name) != 0) goto failed;
        if (!sync_is_skill(child)) continue;
        if (!sync_valid_component(entry->d_name) ||
            strlen(entry->d_name) >= sizeof(out->items[0].skill_name)) goto failed;
        if (out->count == allocated) {
            allocated = allocated ? allocated * 2 : 16;
            void *items = realloc(out->items, (size_t)allocated * sizeof(*out->items));
            if (!items) goto failed;
            out->items = items;
        }
        sync_remote_skill_item_t *item = &out->items[out->count];
        memset(item, 0, sizeof(*item));
        snprintf(item->skill_name, sizeof(item->skill_name), "%s", entry->d_name);
        if (hub_compute_skill_hash(child, item->hash, sizeof(item->hash), &item->mtime) != 0) goto failed;
        out->count++;
    }
    closedir(dir);
    return 0;

failed:
    closedir(dir);
    sync_free_manifest(out);
    if (error) snprintf(error, cap, "无法完整读取同步目录：%s", root);
    return -1;
}

static const sync_remote_skill_item_t *find_skill(const sync_remote_manifest_t *manifest, const char *name) {
    for (int i = 0; i < manifest->count; i++) {
        if (strcmp(manifest->items[i].skill_name, name) == 0) return &manifest->items[i];
    }
    return NULL;
}

static void report_difference(sync_drift_summary_t *out, const sync_remote_skill_item_t *local,
                              const sync_remote_skill_item_t *remote) {
    sync_drift_item_t *item = &out->items[out->count++];
    snprintf(item->skill_name, sizeof(item->skill_name), "%s", local ? local->skill_name : remote->skill_name);
    if (local) {
        snprintf(item->local_hash, sizeof(item->local_hash), "%s", local->hash);
        item->local_mtime = local->mtime;
    }
    if (remote) {
        snprintf(item->remote_hash, sizeof(item->remote_hash), "%s", remote->hash);
        item->remote_mtime = remote->mtime;
    }
    const char *description;
    if (!local) {
        item->status = SYNC_DRIFT_REMOTE_ONLY;
        out->remote_only_count++;
        description = "远端独有 (待拉取)";
    } else if (!remote) {
        item->status = SYNC_DRIFT_LOCAL_ONLY;
        out->local_only_count++;
        description = "本地主仓独有 (待推送)";
    } else if (strcmp(local->hash, remote->hash) == 0) {
        item->status = SYNC_DRIFT_IN_SYNC;
        out->in_sync_count++;
        description = "两端完全一致";
    } else if (local->mtime == remote->mtime) {
        item->status = SYNC_DRIFT_CONFLICT;
        out->conflict_count++;
        description = "修改时间相同，但内容不同";
    } else if (local->mtime > remote->mtime) {
        item->status = SYNC_DRIFT_LOCAL_AHEAD;
        out->local_ahead_count++;
        description = "本地有新改动 (需推送)";
    } else {
        item->status = SYNC_DRIFT_REMOTE_AHEAD;
        out->remote_ahead_count++;
        description = "远端已更新 (需拉取)";
    }
    snprintf(item->status_desc, sizeof(item->status_desc), "%s", description);
}

int sync_compare_directory(const sync_remote_manifest_t *remote, const char *local,
                           sync_drift_summary_t *out, char *error, size_t cap) {
    *out = (sync_drift_summary_t){0};
    char path[SYNC_PATH_CAP];
    if (sync_expand(local ? local : "~/.ai-space/skills", path, sizeof(path)) != 0) return -1;
    sync_remote_manifest_t local_manifest;
    if (sync_scan_directory(path, true, &local_manifest, error, cap) != 0) return -1;

    size_t count = (size_t)remote->count + (size_t)local_manifest.count;
    if (count) {
        out->items = calloc(count, sizeof(*out->items));
        if (!out->items) {
            sync_free_manifest(&local_manifest);
            if (error) snprintf(error, cap, "无法分配同步清单内存");
            return -1;
        }
    }
    for (int i = 0; i < remote->count; i++) {
        const sync_remote_skill_item_t *item = &remote->items[i];
        report_difference(out, find_skill(&local_manifest, item->skill_name), item);
    }
    for (int i = 0; i < local_manifest.count; i++) {
        const sync_remote_skill_item_t *item = &local_manifest.items[i];
        if (!find_skill(remote, item->skill_name)) report_difference(out, item, NULL);
    }
    sync_free_manifest(&local_manifest);
    time_t now = time(NULL);
    struct tm storage;
    const struct tm *tm = ai_localtime(&now, &storage);
    if (tm) strftime(out->last_checked_at, sizeof(out->last_checked_at), "%Y-%m-%d %H:%M:%S", tm);
    return 0;
}

int sync_transfer_batch(sync_backend_t *backend, const char *local, bool pull,
                        sync_progress_cb cb, void *user) {
    sync_drift_summary_t diff = {0};
    char error[256] = "";
    if (backend->ops->check_drift(backend, local, &diff, error, sizeof(error)) != 0) {
        if (cb) cb(0, error[0] ? error : "无法检查同步差异", user);
        return -1;
    }

    int total = pull ? diff.remote_only_count + diff.remote_ahead_count
                     : diff.local_only_count + diff.local_ahead_count;
    int done = 0;
    int rc = 0;
    for (int i = 0; i < diff.count; i++) {
        const sync_drift_item_t *item = &diff.items[i];
        bool candidate = pull ? item->status == SYNC_DRIFT_REMOTE_ONLY || item->status == SYNC_DRIFT_REMOTE_AHEAD
                              : item->status == SYNC_DRIFT_LOCAL_ONLY || item->status == SYNC_DRIFT_LOCAL_AHEAD;
        if (!candidate) continue;
        if (cb) {
            char msg[256];
            snprintf(msg, sizeof(msg), "%s [%s] (%d/%d)", pull ? "正在拉取" : "正在推送",
                     item->skill_name, done + 1, total);
            cb((float)done / total, msg, user);
        }
        rc = pull ? backend->ops->pull_skill(backend, item->skill_name, local, NULL, NULL)
                  : backend->ops->push_skill(backend, item->skill_name, local, NULL, NULL);
        if (rc != 0) {
            if (cb) {
                char msg[256];
                snprintf(msg, sizeof(msg), "%s [%s] 失败，已完成 %d/%d",
                         pull ? "拉取" : "推送", item->skill_name, done, total);
                cb((float)done / total, msg, user);
            }
            break;
        }
        done++;
    }
    if (rc == 0 && cb) cb(1.0f, total ? "同步完成" : "没有需要传输的技能", user);
    sync_free_drift_summary(&diff);
    return rc;
}
