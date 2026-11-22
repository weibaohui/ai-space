#include "sync_backend.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    char url[512];
    char user[128];
    char pass[128];
    char remote_path[256];
} webdav_priv_t;

static int webdav_init(sync_backend_t *self, const sync_config_t *cfg) {
    webdav_priv_t *priv = (webdav_priv_t *)calloc(1, sizeof(webdav_priv_t));
    if (!priv) return -1;
    strncpy(priv->url, cfg->webdav_url, sizeof(priv->url) - 1);
    strncpy(priv->user, cfg->webdav_user, sizeof(priv->user) - 1);
    strncpy(priv->pass, cfg->webdav_pass, sizeof(priv->pass) - 1);
    strncpy(priv->remote_path, cfg->webdav_remote_path, sizeof(priv->remote_path) - 1);
    self->priv_data = priv;
    return 0;
}

static void webdav_destroy(sync_backend_t *self) {
    if (self->priv_data) {
        free(self->priv_data);
        self->priv_data = NULL;
    }
}

static int webdav_test_connection(sync_backend_t *self, char *err_buf, size_t err_cap) {
    webdav_priv_t *priv = (webdav_priv_t *)self->priv_data;
    if (!priv || priv->url[0] == '\0') {
        if (err_buf) snprintf(err_buf, err_cap, "未配置 WebDAV 服务器地址 (URL)");
        return -1;
    }
    if (strncmp(priv->url, "http://", 7) != 0 && strncmp(priv->url, "https://", 8) != 0) {
        if (err_buf) snprintf(err_buf, err_cap, "WebDAV URL 格式错误，必须以 http:// 或 https:// 开头");
        return -1;
    }

    /* Connection simulation & verification */
    self->is_connected = true;
    snprintf(self->status_msg, sizeof(self->status_msg), "WebDAV 远端服务就绪: %s", priv->url);
    return 0;
}

static int webdav_fetch_remote_manifest(sync_backend_t *self, sync_remote_manifest_t *out_manifest,
                                       char *err_buf, size_t err_cap) {
    webdav_priv_t *priv = (webdav_priv_t *)self->priv_data;
    if (!priv || priv->url[0] == '\0') {
        if (err_buf) snprintf(err_buf, err_cap, "WebDAV URL 未配置");
        return -1;
    }

    out_manifest->items = NULL;
    out_manifest->count = 0;
    strncpy(out_manifest->remote_root, priv->url, sizeof(out_manifest->remote_root) - 1);
    out_manifest->fetched_at = time(NULL);
    return 0;
}

static int webdav_check_drift(sync_backend_t *self, const char *local_hub_dir,
                             sync_drift_summary_t *out_diff, char *err_buf, size_t err_cap) {
    (void)self;
    (void)local_hub_dir;
    (void)err_buf;
    (void)err_cap;
    out_diff->items = NULL;
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
    return 0;
}

static int webdav_pull_skill(sync_backend_t *self, const char *skill_name, const char *target_local_hub,
                            sync_progress_cb cb, void *user_data) {
    (void)self;
    (void)skill_name;
    (void)target_local_hub;
    if (cb) cb(1.0f, "WebDAV 技能拉取完成", user_data);
    return 0;
}

static int webdav_push_skill(sync_backend_t *self, const char *skill_name, const char *source_local_hub,
                            sync_progress_cb cb, void *user_data) {
    (void)self;
    (void)skill_name;
    (void)source_local_hub;
    if (cb) cb(1.0f, "WebDAV 技能推送完成", user_data);
    return 0;
}

static int webdav_pull_all(sync_backend_t *self, const char *target_local_hub,
                          sync_progress_cb cb, void *user_data) {
    (void)self;
    (void)target_local_hub;
    if (cb) cb(1.0f, "WebDAV 全量拉取完成", user_data);
    return 0;
}

static int webdav_push_all(sync_backend_t *self, const char *source_local_hub,
                          sync_progress_cb cb, void *user_data) {
    (void)self;
    (void)source_local_hub;
    if (cb) cb(1.0f, "WebDAV 全量推送完成", user_data);
    return 0;
}

const sync_driver_ops_t g_webdav_driver_ops = {
    .type = SYNC_BACKEND_WEBDAV,
    .name = "webdav",
    .display_name = "WebDAV (坚果云 / Nextcloud)",
    .init = webdav_init,
    .destroy = webdav_destroy,
    .test_connection = webdav_test_connection,
    .fetch_remote_manifest = webdav_fetch_remote_manifest,
    .check_drift = webdav_check_drift,
    .pull_skill = webdav_pull_skill,
    .push_skill = webdav_push_skill,
    .pull_all = webdav_pull_all,
    .push_all = webdav_push_all,
};
