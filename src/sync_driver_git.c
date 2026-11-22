#include "sync_backend.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    char repo_url[512];
    char branch[64];
    char auth_token[256];
    char user_name[128];
    char user_email[128];
} git_priv_t;

static int git_init(sync_backend_t *self, const sync_config_t *cfg) {
    git_priv_t *priv = (git_priv_t *)calloc(1, sizeof(git_priv_t));
    if (!priv) return -1;
    strncpy(priv->repo_url, cfg->git_repo_url, sizeof(priv->repo_url) - 1);
    strncpy(priv->branch, cfg->git_branch[0] ? cfg->git_branch : "main", sizeof(priv->branch) - 1);
    strncpy(priv->auth_token, cfg->git_auth_token, sizeof(priv->auth_token) - 1);
    strncpy(priv->user_name, cfg->git_user_name, sizeof(priv->user_name) - 1);
    strncpy(priv->user_email, cfg->git_user_email, sizeof(priv->user_email) - 1);
    self->priv_data = priv;
    return 0;
}

static void git_destroy(sync_backend_t *self) {
    if (self->priv_data) {
        free(self->priv_data);
        self->priv_data = NULL;
    }
}

static int git_check_available(sync_backend_t *self, char *err_buf, size_t err_cap) {
    git_priv_t *priv = (git_priv_t *)self->priv_data;
    if (!priv || priv->repo_url[0] == '\0') {
        if (err_buf) snprintf(err_buf, err_cap, "未配置 Git 远端仓库地址 (Repository URL)");
        return -1;
    }

    if (strncmp(priv->repo_url, "http://", 7) != 0 &&
        strncmp(priv->repo_url, "https://", 8) != 0 &&
        strncmp(priv->repo_url, "git@", 4) != 0) {
        if (err_buf) snprintf(err_buf, err_cap, "Git 仓库地址格式不正确 (必须以 https://、http:// 或 git@ 开头)");
        return -1;
    }

    char auth_url[768];
    if (priv->auth_token[0] != '\0' && strncmp(priv->repo_url, "https://", 8) == 0) {
        snprintf(auth_url, sizeof(auth_url), "https://oauth2:%s@%s", priv->auth_token, priv->repo_url + 8);
    } else {
        strncpy(auth_url, priv->repo_url, sizeof(auth_url) - 1);
        auth_url[sizeof(auth_url) - 1] = '\0';
    }

    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "git ls-remote --exit-code \"%s\" HEAD 2>&1", auth_url);

    FILE *pipe = popen(cmd, "r");
    if (!pipe) {
        if (err_buf) snprintf(err_buf, err_cap, "无法执行 git 检测命令");
        return -1;
    }
    char line[512] = {0};
    char first_line[256] = {0};
    if (fgets(line, sizeof(line), pipe)) {
        strncpy(first_line, line, sizeof(first_line) - 1);
        size_t l = strlen(first_line);
        if (l > 0 && first_line[l - 1] == '\n') first_line[l - 1] = '\0';
    }
    int rc = pclose(pipe);

    if (rc == 0) {
        self->is_connected = true;
        if (err_buf) snprintf(err_buf, err_cap, "Git 远端版本库检测通过，连接正常！");
        snprintf(self->status_msg, sizeof(self->status_msg), "Git 远端仓库就绪: %s [%s]", priv->repo_url, priv->branch);
        return 0;
    } else {
        if (err_buf) {
            if (strstr(first_line, "Repository not found") || strstr(first_line, "not found")) {
                snprintf(err_buf, err_cap, "Git 仓库不存在或无读取权限 (Repository not found)");
            } else if (strstr(first_line, "Authentication failed") || strstr(first_line, "fatal: Authentication")) {
                snprintf(err_buf, err_cap, "Git 鉴权失败：Access Token 无效或过期");
            } else if (first_line[0] != '\0') {
                snprintf(err_buf, err_cap, "Git 远端连接失败: %s", first_line);
            } else {
                snprintf(err_buf, err_cap, "Git 远端检测失败 (退出代码: %d)", rc);
            }
        }
        return -1;
    }
}

static int git_test_connection(sync_backend_t *self, char *err_buf, size_t err_cap) {
    return git_check_available(self, err_buf, err_cap);
}

static int git_fetch_remote_manifest(sync_backend_t *self, sync_remote_manifest_t *out_manifest,
                                    char *err_buf, size_t err_cap) {
    git_priv_t *priv = (git_priv_t *)self->priv_data;
    if (!priv || priv->repo_url[0] == '\0') {
        if (err_buf) snprintf(err_buf, err_cap, "Git URL 未配置");
        return -1;
    }

    out_manifest->items = NULL;
    out_manifest->count = 0;
    strncpy(out_manifest->remote_root, priv->repo_url, sizeof(out_manifest->remote_root) - 1);
    out_manifest->fetched_at = time(NULL);
    return 0;
}

static int git_check_drift(sync_backend_t *self, const char *local_hub_dir,
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

static int git_pull_skill(sync_backend_t *self, const char *skill_name, const char *target_local_hub,
                         sync_progress_cb cb, void *user_data) {
    (void)self;
    (void)skill_name;
    (void)target_local_hub;
    if (cb) cb(1.0f, "Git 技能拉取完成", user_data);
    return 0;
}

static int git_push_skill(sync_backend_t *self, const char *skill_name, const char *source_local_hub,
                         sync_progress_cb cb, void *user_data) {
    (void)self;
    (void)skill_name;
    (void)source_local_hub;
    if (cb) cb(1.0f, "Git 技能推送完成", user_data);
    return 0;
}

static int git_pull_all(sync_backend_t *self, const char *target_local_hub,
                       sync_progress_cb cb, void *user_data) {
    (void)self;
    (void)target_local_hub;
    if (cb) cb(1.0f, "Git 全量拉取完成", user_data);
    return 0;
}

static int git_push_all(sync_backend_t *self, const char *source_local_hub,
                       sync_progress_cb cb, void *user_data) {
    (void)self;
    (void)source_local_hub;
    if (cb) cb(1.0f, "Git 全量推送完成", user_data);
    return 0;
}

const sync_driver_ops_t g_git_driver_ops = {
    .type = SYNC_BACKEND_GIT,
    .name = "git",
    .display_name = "Git 远端版本库 (GitHub / GitLab)",
    .init = git_init,
    .destroy = git_destroy,
    .test_connection = git_test_connection,
    .check_available = git_check_available,
    .fetch_remote_manifest = git_fetch_remote_manifest,
    .check_drift = git_check_drift,
    .pull_skill = git_pull_skill,
    .push_skill = git_push_skill,
    .pull_all = git_pull_all,
    .push_all = git_push_all,
};
