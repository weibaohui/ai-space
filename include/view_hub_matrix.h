#ifndef VIEW_HUB_MATRIX_H
#define VIEW_HUB_MATRIX_H

#include <cui/cui.h>
#include "app_state.h"

/* Renders the All Skills (技能列表 - 全网全景大盘) page */
void render_all_skills_page(cui_ctx *ui, app_state_t *s);

/* Renders the Master Hub (主仓管理 - 基准库管理与分发) page */
void render_hub_matrix_page(cui_ctx *ui, app_state_t *s);

#endif /* VIEW_HUB_MATRIX_H */
