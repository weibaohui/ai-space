#ifndef VIEW_COMMON_H
#define VIEW_COMMON_H

#include <cui/cui.h>
#include "app_state.h"

/* Render sidebar navigation, theme switch and accent palette */
void render_app_sidebar(cui_ctx *ui, app_state_t *s);

/* Legacy top navigation bar (kept as empty compatibility stubs) */
void render_global_nav_bar(cui_ctx *ui, app_state_t *s);
void render_global_navbar(cui_ctx *ui, app_state_t *s);

/* Render modal/overlay progress dialog during background operations */
void render_op_progress_modal(cui_ctx *ui, app_state_t *s);

/* Render global About AI-Space modal dialog */
void render_about_modal(cui_ctx *ui, app_state_t *s);

/* Shared diff helpers */
void clear_diff_texts(app_state_t *s);
void free_skill_diff_manifest(hub_skill_diff_manifest_t *m);
void build_skill_diff_manifest(app_state_t *s, const char *skill_name, int tool_idx);
void load_hub_diff(app_state_t *s, const char *skill_name, int tool_idx, const char *rel_file);
void open_hub_diff_view(app_state_t *s, const char *skill_name);

/* Background thread dispatchers */
void start_hub_scan_thread(app_state_t *s);
void start_adopt_all_thread(app_state_t *s);
void start_copy_install_thread(app_state_t *s, const char *tool_skills_path,
                               const char *skill_name, const char *tool_name, int tool_idx);
void start_sync_master_to_tool_thread(app_state_t *s, const char *tool_skills_path,
                                     const char *skill_name, const char *tool_name, int tool_idx);
void start_promote_to_master_thread(app_state_t *s, const char *tool_skills_path,
                                    const char *skill_name, const char *tool_name, int tool_idx);
void start_broadcast_thread(app_state_t *s, const char *skill_name, int tool_idx);

/* Skill Distribution dispatchers & modal */
void start_distribute_all_thread(app_state_t *s);
void start_distribute_skill_to_all_thread(app_state_t *s, const char *skill_name);
void render_distribute_skill_modal(cui_ctx *ui, app_state_t *s);

/* Remote Sync background dispatchers */
void start_sync_check_thread(app_state_t *s);
void start_sync_check_available_thread(app_state_t *s, bool activate_on_success);
void start_sync_pull_skill_thread(app_state_t *s, const char *skill_name);
void start_sync_push_skill_thread(app_state_t *s, const char *skill_name);
void start_sync_pull_all_thread(app_state_t *s);
void start_sync_push_all_thread(app_state_t *s);

/* Toast feedback */
void app_set_toast(app_state_t *s, const char *msg);

#endif /* VIEW_COMMON_H */
