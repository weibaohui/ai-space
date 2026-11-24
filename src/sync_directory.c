#include "skill_fingerprint.h"
#include "skill_metadata.h"
#include "sync_directory.h"
#include "portability.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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
        if (skill_skip_name(entry->d_name)) continue;
        char child[SYNC_PATH_CAP];
        if (sync_join(child, sizeof(child), root, entry->d_name) != 0) goto failed;
        struct stat st;
        if (lstat(child, &st) != 0) {
            if (errno == ENOENT) continue;
            goto failed;
        }
        if (!S_ISDIR(st.st_mode)) continue;
        char document[SYNC_PATH_CAP + sizeof(SKILL_DOCUMENT)];
        if (sync_join(document, sizeof(document), child, SKILL_DOCUMENT) != 0) goto failed;
        if (lstat(document, &st) != 0) {
            if (errno == ENOENT) continue;
            goto failed;
        }
        if (!S_ISREG(st.st_mode)) continue;
        if (!sync_valid_component(entry->d_name) ||
            strlen(entry->d_name) >= sizeof(out->items[0].skill_name)) goto failed;
        if (out->count == allocated) {
            if (allocated > INT_MAX / 2) goto failed;
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

typedef struct {
    const char *name;
    int position;
} skill_index_entry_t;

static int compare_skill_names(const void *lhs, const void *rhs) {
    const skill_index_entry_t *a = lhs;
    const skill_index_entry_t *b = rhs;
    return strcmp(a->name, b->name);
}

static int compare_baseline_name(const void *name, const void *item) {
    return strcmp(name, ((const sync_baseline_item_t *)item)->skill);
}

static void report_difference(sync_drift_summary_t *out, const sync_remote_skill_item_t *local,
                              const sync_remote_skill_item_t *remote, const sync_baseline_t *baseline) {
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
    const sync_baseline_item_t *previous = baseline && baseline->count
        ? bsearch(item->skill_name, baseline->items, baseline->count,
                  sizeof(*baseline->items), compare_baseline_name) : NULL;
    bool local_unchanged = previous && local && strcmp(local->hash, previous->hash) == 0;
    bool remote_unchanged = previous && remote && strcmp(remote->hash, previous->hash) == 0;

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
    } else if (baseline ? local_unchanged == remote_unchanged : local->mtime == remote->mtime) {
        item->status = SYNC_DRIFT_CONFLICT;
        out->conflict_count++;
        description = baseline
            ? (previous ? "两端均有改动，请选择保留的版本" : "两端内容不同，首次同步需选择版本")
            : "修改时间相同，但内容不同";
    } else if (baseline ? remote_unchanged : local->mtime > remote->mtime) {
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
                           const sync_baseline_t *baseline,
                           sync_drift_summary_t *out, char *error, size_t cap) {
    *out = (sync_drift_summary_t){0};
    char path[SYNC_PATH_CAP];
    if (sync_expand(local ? local : "~/.ai-space/skills", path, sizeof(path)) != 0) return -1;
    sync_remote_manifest_t local_manifest;
    if (sync_scan_directory(path, true, &local_manifest, error, cap) != 0) return -1;

    size_t local_count = (size_t)local_manifest.count;
    size_t count = (size_t)remote->count + local_count;
    bool index_needed = local_count > 0 && remote->count > 0;
    skill_index_entry_t *index = index_needed ? calloc(local_count, sizeof(*index)) : NULL;
    bool *matched = index_needed ? calloc(local_count, sizeof(*matched)) : NULL;
    out->items = count ? calloc(count, sizeof(*out->items)) : NULL;
    if ((count && !out->items) || (index_needed && (!index || !matched))) {
        free(index);
        free(matched);
        sync_free_drift_summary(out);
        sync_free_manifest(&local_manifest);
        if (error) snprintf(error, cap, "无法分配同步清单内存");
        return -1;
    }
    if (index) {
        for (int i = 0; i < local_manifest.count; i++) {
            index[i] = (skill_index_entry_t){local_manifest.items[i].skill_name, i};
        }
        qsort(index, local_count, sizeof(*index), compare_skill_names);
    }
    for (int i = 0; i < remote->count; i++) {
        const sync_remote_skill_item_t *item = &remote->items[i];
        skill_index_entry_t key = {.name = item->skill_name};
        const skill_index_entry_t *match = index
            ? bsearch(&key, index, local_count, sizeof(*index), compare_skill_names) : NULL;
        if (match) matched[match->position] = true;
        report_difference(out, match ? &local_manifest.items[match->position] : NULL, item, baseline);
    }
    for (int i = 0; i < local_manifest.count; i++) {
        if (!matched || !matched[i]) report_difference(out, &local_manifest.items[i], NULL, baseline);
    }
    free(index);
    free(matched);
    sync_free_manifest(&local_manifest);
    time_t now = time(NULL);
    struct tm storage;
    const struct tm *tm = ai_localtime(&now, &storage);
    if (tm) strftime(out->last_checked_at, sizeof(out->last_checked_at), "%Y-%m-%d %H:%M:%S", tm);
    return 0;
}

bool sync_drift_is_candidate(sync_drift_status_t status, bool pull) {
    return pull ? status == SYNC_DRIFT_REMOTE_ONLY || status == SYNC_DRIFT_REMOTE_AHEAD
                : status == SYNC_DRIFT_LOCAL_ONLY || status == SYNC_DRIFT_LOCAL_AHEAD;
}

bool sync_skill_matches(const char *path, const char *expected_hash) {
    struct stat st;
    if (!expected_hash[0]) return lstat(path, &st) != 0 && errno == ENOENT;
    char hash[32];
    return skill_is_directory(path) && hub_compute_skill_hash(path, hash, sizeof(hash), NULL) == 0 &&
           strcmp(hash, expected_hash) == 0;
}

int sync_transfer_batch(sync_backend_t *backend, const char *local, bool pull,
                        sync_progress_cb cb, void *user, sync_drift_summary_t *unchanged_drift) {
    sync_drift_summary_t diff = {0};
    char error[256] = "";
    if (backend->ops->check_drift(backend, local, &diff, error, sizeof(error)) != 0) {
        if (cb) cb(0, error[0] ? error : "无法检查同步差异", user);
        return -1;
    }

    int total = pull ? diff.remote_only_count + diff.remote_ahead_count
                     : diff.local_only_count + diff.local_ahead_count;
    if (!total) {
        if (cb) cb(1.0f, "没有需要传输的技能", user);
        if (unchanged_drift) *unchanged_drift = diff;
        else sync_free_drift_summary(&diff);
        return SYNC_BATCH_UNCHANGED;
    }
    int done = 0;
    int rc = 0;
    for (int i = 0; i < diff.count; i++) {
        const sync_drift_item_t *item = &diff.items[i];
        if (!sync_drift_is_candidate(item->status, pull)) continue;
        if (cb) {
            char msg[256];
            snprintf(msg, sizeof(msg), "%s [%s] (%d/%d)", pull ? "正在拉取" : "正在推送",
                     item->skill_name, done + 1, total);
            cb((float)done / total, msg, user);
        }
        backend->expected_skill = item;
        rc = pull ? backend->ops->pull_skill(backend, item->skill_name, local, NULL, NULL)
                  : backend->ops->push_skill(backend, item->skill_name, local, NULL, NULL);
        backend->expected_skill = NULL;
        if (rc != 0) {
            if (cb) {
                char msg[256];
                snprintf(msg, sizeof(msg), "%s [%s] %s，已完成 %d/%d",
                         pull ? "拉取" : "推送", item->skill_name,
                         rc == SYNC_TRANSFER_CHANGED ? "时发现技能状态已变化" : "失败", done, total);
                cb((float)done / total, msg, user);
            }
            break;
        }
        done++;
    }
    if (rc == 0 && cb) cb(1.0f, "同步完成", user);
    sync_free_drift_summary(&diff);
    return rc;
}
