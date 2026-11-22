#include "sync_backend.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const sync_driver_ops_t g_nfs_driver_ops;
extern const sync_driver_ops_t g_webdav_driver_ops;
extern const sync_driver_ops_t g_git_driver_ops;

static int none_init(sync_backend_t *self, const sync_config_t *cfg) {
    (void)self;
    (void)cfg;
    return 0;
}

static void none_destroy(sync_backend_t *self) {
    (void)self;
}

static int none_test(sync_backend_t *self, char *err_buf, size_t err_cap) {
    (void)err_buf;
    (void)err_cap;
    self->is_connected = false;
    snprintf(self->status_msg, sizeof(self->status_msg), "当前为单机本地模式 (未启用远端同步)");
    return 0;
}

static int none_fetch(sync_backend_t *self, sync_remote_manifest_t *out_manifest, char *err_buf, size_t err_cap) {
    (void)self;
    (void)err_buf;
    (void)err_cap;
    out_manifest->items = NULL;
    out_manifest->count = 0;
    return 0;
}

static int none_drift(sync_backend_t *self, const char *local_hub_dir, sync_drift_summary_t *out_diff, char *err_buf, size_t err_cap) {
    (void)self;
    (void)local_hub_dir;
    (void)err_buf;
    (void)err_cap;
    out_diff->items = NULL;
    out_diff->count = 0;
    return 0;
}

static int none_pull(sync_backend_t *self, const char *skill_name, const char *target_local_hub, sync_progress_cb cb, void *user_data) {
    (void)self; (void)skill_name; (void)target_local_hub; (void)cb; (void)user_data;
    return 0;
}

static int none_push(sync_backend_t *self, const char *skill_name, const char *source_local_hub, sync_progress_cb cb, void *user_data) {
    (void)self; (void)skill_name; (void)source_local_hub; (void)cb; (void)user_data;
    return 0;
}

static int none_pull_all(sync_backend_t *self, const char *target_local_hub, sync_progress_cb cb, void *user_data) {
    (void)self; (void)target_local_hub; (void)cb; (void)user_data;
    return 0;
}

static int none_push_all(sync_backend_t *self, const char *source_local_hub, sync_progress_cb cb, void *user_data) {
    (void)self; (void)source_local_hub; (void)cb; (void)user_data;
    return 0;
}

static const sync_driver_ops_t g_none_driver_ops = {
    .type = SYNC_BACKEND_NONE,
    .name = "none",
    .display_name = "单机本地模式 (未启用远端同步)",
    .init = none_init,
    .destroy = none_destroy,
    .test_connection = none_test,
    .fetch_remote_manifest = none_fetch,
    .check_drift = none_drift,
    .pull_skill = none_pull,
    .push_skill = none_push,
    .pull_all = none_pull_all,
    .push_all = none_push_all,
};

int sync_backend_create(const sync_config_t *cfg, sync_backend_t **out_backend) {
    if (!out_backend) return -1;
    *out_backend = NULL;

    sync_backend_t *backend = (sync_backend_t *)calloc(1, sizeof(sync_backend_t));
    if (!backend) return -1;

    if (cfg) {
        backend->cfg = *cfg;
    } else {
        backend->cfg.type = SYNC_BACKEND_NONE;
    }

    switch (backend->cfg.type) {
        case SYNC_BACKEND_WEBDAV:
            backend->ops = &g_webdav_driver_ops;
            break;
        case SYNC_BACKEND_NFS:
            backend->ops = &g_nfs_driver_ops;
            break;
        case SYNC_BACKEND_GIT:
            backend->ops = &g_git_driver_ops;
            break;
        case SYNC_BACKEND_NONE:
        default:
            backend->ops = &g_none_driver_ops;
            break;
    }

    if (backend->ops->init) {
        if (backend->ops->init(backend, &backend->cfg) != 0) {
            free(backend);
            return -1;
        }
    }

    *out_backend = backend;
    return 0;
}

void sync_backend_destroy(sync_backend_t *backend) {
    if (!backend) return;
    if (backend->ops && backend->ops->destroy) {
        backend->ops->destroy(backend);
    }
    free(backend);
}

const char *sync_backend_name(sync_backend_type_t type) {
    switch (type) {
        case SYNC_BACKEND_WEBDAV: return "webdav";
        case SYNC_BACKEND_NFS: return "nfs";
        case SYNC_BACKEND_GIT: return "git";
        case SYNC_BACKEND_NONE:
        default: return "none";
    }
}

const char *sync_backend_display_name(sync_backend_type_t type) {
    switch (type) {
        case SYNC_BACKEND_WEBDAV: return "WebDAV 远端存储";
        case SYNC_BACKEND_NFS: return "NFS / 局域网共享目录";
        case SYNC_BACKEND_GIT: return "Git 远端版本库";
        case SYNC_BACKEND_NONE:
        default: return "单机本地模式 (未启用远端同步)";
    }
}

void sync_free_drift_summary(sync_drift_summary_t *summary) {
    if (!summary) return;
    if (summary->items) {
        free(summary->items);
        summary->items = NULL;
    }
    summary->count = 0;
}

void sync_free_manifest(sync_remote_manifest_t *manifest) {
    if (!manifest) return;
    if (manifest->items) {
        free(manifest->items);
        manifest->items = NULL;
    }
    manifest->count = 0;
}
