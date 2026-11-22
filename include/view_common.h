#ifndef VIEW_COMMON_H
#define VIEW_COMMON_H

#include <cui/cui.h>
#include "app_state.h"

/* Render top navigation bar with tab switching */
void render_global_nav_bar(cui_ctx *ui, app_state_t *s);
void render_global_navbar(cui_ctx *ui, app_state_t *s);

/* Render modal/overlay progress dialog during background operations */
void render_op_progress_modal(cui_ctx *ui, app_state_t *s);

/* Shared diff helpers */
void clear_diff_texts(app_state_t *s);
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

/* Toast feedback */
void app_set_toast(app_state_t *s, const char *msg);

#endif /* VIEW_COMMON_H */
