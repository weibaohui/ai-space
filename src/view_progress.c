#include "view_progress.h"

void render_op_progress_modal(cui_ctx *ui, app_state_t *s) {
    if (!s || !s->op_progress.active) return;

    const char *title = s->op_progress.title[0] ? s->op_progress.title : "操作处理中";
    cui_dialog(ui, title, &s->op_progress.active,
               .width = 480, .persistent = true, .no_close_button = true) {
        cui_column(ui, .width = CUI_FILL, .gap = 14, .pad = 6) {
            cui_row(ui, .gap = 10, .align = CUI_ALIGN_CENTER) {
                cui_spinner(ui, 22, .style = CUI_SPINNER_COMET, .color = CUI_COLOR_ACCENT);
                cui_text(ui, s->op_progress.message[0] ? s->op_progress.message : "正在执行...",
                         .size = 13, .color = CUI_COLOR_TEXT_SECONDARY);
            }

            cui_progress(ui, s->op_progress.percent,
                         .width = CUI_FILL,
                         .show_value = true,
                         .tone = CUI_TONE_ACCENT);

            cui_row(ui, .gap = 6, .align = CUI_ALIGN_CENTER) {
                cui_icon(ui, CUI_ICON_SHIELD, 13, CUI_COLOR_TEXT_MUTED);
                const char *sub = (s->op_progress.sub_message[0] != '\0')
                    ? s->op_progress.sub_message
                    : "正在进行纯物理复制，覆盖前已自动在 ~/.skillboard/backups/ 保存备份快照";
                cui_text(ui, sub, .size = 11, .color = CUI_COLOR_TEXT_MUTED);
            }
        }
    }
}
