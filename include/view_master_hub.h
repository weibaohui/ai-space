#ifndef VIEW_MASTER_HUB_H
#define VIEW_MASTER_HUB_H

#include <cui/cui.h>
#include "app_state.h"

/* Render the Master Hub Skills List (subview 0) */
void render_master_hub_skills_view(cui_ctx *ui, app_state_t *s);

/* Render the Master Skill File Explorer (subview 3) */
void render_master_hub_explorer_view(cui_ctx *ui, app_state_t *s);

/* Open the Master Skill File Explorer for a given skill */
void open_master_skill_explorer(app_state_t *s, const char *skill_name);

#endif /* VIEW_MASTER_HUB_H */
