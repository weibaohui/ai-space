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
    db_scan_and_sync(s->db, &inserted, &updated, &deleted);
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

    cui_row(ui, .width = CUI_FILL, .gap = 12, .align = CUI_ALIGN_CENTER) {
        char scan_status[80];
        snprintf(scan_status, sizeof(scan_status), "上次扫描  %s",
                 s->stats.last_scanned_at[0] ? s->stats.last_scanned_at : "尚无记录");
        render_workspace_heading(ui, ICON_TOOLS, "工具列表", scan_status);
        if (cui_button(ui, "重新扫描", .variant = CUI_BUTTON_GHOST, .icon = CUI_ICON_REFRESH_CW)) {
            do_scan_sync(s);
        }
    }

    cui_row(ui, .width = CUI_FILL, .gap = 12) {
        char tools[24], skills[24];
        snprintf(tools, sizeof(tools), "%d", s->stats.total_tools);
        snprintf(skills, sizeof(skills), "%d", s->stats.total_skills);
        cui_metric(ui, "识别到的工具", tools, .icon = ICON_TOOLS, .width = CUI_GROW(1));
        cui_metric(ui, "关联技能", skills, .icon = ICON_SKILLS, .width = CUI_GROW(1));
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
                    bool open_tool = cui_table_row(ui, false);

                    /* Col 0: Tool Name */
                    cui_table_cell(ui);
                    cui_row(ui, .gap = 6, .align = CUI_ALIGN_CENTER) {
                        cui_icon(ui, ICON_TOOLS, 14, CUI_COLOR_TEXT_SECONDARY);
                        cui_text(ui, r->name, .font = CUI_FONT_MEDIUM);
                    }

                    /* Col 1: Skill Count */
                    cui_table_cell(ui);
                    char sc_buf[16];
                    snprintf(sc_buf, sizeof(sc_buf), "%d 项", r->skill_count);
                    cui_text(ui, sc_buf, .tabular = true,
                             .color = (r->skill_count > 0) ? CUI_COLOR_FOREGROUND : CUI_COLOR_TEXT_MUTED);

                    /* Col 2: Path */
                    cui_table_cell(ui);
                    cui_text(ui, r->path, .size = 12, .nowrap = true, .width = CUI_FILL,
                             .color = CUI_COLOR_TEXT_SECONDARY);
                    cui_tooltip(ui, r->path);

                    /* Col 3: Action Button */
                    cui_table_cell(ui);
                    if (cui_button(ui, "查看技能", .variant = CUI_BUTTON_GHOST,
                                   .size = CUI_SIZE_SM, .icon_right = CUI_ICON_CHEVRON_RIGHT) || open_tool) {
                        open_tool_skills_page(s, r->name, r->path);
                    }

                    cui_pop_id(ui);
                }
            }
        }
    }
}
