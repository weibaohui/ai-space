#ifndef APP_STATE_H
#define APP_STATE_H

#include <stdbool.h>
#include <stddef.h>
#include <sqlite3.h>
#include <pthread.h>
#include <cui/cui.h>
#include "models.h"
#include "skill_scanner.h"
#include "hub_manager.h"
#include "sync_backend.h"

typedef enum {
    PAGE_TOOL_LIST = 0,
    PAGE_HUB_MATRIX = 1,
    PAGE_TOOL_SKILLS = 2,
    PAGE_SETTINGS = 3,
    PAGE_SYNC_MANAGER = 4,
} page_view_t;

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
    float percent;              /* 0.0 to 1.0 */
    bool indeterminate;
    int current_item;
    int total_items;
    pthread_t thread;
    bool thread_running;
} op_progress_t;

/* Async background scanner state for the Hub Matrix */
typedef struct {
    bool is_scanning;
    float progress;              /* 0.0 to 1.0 */
    char status_text[256];
    pthread_t thread;
    bool thread_running;
} hub_scan_state_t;

/* Status for individual files within a skill */
typedef enum {
    DIFF_FILE_IN_SYNC = 0,    /* Identical in both master and tool */
    DIFF_FILE_MODIFIED = 1,   /* Exists in both but content has drifted */
    DIFF_FILE_ADDED = 2,      /* Exists only in tool (added locally) */
    DIFF_FILE_DELETED = 3,    /* Exists only in master (missing in tool copy) */
} diff_file_status_t;

typedef struct {
    char rel_path[256];
    char file_name[128];
    diff_file_status_t status;
    size_t master_size;
    size_t tool_size;
    bool is_binary;
    int added_lines;            /* Added lines in tool vs master */
    int removed_lines;          /* Removed lines in tool vs master */
} hub_diff_file_item_t;

typedef struct {
    char skill_name[128];
    char tool_name[128];
    hub_diff_file_item_t *items;
    int count;
    int modified_count;
    int added_count;
    int deleted_count;
    int in_sync_count;
    int total_added_lines;
    int total_removed_lines;
} hub_skill_diff_manifest_t;

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
    char hub_search_kw[128];
    int hub_filter_mode;        /* 0: 全部, 1: 存在分化, 2: 未建立主仓, 3: 完全一致 */
    int hub_subview;            /* hub_subview_t */
    int prev_hub_subview;       /* previous subview to return to from diff/explorer */
    char master_skill_search_kw[128];
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
    hub_skill_diff_manifest_t diff_manifest;
    bool diff_manifest_loaded;
    file_node_t *hub_diff_file_tree;

    /* Tool skills view state */
    char active_tool_name[128];
    char active_tool_path[512];
    skill_item_t *skill_items;
    int skill_count;
    char skill_search_kw[128];
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
    int settings_active_tab;    /* 0: 仓储与路径, 1: 远端同步, 2: 扫描与策略, 3: 常规偏好 */
    char setting_hub_dir[512];
    char setting_backup_dir[512];
    bool setting_auto_backup;
    bool setting_auto_scan;
    int setting_default_view;
    int setting_scan_depth;

    /* Remote Synchronization State */
    sync_config_t sync_cfg;
    sync_backend_t *sync_backend;
    sync_drift_summary_t sync_drift;
    bool sync_drift_loaded;
    bool sync_is_busy;
    char sync_status_msg[256];
    char sync_test_result[256];
    bool sync_test_ok;

    /* Pre-activation Availability Check State */
    sync_backend_type_t setting_sync_candidate_type;
    char sync_candidate_check_msg[256];
    bool sync_candidate_checked_ok;
    bool sync_has_checked_candidate;

    /* Global About Dialog (extracted from settings) */
    bool show_about_dialog;
} app_state_t;

/* Common helper functions */
bool ci_contains(const char *haystack, const char *needle);
void format_size(size_t bytes, char *buf, size_t cap);
void app_set_toast(app_state_t *s, const char *msg);

void reload_db_data(app_state_t *s);
void do_scan_sync(app_state_t *s);

void clear_diff_texts(app_state_t *s);
void load_hub_diff(app_state_t *s, const char *skill_name, int tool_idx, const char *rel_file);
void open_hub_diff_view(app_state_t *s, const char *skill_name);

void start_hub_scan_thread(app_state_t *s);
void start_adopt_all_thread(app_state_t *s);

void app_init_settings(app_state_t *s);
void app_save_settings(app_state_t *s);
void app_init_sync_backend(app_state_t *s);

void start_sync_check_thread(app_state_t *s);
void start_sync_pull_skill_thread(app_state_t *s, const char *skill_name);
void start_sync_push_skill_thread(app_state_t *s, const char *skill_name);
void start_sync_pull_all_thread(app_state_t *s);
void start_sync_push_all_thread(app_state_t *s);

#endif /* APP_STATE_H */
