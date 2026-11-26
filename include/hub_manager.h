#ifndef HUB_MANAGER_H
#define HUB_MANAGER_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>
#include <sqlite3.h>

#define HUB_STATUS_IN_SYNC        0   /* 🟢 物理副本与主仓内容完全一致 */
#define HUB_STATUS_LOCAL_MODIFIED 1   /* 🟡 该工具中的副本被修改过（比主仓新或改动） */
#define HUB_STATUS_OUTDATED       2   /* 🔵 该工具副本落后于主仓（主仓更新） */
#define HUB_STATUS_NO_HUB         3   /* 🟣 仅存在于该工具，主仓尚未收录 */
#define HUB_STATUS_NOT_INSTALLED  4   /* ⚪ 该工具未安装此技能 */
#define HUB_STATUS_UNREADABLE     5   /* 读取失败，无法比较 */

/* Status of a skill in a specific tool */
typedef struct {
    char tool_name[64];
    char tool_skills_path[512];
    char skill_path[1024];
    bool exists;
    char hash[32];              /* 16-hex content hash */
    time_t mtime;               /* latest modified time */
    int status;                 /* HUB_STATUS_* */
} tool_copy_status_t;

/* A unique skill entry aggregated across all tools and the hub */
typedef struct {
    char skill_name[128];
    char hub_path[1024];
    bool in_hub;
    bool hub_unreadable;
    char hub_hash[32];
    time_t hub_mtime;
    char description[256];

    int installed_count;        /* count of tools that have this skill */
    int modified_count;         /* count of tools where copy is modified */
    int outdated_count;         /* count of tools where copy is outdated */
    int unreadable_count;       /* count of tool copies that could not be compared */

    tool_copy_status_t *tool_copies;
    int tool_count;
    bool selected;
} hub_skill_entry_t;

/* Aggregate matrix of all unique skills and all tools */
typedef struct {
    char hub_dir[512];          /* ~/.skillboard/skills */
    char backup_dir[512];       /* ~/.skillboard/backups */

    char **tools;               /* array of tool names */
    char **tool_paths;          /* array of tool skills paths */
    int tool_count;

    hub_skill_entry_t *skills;  /* unique skills */
    int skill_count;

    int total_unique_skills;
    int hub_managed_skills;
    int drifted_skills_count;   /* count of skills with at least 1 modified/outdated copy */
    char last_scanned_at[32];
} hub_matrix_t;

/* Progress callback for long-running operations */
typedef void (*hub_progress_cb)(float percent, const char *status_msg, void *user_data);

/* Scan all registered tools and the Master Hub, build the full relation & status matrix */
int hub_scan_matrix(sqlite3 *db, hub_matrix_t *out_matrix);
int hub_scan_matrix_ex(sqlite3 *db, hub_matrix_t *out_matrix, hub_progress_cb cb, void *user_data);

/* 返回矩阵中的只读条目，矩阵刷新或释放后指针失效。 */
const hub_skill_entry_t *hub_find_skill(const hub_matrix_t *matrix, const char *name);

/* Free memory in hub_matrix_t */
void hub_free_matrix(hub_matrix_t *matrix);

#endif /* HUB_MANAGER_H */
