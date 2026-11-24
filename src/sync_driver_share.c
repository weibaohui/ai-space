#include "skill_metadata.h"
#include "sync_directory.h"
#include "sync_network_share.h"
#include "portability.h"

#include <string.h>
#include <sys/stat.h>

typedef struct {
    char path[SYNC_PATH_CAP];
} shared_dir_t;

static int shared_init(sync_backend_t *self, const sync_config_t *cfg) {
    shared_dir_t *priv = calloc(1, sizeof(*priv));
    if (!priv) return -1;
    if (cfg->share_mode == SYNC_SHARE_MOUNTED && cfg->nfs_mount_path[0] &&
        sync_expand(cfg->nfs_mount_path, priv->path, sizeof(priv->path)) != 0) {
        free(priv);
        return -1;
    }
    self->priv_data = priv;
    return 0;
}

static void shared_destroy(sync_backend_t *self) {
    free(self->priv_data);
    self->priv_data = NULL;
}

static int shared_check_available(sync_backend_t *self, char *error, size_t cap) {
    if (self->cfg.share_mode != SYNC_SHARE_MOUNTED) {
        int rc = sync_network_check(&self->cfg, error, cap);
        self->is_connected = rc == 0;
        return rc;
    }
    shared_dir_t *priv = self->priv_data;
    self->is_connected = false;
    char stage[SYNC_PATH_CAP], probe[SYNC_PATH_CAP];
    if (!priv->path[0] || sync_make_stage(priv->path, stage, sizeof(stage)) != 0) {
        if (error) snprintf(error, cap, "请选择已挂载且可写的共享目录：%.180s", priv->path);
        return -1;
    }

    int rc = -1;
    if (sync_join(probe, sizeof(probe), stage, "probe") == 0) {
        const char *value = "ai-space";
        FILE *fp = fopen(probe, "wb");
        if (fp) {
            bool written = fputs(value, fp) >= 0;
            bool closed = fclose(fp) == 0;
            if (written && closed) {
                fp = fopen(probe, "rb");
                if (fp) {
                    char buf[32];
                    if (fgets(buf, sizeof(buf), fp) && strcmp(buf, value) == 0) rc = 0;
                    fclose(fp);
                }
            }
        }
    }
    if (sync_remove_tree(stage) != 0) rc = -1;
    self->is_connected = rc == 0;
    if (error) snprintf(error, cap, "%s：%.180s", rc == 0 ? "已验证共享目录读写" : "共享目录读写测试失败", priv->path);
    return rc;
}

static int shared_fetch(sync_backend_t *self, sync_remote_manifest_t *manifest, char *error, size_t cap) {
    if (self->cfg.share_mode != SYNC_SHARE_MOUNTED)
        return sync_network_fetch(&self->cfg, manifest, error, cap);
    shared_dir_t *priv = self->priv_data;
    if (!priv->path[0]) {
        if (error) snprintf(error, cap, "未配置共享目录");
        return -1;
    }
    return sync_scan_directory(priv->path, false, manifest, error, cap);
}

static int shared_drift(sync_backend_t *self, const char *local, sync_drift_summary_t *out,
                        char *error, size_t cap) {
    sync_remote_manifest_t remote = {0};
    if (shared_fetch(self, &remote, error, cap) != 0) return -1;
    int rc = sync_compare_directory(&remote, local, self->baseline, out, error, cap);
    sync_free_manifest(&remote);
    return rc;
}

static int shared_transfer(sync_backend_t *self, const char *name, const char *local,
                           bool pull, sync_progress_cb cb, void *user) {
    if (self->cfg.share_mode != SYNC_SHARE_MOUNTED)
        return sync_network_transfer(&self->cfg, name, local, pull, self->expected_skill, cb, user);
    shared_dir_t *priv = self->priv_data;
    if (!priv->path[0] || !sync_valid_component(name) || skill_skip_name(name)) return -1;

    char local_root[SYNC_PATH_CAP];
    if (sync_expand(local ? local : "~/.ai-space/skills", local_root, sizeof(local_root)) != 0) return -1;
    const char *src_root = pull ? priv->path : local_root;
    const char *dst_root = pull ? local_root : priv->path;
    char src[SYNC_PATH_CAP], dst[SYNC_PATH_CAP], stage[SYNC_PATH_CAP], replacement[SYNC_PATH_CAP];
    if (sync_join(src, sizeof(src), src_root, name) != 0 || !skill_is_directory(src) ||
        sync_join(dst, sizeof(dst), dst_root, name) != 0) return -1;

    if (pull) ai_mkdir_p(local_root);
    if (sync_make_stage(dst_root, stage, sizeof(stage)) != 0) return -1;

    if (cb) cb(0.2f, pull ? "正在读取共享目录" : "正在写入共享目录", user);
    int rc = -1;
    const sync_drift_item_t *expected = self->expected_skill;
    if (sync_join(replacement, sizeof(replacement), stage, "new") == 0 &&
        file_tree_copy(src, replacement, false, skill_skip_name) == 0) {
        if (!expected || (sync_skill_matches(replacement, pull ? expected->remote_hash : expected->local_hash) &&
                          sync_skill_matches(dst, pull ? expected->local_hash : expected->remote_hash)))
            rc = sync_replace_tree(stage, replacement, dst);
        else rc = SYNC_TRANSFER_CHANGED;
    }
    if (rc != -2) sync_remove_tree(stage);
    if (cb) cb(rc == 0 ? 1.0f : 0.2f, rc == 0 ? "共享目录同步完成" : rc == SYNC_TRANSFER_CHANGED ? "技能状态已变化，请检查后重试" : "共享目录传输失败", user);
    return rc;
}

static int shared_pull(sync_backend_t *self, const char *name, const char *local,
                       sync_progress_cb cb, void *user) {
    return shared_transfer(self, name, local, true, cb, user);
}

static int shared_push(sync_backend_t *self, const char *name, const char *local,
                       sync_progress_cb cb, void *user) {
    return shared_transfer(self, name, local, false, cb, user);
}

static int shared_pull_all(sync_backend_t *self, const char *local, sync_progress_cb cb, void *user) {
    int rc = sync_transfer_batch(self, local, true, cb, user, NULL);
    return rc == SYNC_BATCH_UNCHANGED ? 0 : rc;
}

static int shared_push_all(sync_backend_t *self, const char *local, sync_progress_cb cb, void *user) {
    int rc = sync_transfer_batch(self, local, false, cb, user, NULL);
    return rc == SYNC_BATCH_UNCHANGED ? 0 : rc;
}

const sync_driver_ops_t g_share_driver_ops = {
    .type = SYNC_BACKEND_NFS,
    .name = "nfs",
    .display_name = "SMB / NFS 共享目录",
    .init = shared_init,
    .destroy = shared_destroy,
    .test_connection = shared_check_available,
    .check_available = shared_check_available,
    .fetch_remote_manifest = shared_fetch,
    .check_drift = shared_drift,
    .pull_skill = shared_pull,
    .push_skill = shared_push,
    .pull_all = shared_pull_all,
    .push_all = shared_push_all,
    .transfer_batch = sync_transfer_batch,
};
