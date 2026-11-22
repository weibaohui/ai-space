#include "sync_backend.h"
#include "hub_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <pwd.h>

typedef struct {
    char expanded_path[512];
} nfs_priv_t;

static void expand_path_local(const char *in, char *out, size_t cap) {
    if (!in || in[0] == '\0') {
        if (out && cap > 0) out[0] = '\0';
        return;
    }
    if (in[0] == '~' && (in[1] == '/' || in[1] == '\0')) {
        const char *h = getenv("HOME");
        if (!h || h[0] == '\0') {
            struct passwd *pw = getpwuid(getuid());
            if (pw) h = pw->pw_dir;
        }
        if (h) {
            snprintf(out, cap, "%s%s", h, in + 1);
            return;
        }
    }
    strncpy(out, in, cap - 1);
    out[cap - 1] = '\0';
}

static int nfs_init(sync_backend_t *self, const sync_config_t *cfg) {
    nfs_priv_t *priv = (nfs_priv_t *)calloc(1, sizeof(nfs_priv_t));
    if (!priv) return -1;
    expand_path_local(cfg->nfs_mount_path, priv->expanded_path, sizeof(priv->expanded_path));
    self->priv_data = priv;
    return 0;
}

static void nfs_destroy(sync_backend_t *self) {
    if (self->priv_data) {
        free(self->priv_data);
        self->priv_data = NULL;
    }
}

static int nfs_test_connection(sync_backend_t *self, char *err_buf, size_t err_cap) {
    nfs_priv_t *priv = (nfs_priv_t *)self->priv_data;
    if (!priv || priv->expanded_path[0] == '\0') {
        if (err_buf) snprintf(err_buf, err_cap, "未配置 NFS / 局域网共享目录路径");
        return -1;
    }

    struct stat st;
    if (stat(priv->expanded_path, &st) != 0) {
        /* Try creating directory if parent exists */
        if (mkdir(priv->expanded_path, 0755) != 0) {
            if (err_buf) snprintf(err_buf, err_cap, "共享目录不存在且无法自动创建: %s", priv->expanded_path);
            return -1;
        }
    } else if (!S_ISDIR(st.st_mode)) {
        if (err_buf) snprintf(err_buf, err_cap, "指定路径非有效目录: %s", priv->expanded_path);
        return -1;
    }

    if (access(priv->expanded_path, R_OK | W_OK) != 0) {
        if (err_buf) snprintf(err_buf, err_cap, "共享目录读写权限受限 (Permission Denied): %s", priv->expanded_path);
        return -1;
    }

    self->is_connected = true;
    snprintf(self->status_msg, sizeof(self->status_msg), "共享目录连接就绪: %s", priv->expanded_path);
    return 0;
}

static int nfs_fetch_remote_manifest(sync_backend_t *self, sync_remote_manifest_t *out_manifest,
                                     char *err_buf, size_t err_cap) {
    nfs_priv_t *priv = (nfs_priv_t *)self->priv_data;
    if (!priv || priv->expanded_path[0] == '\0') {
        if (err_buf) snprintf(err_buf, err_cap, "NFS 路径未初始化");
        return -1;
    }

    DIR *d = opendir(priv->expanded_path);
    if (!d) {
        if (err_buf) snprintf(err_buf, err_cap, "无法打开共享目录: %s", priv->expanded_path);
        return -1;
    }

    int cap = 32;
    out_manifest->items = (sync_remote_skill_item_t *)calloc(cap, sizeof(sync_remote_skill_item_t));
    out_manifest->count = 0;
    strncpy(out_manifest->remote_root, priv->expanded_path, sizeof(out_manifest->remote_root) - 1);
    out_manifest->fetched_at = time(NULL);

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;

        char skill_dir[1024];
        snprintf(skill_dir, sizeof(skill_dir), "%s/%s", priv->expanded_path, ent->d_name);

        struct stat st;
        if (stat(skill_dir, &st) == 0 && S_ISDIR(st.st_mode)) {
            char skill_md[1024];
            snprintf(skill_md, sizeof(skill_md), "%s/SKILL.md", skill_dir);
            struct stat md_st;
            if (stat(skill_md, &md_st) == 0) {
                if (out_manifest->count >= cap) {
                    cap *= 2;
                    out_manifest->items = (sync_remote_skill_item_t *)realloc(
                        out_manifest->items, sizeof(sync_remote_skill_item_t) * cap);
                }
                sync_remote_skill_item_t *item = &out_manifest->items[out_manifest->count];
                strncpy(item->skill_name, ent->d_name, sizeof(item->skill_name) - 1);
                item->mtime = st.st_mtime;

                time_t dir_mtime = 0;
                hub_compute_skill_hash(skill_dir, item->hash, sizeof(item->hash), &dir_mtime);
                if (dir_mtime > item->mtime) item->mtime = dir_mtime;

                out_manifest->count++;
            }
        }
    }
    closedir(d);
    return 0;
}

static int nfs_check_drift(sync_backend_t *self, const char *local_hub_dir,
                           sync_drift_summary_t *out_diff, char *err_buf, size_t err_cap) {
    sync_remote_manifest_t manifest = {0};
    if (nfs_fetch_remote_manifest(self, &manifest, err_buf, err_cap) != 0) {
        return -1;
    }

    char exp_local[512];
    expand_path_local(local_hub_dir ? local_hub_dir : "~/.ai-space/skills", exp_local, sizeof(exp_local));

    int cap = manifest.count + 32;
    out_diff->items = (sync_drift_item_t *)calloc(cap, sizeof(sync_drift_item_t));
    out_diff->count = 0;
    out_diff->in_sync_count = 0;
    out_diff->remote_ahead_count = 0;
    out_diff->local_ahead_count = 0;
    out_diff->remote_only_count = 0;
    out_diff->local_only_count = 0;
    out_diff->conflict_count = 0;

    time_t now = time(NULL);
    struct tm tm_info;
    localtime_r(&now, &tm_info);
    strftime(out_diff->last_checked_at, sizeof(out_diff->last_checked_at), "%Y-%m-%d %H:%M:%S", &tm_info);

    /* 1. Compare remote items with local */
    for (int i = 0; i < manifest.count; i++) {
        sync_remote_skill_item_t *rm = &manifest.items[i];
        sync_drift_item_t *item = &out_diff->items[out_diff->count++];
        strncpy(item->skill_name, rm->skill_name, sizeof(item->skill_name) - 1);
        strncpy(item->remote_hash, rm->hash, sizeof(item->remote_hash) - 1);
        item->remote_mtime = rm->mtime;

        char local_skill_dir[1024];
        snprintf(local_skill_dir, sizeof(local_skill_dir), "%s/%s", exp_local, rm->skill_name);

        struct stat st;
        if (stat(local_skill_dir, &st) == 0 && S_ISDIR(st.st_mode)) {
            time_t l_mtime = 0;
            hub_compute_skill_hash(local_skill_dir, item->local_hash, sizeof(item->local_hash), &l_mtime);
            item->local_mtime = l_mtime;

            if (strcmp(item->local_hash, item->remote_hash) == 0) {
                item->status = SYNC_DRIFT_IN_SYNC;
                snprintf(item->status_desc, sizeof(item->status_desc), "两端完全一致");
                out_diff->in_sync_count++;
            } else if (item->remote_mtime > item->local_mtime) {
                item->status = SYNC_DRIFT_REMOTE_AHEAD;
                snprintf(item->status_desc, sizeof(item->status_desc), "远端已更新 (需拉取)");
                out_diff->remote_ahead_count++;
            } else {
                item->status = SYNC_DRIFT_LOCAL_AHEAD;
                snprintf(item->status_desc, sizeof(item->status_desc), "本地有新改动 (需推送)");
                out_diff->local_ahead_count++;
            }
        } else {
            item->status = SYNC_DRIFT_REMOTE_ONLY;
            item->local_hash[0] = '\0';
            snprintf(item->status_desc, sizeof(item->status_desc), "远端独有 (待拉取)");
            out_diff->remote_only_count++;
        }
    }

    /* 2. Check local items missing on remote */
    DIR *ld = opendir(exp_local);
    if (ld) {
        struct dirent *ent;
        while ((ent = readdir(ld)) != NULL) {
            if (ent->d_name[0] == '.') continue;
            char ldir[1024];
            snprintf(ldir, sizeof(ldir), "%s/%s", exp_local, ent->d_name);
            struct stat lst;
            if (stat(ldir, &lst) == 0 && S_ISDIR(lst.st_mode)) {
                /* Check if already processed */
                bool found = false;
                for (int i = 0; i < out_diff->count; i++) {
                    if (strcmp(out_diff->items[i].skill_name, ent->d_name) == 0) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    if (out_diff->count >= cap) {
                        cap *= 2;
                        out_diff->items = (sync_drift_item_t *)realloc(
                            out_diff->items, sizeof(sync_drift_item_t) * cap);
                    }
                    sync_drift_item_t *item = &out_diff->items[out_diff->count++];
                    strncpy(item->skill_name, ent->d_name, sizeof(item->skill_name) - 1);
                    item->status = SYNC_DRIFT_LOCAL_ONLY;
                    time_t lm = 0;
                    hub_compute_skill_hash(ldir, item->local_hash, sizeof(item->local_hash), &lm);
                    item->local_mtime = lm;
                    item->remote_hash[0] = '\0';
                    snprintf(item->status_desc, sizeof(item->status_desc), "本地主仓独有 (待推送)");
                    out_diff->local_only_count++;
                }
            }
        }
        closedir(ld);
    }

    sync_free_manifest(&manifest);
    return 0;
}

static int nfs_pull_skill(sync_backend_t *self, const char *skill_name, const char *target_local_hub,
                          sync_progress_cb cb, void *user_data) {
    nfs_priv_t *priv = (nfs_priv_t *)self->priv_data;
    if (!priv || !skill_name) return -1;

    char exp_local[512];
    expand_path_local(target_local_hub ? target_local_hub : "~/.ai-space/skills", exp_local, sizeof(exp_local));
    mkdir(exp_local, 0755);

    char src_dir[1024];
    snprintf(src_dir, sizeof(src_dir), "%s/%s", priv->expanded_path, skill_name);

    char dst_dir[1024];
    snprintf(dst_dir, sizeof(dst_dir), "%s/%s", exp_local, skill_name);

    if (cb) cb(0.3f, "正在从远端共享拉取目录文件...", user_data);
    int rc = hub_copy_dir_recursive(src_dir, dst_dir);
    if (cb) cb(1.0f, "拉取完成", user_data);
    return rc;
}

static int nfs_push_skill(sync_backend_t *self, const char *skill_name, const char *source_local_hub,
                          sync_progress_cb cb, void *user_data) {
    nfs_priv_t *priv = (nfs_priv_t *)self->priv_data;
    if (!priv || !skill_name) return -1;

    char exp_local[512];
    expand_path_local(source_local_hub ? source_local_hub : "~/.ai-space/skills", exp_local, sizeof(exp_local));

    char src_dir[1024];
    snprintf(src_dir, sizeof(src_dir), "%s/%s", exp_local, skill_name);

    char dst_dir[1024];
    snprintf(dst_dir, sizeof(dst_dir), "%s/%s", priv->expanded_path, skill_name);

    if (cb) cb(0.3f, "正在物理推送到远端共享目录...", user_data);
    int rc = hub_copy_dir_recursive(src_dir, dst_dir);
    if (cb) cb(1.0f, "推送完成", user_data);
    return rc;
}

static int nfs_pull_all(sync_backend_t *self, const char *target_local_hub,
                        sync_progress_cb cb, void *user_data) {
    sync_drift_summary_t diff = {0};
    if (nfs_check_drift(self, target_local_hub, &diff, NULL, 0) != 0) {
        return -1;
    }

    int total = diff.remote_ahead_count + diff.remote_only_count;
    if (total == 0) {
        if (cb) cb(1.0f, "本地已与远端完全一致，无需拉取", user_data);
        sync_free_drift_summary(&diff);
        return 0;
    }

    int current = 0;
    for (int i = 0; i < diff.count; i++) {
        if (diff.items[i].status == SYNC_DRIFT_REMOTE_AHEAD ||
            diff.items[i].status == SYNC_DRIFT_REMOTE_ONLY) {
            current++;
            char msg[256];
            snprintf(msg, sizeof(msg), "正在拉取技能 [%s] (%d/%d)...",
                     diff.items[i].skill_name, current, total);
            float pct = (float)current / (float)total;
            if (cb) cb(pct, msg, user_data);
            nfs_pull_skill(self, diff.items[i].skill_name, target_local_hub, NULL, NULL);
        }
    }
    if (cb) cb(1.0f, "所有远端技能已成功拉取至本地主仓！", user_data);
    sync_free_drift_summary(&diff);
    return 0;
}

static int nfs_push_all(sync_backend_t *self, const char *source_local_hub,
                        sync_progress_cb cb, void *user_data) {
    sync_drift_summary_t diff = {0};
    if (nfs_check_drift(self, source_local_hub, &diff, NULL, 0) != 0) {
        return -1;
    }

    int total = diff.local_ahead_count + diff.local_only_count;
    if (total == 0) {
        if (cb) cb(1.0f, "本地没有需推送的新增或改动技能", user_data);
        sync_free_drift_summary(&diff);
        return 0;
    }

    int current = 0;
    for (int i = 0; i < diff.count; i++) {
        if (diff.items[i].status == SYNC_DRIFT_LOCAL_AHEAD ||
            diff.items[i].status == SYNC_DRIFT_LOCAL_ONLY) {
            current++;
            char msg[256];
            snprintf(msg, sizeof(msg), "正在推送技能 [%s] 到远端共享 (%d/%d)...",
                     diff.items[i].skill_name, current, total);
            float pct = (float)current / (float)total;
            if (cb) cb(pct, msg, user_data);
            nfs_push_skill(self, diff.items[i].skill_name, source_local_hub, NULL, NULL);
        }
    }
    if (cb) cb(1.0f, "所有本地技能已成功推送至远端共享！", user_data);
    sync_free_drift_summary(&diff);
    return 0;
}

const sync_driver_ops_t g_nfs_driver_ops = {
    .type = SYNC_BACKEND_NFS,
    .name = "nfs",
    .display_name = "NFS / 局域网共享目录",
    .init = nfs_init,
    .destroy = nfs_destroy,
    .test_connection = nfs_test_connection,
    .fetch_remote_manifest = nfs_fetch_remote_manifest,
    .check_drift = nfs_check_drift,
    .pull_skill = nfs_pull_skill,
    .push_skill = nfs_push_skill,
    .pull_all = nfs_pull_all,
    .push_all = nfs_push_all,
};
