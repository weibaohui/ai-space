#ifndef VIEW_COMMON_H
#define VIEW_COMMON_H

#include <cui/cui.h>
#include "app_state.h"

/* Below this window width the workspace uses single-pane detail views. */
#define COMPACT_WORKSPACE_WIDTH 1160

#define ICON_TOOLS CUI_ICON_TERMINAL
#define ICON_SKILLS CUI_ICON_PACKAGE
#define ICON_MASTER_HUB CUI_ICON_ARCHIVE
#define ICON_SYNC CUI_ICON_REPEAT
#define ICON_COLLECT CUI_ICON_INBOX
#define ICON_COMPARE CUI_ICON_GIT_BRANCH
#define ICON_DISTRIBUTE CUI_ICON_SEND
#define ICON_OVERWRITE CUI_ICON_ARROW_DOWN

void apply_workspace_theme(cui_ctx *ui);
void render_workspace_heading(cui_ctx *ui, const char *icon, const char *title,
                              const char *subtitle);
void render_skill_token_badge(cui_ctx *ui, const skill_token_usage_t *tokens);

/* Render sidebar navigation, theme switch and accent palette */
void render_app_sidebar(cui_ctx *ui, app_state_t *s);

#endif /* VIEW_COMMON_H */
