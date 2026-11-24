#include "sync_git_repository.h"
#include "skill_fingerprint.h"
#include "skill_metadata.h"
#include "sync_backend.h"
#include "sync_directory.h"
#include "sync_local_files.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>
#include "portability.h"

static int git_init(sync_backend_t *self, const sync_config_t *cfg) {
    git_priv_t *priv = calloc(1, sizeof(*priv));
    if (!priv) return -1;
    if (git_repo_configure(priv, cfg) != 0) {
        free(priv);
        return -1;
    }
    self->priv_data = priv;
    return 0;
}

static void git_destroy(sync_backend_t *self) {
    if (self->priv_data) {
        git_repo_release(self->priv_data);
        free(self->priv_data);
        self->priv_data = NULL;
    }
}

static int git_check_available(sync_backend_t *self, char *error, size_t cap) {
    const git_priv_t *repo = self->priv_data;
    int rc = git_repo_check_available(repo, error, cap);
    if (rc == 0) {
        self->is_connected = true;
        snprintf(self->status_msg, sizeof(self->status_msg), "Git 远端仓库就绪: %s [%s]",
                 repo->repo_url, repo->branch);
    }
    return rc;
}

static int git_test_connection(sync_backend_t *self, char *err_buf, size_t err_cap) {
    git_priv_t *priv = (git_priv_t *)self->priv_data;
    if (!priv || priv->repo_url[0] == '\0') {
        if (err_buf) snprintf(err_buf, err_cap, "未配置 Git 远端仓库地址");
        return -1;
    }
    self->is_connected = true;
    return 0;
}

static int git_read_manifest(const git_priv_t *repo, int revision, sync_remote_manifest_t *out,
                             char *error, size_t cap) {
    *out = (sync_remote_manifest_t){0};
    /* 空仓可以首次推送，读取清单时直接返回零项。 */
    if (revision != GIT_REPO_EMPTY &&
        sync_scan_directory(repo->git_cache_dir, false, out, error, cap) != 0) return -1;
    snprintf(out->remote_root, sizeof(out->remote_root), "%s", repo->repo_url);
    out->fetched_at = time(NULL);
    return 0;
}

static int git_fetch_remote_manifest(sync_backend_t *self, sync_remote_manifest_t *out,
                                     char *error, size_t cap) {
    git_priv_t *repo = self->priv_data;
    int revision = git_repo_refresh(repo, error, cap);
    if (revision < 0) return -1;
    return git_read_manifest(repo, revision, out, error, cap);
}

static void git_remove_new_skill(const git_priv_t *repo, const char *name) {
    char path[SYNC_PATH_CAP];
    if (sync_join(path, sizeof(path), repo->git_cache_dir, name) == 0) sync_remove_tree(path);
}

static int git_transfer_skill(sync_backend_t *self, const char *name, const char *local, bool pull,
                              sync_progress_cb cb, void *user) {
    git_priv_t *repo = self->priv_data;
    char root[SYNC_PATH_CAP], error[256] = "";
    if (!sync_valid_component(name) || skill_skip_name(name) ||
        sync_expand(local ? local : "~/.ai-space/skills", root, sizeof(root)) != 0) return -1;
    if (cb) cb(0.1f, "正在获取 Git 远端分支", user);
    int revision = git_repo_refresh(repo, error, sizeof(error));
    if (revision < 0 || (pull && revision == GIT_REPO_EMPTY)) {
        if (cb) cb(0.1f, revision < 0 ? error : "Git 远端仓库为空", user);
        return -1;
    }

    if (cb) cb(0.4f, pull ? "正在拉取 Git 技能" : "正在准备 Git 技能提交", user);
    char target[SYNC_PATH_CAP];
    struct stat existing;
    if (sync_join(target, sizeof(target), repo->git_cache_dir, name) != 0) return -1;
    bool new_skill = lstat(target, &existing) != 0 && errno == ENOENT;
    int rc = sync_copy_local_skill(root, repo->git_cache_dir, name, self->expected_skill, pull);
    if (rc == 0 && !pull) {
        rc = git_repo_stage(repo, name);
        if (rc == 0) rc = git_repo_commit(repo, name, 0);
        if (rc == 0) rc = git_repo_push(repo);
        else {
            git_repo_restore(repo, revision);
            if (new_skill) git_remove_new_skill(repo, name);
        }
    }
    const char *message = rc == 0 ? "Git 同步完成" :
        rc == SYNC_TRANSFER_CHANGED ? "技能状态已变化，请检查后重试" : "Git 同步失败";
    if (cb) cb(rc == 0 ? 1.0f : 0.4f, message, user);
    return rc;
}

static int git_pull_skill(sync_backend_t *self, const char *name, const char *local,
                          sync_progress_cb cb, void *user) {
    return git_transfer_skill(self, name, local, true, cb, user);
}

static int git_push_skill(sync_backend_t *self, const char *name, const char *local,
                          sync_progress_cb cb, void *user) {
    return git_transfer_skill(self, name, local, false, cb, user);
}

static int git_transfer_batch(sync_backend_t *self, const char *local, bool pull,
                              sync_progress_cb cb, void *user, sync_drift_summary_t *unchanged) {
    git_priv_t *repo = self->priv_data;
    char root[SYNC_PATH_CAP], error[256] = "";
    if (sync_expand(local ? local : "~/.ai-space/skills", root, sizeof(root)) != 0) return -1;
    if (cb) cb(0.1f, "正在获取 Git 远端分支", user);
    int revision = git_repo_refresh(repo, error, sizeof(error));
    sync_remote_manifest_t remote = {0};
    sync_drift_summary_t drift = {0};
    if (revision < 0 || git_read_manifest(repo, revision, &remote, error, sizeof(error)) != 0 ||
        sync_compare_directory(&remote, root, self->baseline, &drift, error, sizeof(error)) != 0) {
        if (cb) cb(0.1f, error[0] ? error : "无法检查 Git 同步差异", user);
        sync_free_manifest(&remote);
        return -1;
    }
    sync_free_manifest(&remote);

    int total = pull ? drift.remote_only_count + drift.remote_ahead_count
                     : drift.local_only_count + drift.local_ahead_count;
    if (!total) {
        if (cb) cb(1.0f, "没有需要传输的技能", user);
        if (unchanged) *unchanged = drift;
        else sync_free_drift_summary(&drift);
        return SYNC_BATCH_UNCHANGED;
    }
    /* 批量推送先准备所有候选项，最后合为一次 Git 提交。 */
    int done = 0, rc = 0, last_copied = -1;
    for (int i = 0; i < drift.count; i++) {
        const sync_drift_item_t *item = &drift.items[i];
        if (!sync_drift_is_candidate(item->status, pull)) continue;
        const char *name = item->skill_name;
        rc = sync_copy_local_skill(root, repo->git_cache_dir, name, item, pull);
        if (rc == 0 && !pull) {
            last_copied = i;
            rc = git_repo_stage(repo, name);
        }
        if (rc != 0) break;
        done++;
        if (cb) {
            char message[192];
            snprintf(message, sizeof(message), "已准备 Git 技能 [%s]（%d/%d）", name, done, total);
            cb(0.2f + 0.6f * done / total, message, user);
        }
    }
    if (!pull && last_copied >= 0) {
        if (rc == 0) rc = git_repo_commit(repo, NULL, done);
        if (rc == 0) rc = git_repo_push(repo);
        else {
            git_repo_restore(repo, revision);
            for (int i = 0; i <= last_copied; i++) {
                if (drift.items[i].status == SYNC_DRIFT_LOCAL_ONLY)
                    git_remove_new_skill(repo, drift.items[i].skill_name);
            }
        }
    }
    if (cb) cb(rc == 0 ? 1.0f : 0.8f, rc == 0 ? "Git 同步完成" : "Git 批量同步未完成", user);
    sync_free_drift_summary(&drift);
    return rc;
}

const sync_driver_ops_t g_git_driver_ops = {
    .type = SYNC_BACKEND_GIT,
    .name = "git",
    .display_name = "Git 远端版本库",
    .init = git_init,
    .destroy = git_destroy,
    .test_connection = git_test_connection,
    .check_available = git_check_available,
    .fetch_remote_manifest = git_fetch_remote_manifest,
    .check_drift = sync_check_directory_drift,
    .pull_skill = git_pull_skill,
    .push_skill = git_push_skill,
    .pull_all = sync_batch_pull_all,
    .push_all = sync_batch_push_all,
    .transfer_batch = git_transfer_batch,
};
