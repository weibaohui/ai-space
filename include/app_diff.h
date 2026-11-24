#ifndef APP_DIFF_H
#define APP_DIFF_H

#include "app_state.h"

void clear_diff_texts(app_state_t *s);
void build_skill_diff_manifest(app_state_t *s, const char *skill_name, int tool_idx);
void load_hub_diff(app_state_t *s, const char *skill_name, int tool_idx, const char *rel_file);
void open_hub_diff_view(app_state_t *s, const char *skill_name);

#endif
