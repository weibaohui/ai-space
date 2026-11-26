#ifndef APP_SYNC_H
#define APP_SYNC_H

#include "app_state.h"

void start_sync_check_thread(app_state_t *s);
void start_sync_check_available_thread(app_state_t *s, bool activate_on_success);
void start_sync_pull_skill_thread(app_state_t *s, const char *skill_name);
void start_sync_push_skill_thread(app_state_t *s, const char *skill_name);
void start_sync_resolve_conflict_thread(app_state_t *s, const char *skill_name, bool pull);
void start_sync_git_merge_thread(app_state_t *s, const char *skill, bool publish);
void start_sync_git_merge_stage_thread(app_state_t *s, const char *skill);
void app_sync_discard_git_merge(app_state_t *s, const char *skill);
void app_sync_open_conflict(app_state_t *s, const char *skill);
void app_sync_refresh_git_drafts(app_state_t *s);
void start_sync_pull_all_thread(app_state_t *s);
void start_sync_push_all_thread(app_state_t *s);
void app_sync_use_local_mode(app_state_t *s);
void app_sync_poll(app_state_t *s);
void app_sync_shutdown(app_state_t *s);
void app_sync_check_now(app_state_t *s);

#endif
