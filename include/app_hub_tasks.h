#ifndef APP_HUB_TASKS_H
#define APP_HUB_TASKS_H

#include "app_state.h"

void start_hub_scan_thread(app_state_t *s);
void start_adopt_all_thread(app_state_t *s);
void start_copy_install_thread(app_state_t *s, const char *tool_skills_path,
                               const char *skill_name, const char *tool_name, int tool_idx);
void start_sync_master_to_tool_thread(app_state_t *s, const char *tool_skills_path,
                                     const char *skill_name, const char *tool_name, int tool_idx);
void start_promote_to_master_thread(app_state_t *s, const char *tool_skills_path,
                                    const char *skill_name, const char *tool_name, int tool_idx);
void start_broadcast_thread(app_state_t *s, const char *skill_name, int tool_idx);
void start_distribute_all_thread(app_state_t *s);
void start_distribute_skill_to_all_thread(app_state_t *s, const char *skill_name);

void app_hub_tasks_poll(app_state_t *s);
void app_hub_tasks_shutdown(app_state_t *s);

#endif
