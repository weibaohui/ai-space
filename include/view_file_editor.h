#ifndef VIEW_FILE_EDITOR_H
#define VIEW_FILE_EDITOR_H

#include <cui/cui.h>
#include "app_state.h"

/* 加载文件到预览/编辑缓冲区（忽略未保存拦截，用于确认后或初次打开）。 */
void app_load_skill_file_force(app_state_t *s, const char *path, const char *name);

/* 切换选中文件；若当前存在未保存修改则弹出确认框。 */
void app_select_skill_file(app_state_t *s, const char *path, const char *name);

/* 原子保存当前编辑中的文件，并刷新 Token 与主仓状态。 */
bool app_save_active_skill_file(cui_ctx *ui, app_state_t *s);

/* 渲染右侧文件预览/编辑面板（支持预览、编辑、实时 Token 与状态栏）。 */
void render_skill_file_pane(cui_ctx *ui, app_state_t *s, const char *scope_id, const char *empty_hint);

/* 渲染未保存修改确认弹窗。 */
void render_unsaved_file_modal(cui_ctx *ui, app_state_t *s);

#endif /* VIEW_FILE_EDITOR_H */
