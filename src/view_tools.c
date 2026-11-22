#include "view_tools.h"
#include "view_tool_skills.h"
#include "view_common.h"
#include "db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void reload_db_data(app_state_t *s) {
    if (!s || !s->db) return;
    db_query_stats(s->db, &s->stats);

    if (s->records) {
        free(s->records);
        s->records = NULL;
        s->record_count = 0;
    }

    db_query_tools(s->db, &s->records, &s->record_count,
                   s->search_kw, s->sort.column, s->sort.descending);

    strncpy(s->last_search_kw, s->search_kw, sizeof(s->last_search_kw) - 1);
    s->last_sort_col = s->sort.column;
    s->last_sort_desc = s->sort.descending;
    s->needs_reload = false;
}

void do_scan_sync(app_state_t *s) {
    if (!s || !s->db) return;
    int inserted = 0, updated = 0, deleted = 0;
    if (db_scan_and_sync(s->db, &inserted, &updated, &deleted) == 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "扫描同步完成: 新增 %d 个, 更新 %d 个, 移除 %d 个",
                 inserted, updated, deleted);
        app_set_toast(s, msg);
    } else {
        app_set_toast(s, "扫描同步失败，请检查文件权限");
    }
    reload_db_data(s);
}

void render_tools_list_page(cui_ctx *ui, app_state_t *s) {
    if (strcmp(s->search_kw, s->last_search_kw) != 0 ||
        s->sort.column != s->last_sort_col ||
        s->sort.descending != s->last_sort_desc ||
        s->needs_reload) {
        reload_db_data(s);
    }

    /* 0. Toast counter */
    if (s->toast_frames > 0) {
        s->toast_frames--;
    }

    /* 1. Global Navigation Bar */
    render_global_navbar(ui, s);

    /* 2. Top Header Area */
    cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
        cui_row(ui, .gap = 12, .align = CUI_ALIGN_CENTER) {
            cui_box(ui, .width = CUI_PX(40), .height = CUI_PX(40), .radius = 10,
                    .bg = cui_alpha(cui_color_of(ui, CUI_COLOR_ACCENT), 0.15f),
                    .dir = CUI_STACK, .align = CUI_ALIGN_CENTER, .justify = CUI_JUSTIFY_CENTER) {
                cui_icon(ui, CUI_ICON_FOLDER, 22, CUI_COLOR_ACCENT);
            }
            cui_column(ui, .gap = 2) {
                cui_heading(ui, "本地工具", 3);
                cui_text(ui, "自动识别本机支持 Skills 的 AI 编程助手与扩展工具",
                         .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);
            }
        }

        cui_spacer(ui);

        /* Action Buttons: Only current page's core actions */
        cui_row(ui, .gap = 10, .align = CUI_ALIGN_CENTER) {
            if (cui_button(ui, "重新扫描", .variant = CUI_BUTTON_PRIMARY, .icon = CUI_ICON_REFRESH_CW)) {
                do_scan_sync(s);
            }
        }
    }

    /* 3. Statistical Metric Cards */
    cui_row(ui, .width = CUI_FILL, .gap = 14) {
        char buf_tot[32], buf_skl[32];
        snprintf(buf_tot, sizeof(buf_tot), "%d 个", s->stats.total_tools);
        snprintf(buf_skl, sizeof(buf_skl), "%d 项", s->stats.total_skills);

        cui_card(ui, .width = CUI_GROW(1), .pad = 16) {
            cui_metric(ui, "已识别工具", buf_tot, .icon = CUI_ICON_BOX);
        }
        cui_card(ui, .width = CUI_GROW(1), .pad = 16) {
            cui_metric(ui, "关联技能总数", buf_skl, .icon = CUI_ICON_LAYERS);
        }
        cui_card(ui, .width = CUI_GROW(1), .pad = 16) {
            char time_display[32];
            if (s->stats.last_scanned_at[0]) {
                const char *space = strchr(s->stats.last_scanned_at, ' ');
                if (space) {
                    snprintf(time_display, sizeof(time_display), "%s", space + 1);
                } else {
                    snprintf(time_display, sizeof(time_display), "%s", s->stats.last_scanned_at);
                }
            } else {
                snprintf(time_display, sizeof(time_display), "刚刚");
            }
            cui_metric(ui, "最近扫描时间", time_display, .icon = CUI_ICON_CLOCK);
        }
    }

    /* 4. Search Bar */
    cui_row(ui, .width = CUI_FILL, .gap = 12, .align = CUI_ALIGN_CENTER) {
        cui_search(ui, "tool_search_box", s->search_kw, sizeof(s->search_kw),
                   .placeholder = "按工具名或路径搜索...", .width = CUI_PX(320));

        cui_spacer(ui);

        char count_str[64];
        snprintf(count_str, sizeof(count_str), "显示 %d / %d 项", s->record_count, s->stats.total_tools);
        cui_text(ui, count_str, .size = 13, .color = CUI_COLOR_TEXT_SECONDARY);
    }

    /* 5. Main Data Table (Simplified to 4 essential columns) */
    static const cui_table_column cols[] = {
        { "工具名称", CUI_PX(200), CUI_TEXT_LEFT,   true  },
        { "技能数",   CUI_PX(100), CUI_TEXT_RIGHT,  true  },
        { "存储路径", CUI_FILL,   CUI_TEXT_LEFT,   false },
        { "操作",     CUI_PX(120), CUI_TEXT_CENTER, false },
    };

    cui_card(ui, .width = CUI_FILL, .height = CUI_FILL, .pad = 0, .gap = 0) {
        if (s->record_count == 0) {
            cui_column(ui, .width = CUI_FILL, .height = CUI_FILL,
                       .align = CUI_ALIGN_CENTER, .justify = CUI_JUSTIFY_CENTER) {
                if (cui_empty_state(ui, "暂无匹配的本地工具记录",
                                    .description = "点击下方按钮重新扫描系统目录",
                                    .icon = CUI_ICON_INBOX,
                                    .action = "立即扫描同步")) {
                    do_scan_sync(s);
                }
            }
        } else {
            cui_table(ui, "tools_grid", cols, 4,
                      .sort = &s->sort, .striped = true, .compact = true,
                      .selectable = true, .height = CUI_FILL) {
                for (int i = 0; i < s->record_count; i++) {
                    tool_record_t *r = &s->records[i];
                    cui_push_id_int(ui, r->id);
                    cui_table_row(ui, false);

                    /* Col 0: Tool Name */
                    cui_table_cell(ui);
                    cui_row(ui, .gap = 6, .align = CUI_ALIGN_CENTER) {
                        cui_icon(ui, CUI_ICON_FOLDER, 14, CUI_COLOR_ACCENT);
                        cui_text(ui, r->name, .font = CUI_FONT_MEDIUM);
                    }

                    /* Col 1: Skill Count */
                    cui_table_cell(ui);
                    char sc_buf[16];
                    snprintf(sc_buf, sizeof(sc_buf), "%d 项", r->skill_count);
                    cui_text(ui, sc_buf, .tabular = true,
                             .color = (r->skill_count > 0) ? CUI_COLOR_FOREGROUND : CUI_COLOR_TEXT_MUTED);

                    /* Col 2: Path */
                    cui_table_cell_text(ui, r->path);

                    /* Col 3: Action Button */
                    cui_table_cell(ui);
                    if (cui_button(ui, " 查看技能", .variant = CUI_BUTTON_OUTLINE,
                                   .size = CUI_SIZE_SM, .icon = CUI_ICON_FOLDER)) {
                        open_tool_skills_page(s, r->name, r->path);
                    }

                    cui_pop_id(ui);
                }
            }
        }
    }

    /* 6. Bottom Status Bar */
    cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
        cui_textf(ui, "数据库: %s", s->db_path);

        if (s->toast_frames > 0 && s->toast_msg[0] != '\0') {
            cui_text(ui, " · ", .color = CUI_COLOR_TEXT_MUTED);
            cui_text(ui, s->toast_msg, .color = CUI_COLOR_ACCENT);
        }

        cui_spacer(ui);

        if (s->stats.last_scanned_at[0] != '\0') {
            cui_textf(ui, "最后扫描: %s", s->stats.last_scanned_at);
        }
    }
}
