#ifndef SYNC_BACKEND_H
#define SYNC_BACKEND_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

/*
 * Supported Multi-Host Remote Synchronization Backends
 * Each host configuring the same backend endpoint shares the exact same canonical version.
 */
typedef enum {
    SYNC_BACKEND_NONE = 0,    /* 禁用远端同步 (单机本地离线模式) */
    SYNC_BACKEND_WEBDAV = 1,  /* WebDAV (Nextcloud / 坚果云 / 自建 WebDAV) */
    SYNC_BACKEND_NFS = 2,     /* NFS / 局域网共享挂载目录 (NAS / SMB / 共享文件系统) */
    SYNC_BACKEND_GIT = 3,     /* Git 远端版本库 (GitHub / GitLab / Gitea) */
} sync_backend_type_t;

/*
 * Drift status between Local Master Hub (~/.ai-space/skills/) and Remote Shared Backend
 */
typedef enum {
    SYNC_DRIFT_IN_SYNC = 0,       /* 🟢 两端内容完全一致 */
    SYNC_DRIFT_REMOTE_AHEAD = 1,  /* 🔵 远端版本已由其他主机更新 (本地需从远端拉取 Pull) */
    SYNC_DRIFT_LOCAL_AHEAD = 2,   /* 🟡 本地主仓有新提交变更 (需推送至远端共享 Push) */
    SYNC_DRIFT_REMOTE_ONLY = 3,   /* 🟣 远端独有技能 (本地尚未拉取) */
    SYNC_DRIFT_LOCAL_ONLY = 4,    /* 🟠 本地主仓独有技能 (尚未发布至远端) */
    SYNC_DRIFT_CONFLICT = 5,      /* 🔴 两端均发生独立改动 (需协同或确认覆盖) */
} sync_drift_status_t;

/* A remote skill item recorded in the remote manifest */
typedef struct {
    char skill_name[128];
    char hash[32];                /* 16-hex content fingerprint */
    time_t mtime;
    size_t size;
    int file_count;
    char version[64];
    char description[256];
} sync_remote_skill_item_t;

/* Full remote manifest representing the remote shared hub state */
typedef struct {
    sync_remote_skill_item_t *items;
    int count;
    time_t fetched_at;
    char remote_root[512];
    char revision[64];
} sync_remote_manifest_t;

/* Comparison item between local Master Hub and remote shared hub */
typedef struct {
    char skill_name[128];
    sync_drift_status_t status;
    char local_hash[32];
    char remote_hash[32];
    time_t local_mtime;
    time_t remote_mtime;
    int local_files;
    int remote_files;
    char status_desc[128];
} sync_drift_item_t;

/* Aggregated drift summary across all skills */
typedef struct {
    sync_drift_item_t *items;
    int count;
    int in_sync_count;
    int remote_ahead_count;
    int local_ahead_count;
    int remote_only_count;
    int local_only_count;
    int conflict_count;
    char last_checked_at[32];
} sync_drift_summary_t;

typedef enum {
    SYNC_SHARE_MOUNTED = 0,
    SYNC_SHARE_NFS,
    SYNC_SHARE_SMB,
} sync_share_mode_t;

/* Unified configuration for any synchronization backend */
typedef struct {
    sync_backend_type_t type;

    /* WebDAV options */
    char webdav_url[512];          /* e.g. "https://dav.jianguoyun.com/dav/ai-space/" */
    char webdav_user[128];
    char webdav_pass[128];
    char webdav_remote_path[256];

    /* NFS / Shared Folder options */
    sync_share_mode_t share_mode;
    char share_server[256];
    char share_export[512];
    char share_subdir[256];
    char smb_user[128];
    char smb_pass[128];
    char nfs_mount_path[512];       /* e.g. "/Volumes/nas/ai-space/skills" */

    /* Git Remote Repo options */
    char git_repo_url[512];        /* e.g. "https://github.com/myteam/ai-skills.git" */
    char git_branch[64];           /* e.g. "main" */
    char git_auth_token[256];
    char git_user_name[128];
    char git_user_email[128];
    char git_cache_dir[512];       /* Local clone/cache staging directory (optional, default ~/.ai-space/cache/git_remote) */
} sync_config_t;

/* Progress callback for remote sync transfers */
typedef void (*sync_progress_cb)(float percent, const char *msg, void *user_data);

/* Forward declaration */
typedef struct sync_backend_t sync_backend_t;

typedef struct {
    char skill[128];
    char hash[32];
} sync_baseline_item_t;

typedef struct {
    sync_baseline_item_t *items; /* 按技能名称排序，供差异检查二分查找。 */
    size_t count;
} sync_baseline_t;

enum { SYNC_BATCH_UNCHANGED = 1, SYNC_TRANSFER_CHANGED = -3 };

/* 统一的同步驱动接口，由各后端提供连接、清单和文件传输实现。 */
typedef struct sync_driver_ops_t {
    sync_backend_type_t type;
    const char *name;              /* "webdav", "nfs", "git", "none" */
    const char *display_name;      /* Friendly UI display string */

    /* 驱动实例的初始化与释放。 */
    int (*init)(sync_backend_t *self, const sync_config_t *cfg);
    void (*destroy)(sync_backend_t *self);

    /* Connection & Health Check */
    int (*test_connection)(sync_backend_t *self, char *err_buf, size_t err_cap);

    /* Availability Check Interface (各个后端检测是否可用，必须通过后才可激活) */
    int (*check_available)(sync_backend_t *self, char *err_buf, size_t err_cap);

    /* 读取远端清单，并与本地主仓比较。 */
    int (*fetch_remote_manifest)(sync_backend_t *self, sync_remote_manifest_t *out_manifest, char *err_buf, size_t err_cap);
    int (*check_drift)(sync_backend_t *self, const char *local_hub_dir, sync_drift_summary_t *out_diff, char *err_buf, size_t err_cap);

    /* 单个技能的拉取和推送。 */
    int (*pull_skill)(sync_backend_t *self, const char *skill_name, const char *target_local_hub, sync_progress_cb cb, void *user_data);
    int (*push_skill)(sync_backend_t *self, const char *skill_name, const char *source_local_hub, sync_progress_cb cb, void *user_data);

    /* 批量入口按差异决定传输范围。 */
    int (*pull_all)(sync_backend_t *self, const char *target_local_hub, sync_progress_cb cb, void *user_data);
    int (*push_all)(sync_backend_t *self, const char *source_local_hub, sync_progress_cb cb, void *user_data);
    /* 无需传输时可把最新差异交给调用方，返回的条目由调用方释放。 */
    int (*transfer_batch)(sync_backend_t *self, const char *local_hub_dir, bool pull,
                          sync_progress_cb cb, void *user_data, sync_drift_summary_t *unchanged_drift);
} sync_driver_ops_t;

struct sync_backend_t {
    const sync_driver_ops_t *ops;
    sync_config_t cfg;
    void *priv_data;
    bool is_connected;
    char status_msg[256];
    const sync_baseline_t *baseline;       /* 借用当前任务加载的基线。 */
    const sync_drift_item_t *expected_skill; /* 借用当前传输计划中的指纹。 */
};

/* Factory and memory helpers */
int sync_backend_create(const sync_config_t *cfg, sync_backend_t **out_backend);
void sync_backend_destroy(sync_backend_t *backend);

/*
 * Check if a candidate backend configuration is healthy and available.
 * Must return 0 before the backend is allowed to be activated.
 */
int sync_backend_check_available(const sync_config_t *cfg, char *err_buf, size_t err_cap);

bool sync_git_uses_ssh(const char *url);
bool sync_git_uses_token(const char *url);

const char *sync_backend_name(sync_backend_type_t type);
const char *sync_backend_display_name(sync_backend_type_t type);

void sync_free_drift_summary(sync_drift_summary_t *summary);
void sync_free_manifest(sync_remote_manifest_t *manifest);

#endif /* SYNC_BACKEND_H */
