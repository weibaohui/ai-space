#ifndef APP_STATE_H
#define APP_STATE_H

#include <stdbool.h>
#include <stddef.h>
#include <sqlite3.h>
#include <cui/cui.h>
#include "models.h"
#include "version.h"
#include "skill_scanner.h"
#include "skill_file_tree.h"
#include "hub_manager.h"
#include "sync_backend.h"
#include "sync_git_merge.h"
#include "skill_diff.h"
#include "app_storage.h"

typedef enum {
    PAGE_DASHBOARD = 0,        /* 首页 */
    PAGE_TOOL_LIST = 1,        /* 工具列表 (按工具维) */
    PAGE_ALL_SKILLS = 2,       /* 技能列表 (全网全景大盘) */
    PAGE_HUB_MATRIX = 3,       /* 主仓管理 (基准库管理与分发) */
    PAGE_TOOL_SKILLS = 4,      /* 单个工具内的技能详情 */
    PAGE_SYNC_MANAGER = 5,     /* 远端同步管理 */
    PAGE_SETTINGS = 6,         /* 设置 */
} page_view_t;

typedef enum {
    SETTINGS_TAB_STORAGE_PATHS,
    SETTINGS_TAB_REMOTE_SYNC,
    SETTINGS_TAB_SCAN,
    SETTINGS_TAB_PREFERENCES,
    SETTINGS_TAB_DEPENDENCIES,
    SETTINGS_TAB_LOCAL_STORAGE,
    SETTINGS_TAB_ABOUT,
    SETTINGS_TAB_COUNT,
} settings_tab_t;

typedef enum {
    STORAGE_CLEANUP_CACHE,
    STORAGE_CLEANUP_OLD_SNAPSHOTS,
    STORAGE_CLEANUP_ALL_SNAPSHOTS,
    STORAGE_CLEANUP_SINGLE_SNAPSHOT,
    STORAGE_CLEANUP_CACHE_AND_OLD_SNAPSHOTS,
} storage_cleanup_action_t;

typedef enum {
    SKILL_FILTER_ALL,
    SKILL_FILTER_CHANGED,
    SKILL_FILTER_UNCOLLECTED,
    SKILL_FILTER_MATCHED,
} skill_filter_t;

typedef enum {
    HUB_SUBVIEW_MASTER_SKILLS = 0, /* 主仓技能集中管理 (已入库技能) */
    HUB_SUBVIEW_DIFF = 1,          /* 差异对比与同步面板 */
    HUB_SUBVIEW_SCAN_MATRIX = 2,   /* 技能全景扫描与关系矩阵 */
    HUB_SUBVIEW_EXPLORER = 3,      /* 主仓技能文件浏览 */
} hub_subview_t;

/* Progress state for long-running batch operations */
typedef struct {
    bool active;
    char title[128];
    char message[256];
    char sub_message[256];      /* Detail / security note */
    float percent;              /* 0.0 to 1.0 */
    bool indeterminate;
    int current_item;
    int total_items;
} op_progress_t;

/* Async background scanner state for the Hub Matrix */
typedef struct {
    bool is_scanning;
    bool failed;
    float progress;              /* 0.0 to 1.0 */
    char status_text[256];
} hub_scan_state_t;

/* Dependency severity levels */
typedef enum {
    DEP_SEVERITY_REQUIRED = 0,    /* 核心必须 */
    DEP_SEVERITY_RECOMMENDED = 1  /* 推荐环境 */
} dep_severity_t;

/* Single dependency item */
typedef struct {
    char binary_name[32];         /* e.g. "git" */
    char display_name[64];        /* e.g. "Git 版本控制工具" */
    char category[32];            /* e.g. "核心协同" */
    char purpose[128];            /* e.g. "用于技能版本管理、主仓同步及远端拉取推送" */
    dep_severity_t severity;
    bool installed;
    char path[256];
    char version[128];
    char install_hint[128];
} dep_item_t;

/* Overall dependency check state */
typedef struct {
    bool loaded;
    dep_item_t items[16];
    int count;
    int required_missing_count;
    int recommended_missing_count;
    char generated_prompt[2048];
    char last_checked_at[32];
} dep_check_state_t;

typedef struct app_state_t {
    sqlite3 *db;
    char db_path[512];

    page_view_t current_page;
    page_view_t prev_page;

    /* Tool list state */
    tool_record_t *records;
    int record_count;
    tool_stats_t stats;

    char search_kw[128];
    char last_search_kw[128];

    cui_table_sort sort;
    int last_sort_col;
    bool last_sort_desc;

    char toast_msg[256];
    int toast_frames;
    bool needs_reload;

    /* Central Hub & Relation Matrix state */
    hub_matrix_t hub_matrix;
    bool hub_matrix_loaded;
    hub_scan_state_t hub_scan;
    struct app_hub_job *hub_job;
    char hub_search_kw[128];
    int hub_filter_mode;        /* skill_filter_t */
    int hub_subview;            /* hub_subview_t */
    int prev_hub_subview;       /* previous subview to return to from diff/explorer */
    char master_skill_search_kw[128];
    int master_skill_filter;
    file_node_t *master_skill_file_tree;

    /* Generic operation progress state (progress bar / modal) */
    op_progress_t op_progress;

    /* Hub Detail & Diff state */
    char selected_hub_skill[128];
    int selected_hub_tool_idx;  /* Index into entry->tool_copies */
    char diff_file_rel[256];    /* Relative path e.g. "SKILL.md" */
    char *diff_old_text;        /* Master Hub text */
    char *diff_new_text;        /* Tool copy text */
    int diff_current_change;    /* Selected change index for cui_diff_view */
    cui_diff_mode diff_mode;    /* CUI_DIFF_SPLIT (0) or CUI_DIFF_INLINE (1) */
    int diff_sync_view_mode;    /* 0: 文档预览, 1: 双栏比对 */
    int diff_detail_tab;       /* 0: 差异, 1: 副本, 2: 文件 */
    hub_skill_diff_manifest_t diff_manifest;
    bool diff_manifest_loaded;
    file_node_t *hub_diff_file_tree;

    /* Tool skills view state */
    char active_tool_name[128];
    char active_tool_path[512];
    skill_item_t *skill_items;
    int skill_count;
    char skill_search_kw[128];
    int tool_detail_tab;       /* 0: 预览, 1: 技能, 2: 文件 */
    int skill_view_mode;        /* 0: 树形浏览 (左树右内容), 1: 全部技能清单 */
    cui_table_sort skill_table_sort;

    /* Selected skill state */
    char active_skill_name[128];
    char active_skill_path[1024];
    file_node_t *skill_file_tree;

    /* Selected file state */
    char selected_file_path[1024];
    char selected_file_name[128];
    char *selected_file_content;
    size_t selected_file_size;
    int selected_file_lines;
    bool selected_file_is_binary;
    int doc_render_mode;        /* 0: 预览/高亮, 1: 源码/代码 */

    /* Settings Page state */
    int settings_active_tab;
    char setting_hub_dir[512];
    char setting_backup_dir[512];
    bool setting_auto_backup;
    bool setting_auto_scan;
    int setting_default_view;
    int setting_scan_depth;
    bool dark_theme;            /* true: CUI_THEME_DARK, false: CUI_THEME_LIGHT */
    int accent_color;           /* cui_accent enum value (0..CUI_ACCENT_COUNT-1) */

    /* Remote Synchronization State */
    sync_config_t sync_cfg;
    struct app_sync_job *sync_job;
    sync_drift_summary_t sync_drift;
    bool sync_drift_loaded;
    bool show_sync_conflict_modal;
    char sync_conflict_skill[128];
    sync_git_draft_t sync_conflict_draft;
    bool sync_drafts_loaded;
    char sync_pending_merge_skill[128];
    bool sync_is_busy;
    char sync_status_msg[256];

    /* Pre-activation Availability Check State */
    sync_backend_type_t setting_sync_candidate_type;
    sync_config_t sync_pending_cfg;
    char sync_candidate_check_msg[256];
    bool sync_candidate_checked_ok;
    bool sync_has_checked_candidate;

    /* Skill Distribution Modal State (从主仓分发技能至各本地工具) */
    bool show_distribute_modal;
    char distribute_skill_name[128];

    /* Uninstall Modals State */
    bool show_uninstall_all_modal;
    bool uninstall_all_is_batch;
    char uninstall_all_skill_name[128];

    bool show_uninstall_tool_modal;
    char uninstall_tool_skill_name[128];

    /* 清空工具技能目录的确认状态 */
    bool show_clear_tool_modal;
    char clear_tool_name[128];
    char clear_tool_path[512];

    /* Master repository removal confirmation */
    bool show_remove_master_modal;
    char remove_master_skill[128]; /* Empty for the selected batch */

    /* Local Storage Management State */
    storage_stats_t storage_stats;
    app_storage_cache_t storage_cache;
    bool show_storage_cleanup_modal;
    storage_cleanup_action_t storage_cleanup_action;
    char storage_cleanup_snapshot[128];

    /* Dependency Check State */
    dep_check_state_t dep_state;
} app_state_t;

/* Common helper functions */
bool ci_contains(const char *haystack, const char *needle);
void format_size(size_t bytes, char *buf, size_t cap);
void app_set_toast(app_state_t *s, const char *msg);

void reload_db_data(app_state_t *s);
void do_scan_sync(app_state_t *s);

/* Dependency check helpers */
void app_check_dependencies(app_state_t *s);

#endif /* APP_STATE_H */
