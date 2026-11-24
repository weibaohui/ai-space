#include "hub_paths.h"
#include "app_settings.h"
#include "db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "portability.h"

void app_init_settings(app_state_t *s) {
    if (!s || !s->db) return;

    db_get_setting(s->db, "master_hub_dir", s->setting_hub_dir, sizeof(s->setting_hub_dir), "~/.ai-space/skills");
    db_get_setting(s->db, "backup_dir", s->setting_backup_dir, sizeof(s->setting_backup_dir), "~/.ai-space/backups");

    char val[64];
    db_get_setting(s->db, "auto_backup", val, sizeof(val), "1");
    s->setting_auto_backup = (strcmp(val, "0") != 0);

    db_get_setting(s->db, "auto_scan", val, sizeof(val), "1");
    s->setting_auto_scan = (strcmp(val, "0") != 0);

    db_get_setting(s->db, "default_view", val, sizeof(val), "0");
    s->setting_default_view = atoi(val);

    db_get_setting(s->db, "scan_depth", val, sizeof(val), "5");
    s->setting_scan_depth = atoi(val);
    if (s->setting_scan_depth <= 0) s->setting_scan_depth = 5;

    db_get_setting(s->db, "dark_theme", val, sizeof(val), "1");
    s->dark_theme = (strcmp(val, "0") != 0);

    db_get_setting(s->db, "accent_color", val, sizeof(val), "2");
    s->accent_color = atoi(val);
    if (s->accent_color < 0 || s->accent_color >= CUI_ACCENT_COUNT) {
        s->accent_color = CUI_ACCENT_BLUE;
    }

    /* 设置加载后先同步主仓路径，再读取后端配置。 */
    hub_set_custom_dirs(s->setting_hub_dir, s->setting_backup_dir);

    /* 从同一配置恢复运行后端和待编辑表单。 */
    char s_type[16];
    db_get_setting(s->db, "sync_backend_type", s_type, sizeof(s_type), "0"); /* default Single Machine (None) */
    s->sync_cfg.type = (sync_backend_type_t)atoi(s_type);

    db_get_setting(s->db, "sync_webdav_url", s->sync_cfg.webdav_url, sizeof(s->sync_cfg.webdav_url), "https://dav.jianguoyun.com/dav/ai-space/");
    db_get_setting(s->db, "sync_webdav_user", s->sync_cfg.webdav_user, sizeof(s->sync_cfg.webdav_user), "");
    db_get_setting(s->db, "sync_webdav_pass", s->sync_cfg.webdav_pass, sizeof(s->sync_cfg.webdav_pass), "");
    db_get_setting(s->db, "sync_webdav_path", s->sync_cfg.webdav_remote_path, sizeof(s->sync_cfg.webdav_remote_path), "/ai-space/skills");

    db_get_setting(s->db, "sync_nfs_path", s->sync_cfg.nfs_mount_path, sizeof(s->sync_cfg.nfs_mount_path), "~/.ai-space/remote-share");
    char share_mode[16];
    db_get_setting(s->db, "sync_share_mode", share_mode, sizeof(share_mode),
                   s->sync_cfg.type == SYNC_BACKEND_NFS ? "0" : "1");
    int mode = atoi(share_mode);
    s->sync_cfg.share_mode = mode >= SYNC_SHARE_MOUNTED && mode <= SYNC_SHARE_SMB
                             ? (sync_share_mode_t)mode : SYNC_SHARE_MOUNTED;
    db_get_setting(s->db, "sync_share_server", s->sync_cfg.share_server, sizeof(s->sync_cfg.share_server), "");
    db_get_setting(s->db, "sync_share_export", s->sync_cfg.share_export, sizeof(s->sync_cfg.share_export), "");
    db_get_setting(s->db, "sync_share_subdir", s->sync_cfg.share_subdir, sizeof(s->sync_cfg.share_subdir), "");
    db_get_setting(s->db, "sync_smb_user", s->sync_cfg.smb_user, sizeof(s->sync_cfg.smb_user), "");
    db_get_setting(s->db, "sync_smb_pass", s->sync_cfg.smb_pass, sizeof(s->sync_cfg.smb_pass), "");

    db_get_setting(s->db, "sync_git_url", s->sync_cfg.git_repo_url, sizeof(s->sync_cfg.git_repo_url), "");
    db_get_setting(s->db, "sync_git_branch", s->sync_cfg.git_branch, sizeof(s->sync_cfg.git_branch), "main");
    db_get_setting(s->db, "sync_git_token", s->sync_cfg.git_auth_token, sizeof(s->sync_cfg.git_auth_token), "");
    db_get_setting(s->db, "sync_git_user", s->sync_cfg.git_user_name, sizeof(s->sync_cfg.git_user_name), "");
    db_get_setting(s->db, "sync_git_email", s->sync_cfg.git_user_email, sizeof(s->sync_cfg.git_user_email), "");

    s->setting_sync_candidate_type = s->sync_cfg.type;
    s->sync_pending_cfg = s->sync_cfg;
}

void app_save_settings(app_state_t *s) {
    if (!s || !s->db) return;

    db_set_setting(s->db, "auto_backup", s->setting_auto_backup ? "1" : "0");
    db_set_setting(s->db, "auto_scan", s->setting_auto_scan ? "1" : "0");

    char val[32];
    snprintf(val, sizeof(val), "%d", s->setting_default_view);
    db_set_setting(s->db, "default_view", val);

    snprintf(val, sizeof(val), "%d", s->setting_scan_depth);
    db_set_setting(s->db, "scan_depth", val);

    db_set_setting(s->db, "dark_theme", s->dark_theme ? "1" : "0");
    snprintf(val, sizeof(val), "%d", s->accent_color);
    db_set_setting(s->db, "accent_color", val);
}

void app_save_storage_settings(app_state_t *s) {
    if (!s || !s->db || s->hub_job || s->sync_job) return;
    db_set_setting(s->db, "master_hub_dir", s->setting_hub_dir);
    db_set_setting(s->db, "backup_dir", s->setting_backup_dir);
    hub_set_custom_dirs(s->setting_hub_dir, s->setting_backup_dir);
    app_storage_invalidate(s);
    s->hub_matrix_loaded = false;
    s->hub_scan.failed = false;
    s->sync_drift_loaded = false;
}

int app_store_sync_settings(sqlite3 *db, const sync_config_t *cfg) {
    char backend[16], share_mode[16];
    snprintf(backend, sizeof(backend), "%d", cfg->type);
    snprintf(share_mode, sizeof(share_mode), "%d", cfg->share_mode);
    const struct { const char *key; const char *value; } settings[] = {
        {"sync_backend_type", backend},
        {"sync_webdav_url", cfg->webdav_url},
        {"sync_webdav_user", cfg->webdav_user},
        {"sync_webdav_pass", cfg->webdav_pass},
        {"sync_webdav_path", cfg->webdav_remote_path},
        {"sync_nfs_path", cfg->nfs_mount_path},
        {"sync_share_mode", share_mode},
        {"sync_share_server", cfg->share_server},
        {"sync_share_export", cfg->share_export},
        {"sync_share_subdir", cfg->share_subdir},
        {"sync_smb_user", cfg->smb_user},
        {"sync_smb_pass", cfg->smb_pass},
        {"sync_git_url", cfg->git_repo_url},
        {"sync_git_branch", cfg->git_branch},
        {"sync_git_token", cfg->git_auth_token},
        {"sync_git_user", cfg->git_user_name},
        {"sync_git_email", cfg->git_user_email},
    };

    /* 将生效后端和对应连接参数一起提交，避免下次启动读到部分配置。 */
    if (sqlite3_exec(db, "BEGIN IMMEDIATE", NULL, NULL, NULL) != SQLITE_OK) return -1;
    for (size_t i = 0; i < sizeof(settings) / sizeof(settings[0]); i++) {
        if (db_set_setting(db, settings[i].key, settings[i].value) != 0) goto rollback;
    }
    if (sqlite3_exec(db, "COMMIT", NULL, NULL, NULL) == SQLITE_OK) return 0;
rollback:
    sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
    return -1;
}
