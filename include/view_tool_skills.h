#ifndef VIEW_TOOL_SKILLS_H
#define VIEW_TOOL_SKILLS_H

#include <cui/cui.h>
#include "app_state.h"

/* Opens and initializes the skills browser view for a single tool */
void open_tool_skills_page(app_state_t *s, const char *tool_name, const char *tool_path);
void select_active_skill(app_state_t *s, const char *skill_name, const char *skill_path);

/* Renders the skills browser view (dual-pane tree + content viewer) */
void render_tool_skills_page(cui_ctx *ui, app_state_t *s);

#endif /* VIEW_TOOL_SKILLS_H */
