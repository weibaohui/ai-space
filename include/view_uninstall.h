#ifndef VIEW_UNINSTALL_H
#define VIEW_UNINSTALL_H

#include "app_state.h"

void render_uninstall_all_modal(cui_ctx *ui, app_state_t *s);
void render_uninstall_tool_modal(cui_ctx *ui, app_state_t *s);

/* 记录目标工具并弹出清空确认框，可在任意页面调用。 */
void request_clear_tool(app_state_t *s, const char *tool_name, const char *tool_path);
void render_clear_tool_modal(cui_ctx *ui, app_state_t *s);

#endif
