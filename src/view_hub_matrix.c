#include "view_hub_matrix.h"
#include "view_common.h"
#include "view_hub_diff.h"
#include "view_master_hub.h"
#include <stdio.h>
#include <string.h>

/* ========================================================================== */
/* Page 1: 技能列表 (All Skills - 全网全景大盘)                                  */
/* ========================================================================== */
void render_all_skills_page(cui_ctx *ui, app_state_t *s) {
    if (s->toast_frames > 0) {
        s->toast_frames--;
    }

    /* 1. Global Navigation Bar */
    render_global_navbar(ui, s);

    /* 2. Check Subview: Diff View */
    if (s->hub_subview == HUB_SUBVIEW_DIFF) {
        render_hub_diff_subview(ui, s);
        return;
    }

    /* 3. Loading State with Radar Spinner & Progress */
    if (s->hub_scan.is_scanning || !s->hub_matrix_loaded) {
        if (!s->hub_scan.is_scanning && !s->hub_matrix_loaded) {
            start_hub_scan_thread(s);
        }

        cui_card(ui, .width = CUI_FILL, .height = CUI_FILL, .pad = 36) {
            cui_column(ui, .width = CUI_FILL, .height = CUI_FILL,
                       .align = CUI_ALIGN_CENTER, .justify = CUI_JUSTIFY_CENTER) {
                cui_spinner(ui, 48, .style = CUI_SPINNER_RADAR, .color = CUI_COLOR_ACCENT);
                cui_gap(ui, 18);
                cui_heading(ui, "正在扫描全局工具技能分布...", 3);
                cui_text(ui, "正在计算本机所有 AI 工具中技能的内容指纹并对比状态",
                         .size = 13, .color = CUI_COLOR_TEXT_SECONDARY);
                cui_gap(ui, 18);
                cui_progress(ui, s->hub_scan.progress,
                             .width = CUI_PX(520),
                             .label = s->hub_scan.status_text[0] ? s->hub_scan.status_text : "分析中...",
                             .show_value = true,
                             .tone = CUI_TONE_ACCENT);
            }
        }
        return;
    }

    /* 4. Top Header Area */
    cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
        cui_row(ui, .gap = 12, .align = CUI_ALIGN_CENTER) {
            cui_box(ui, .width = CUI_PX(40), .height = CUI_PX(40), .radius = 10,
                    .bg = cui_alpha(cui_color_of(ui, CUI_COLOR_ACCENT), 0.15f),
                    .dir = CUI_STACK, .align = CUI_ALIGN_CENTER, .justify = CUI_JUSTIFY_CENTER) {
                cui_icon(ui, CUI_ICON_PACKAGE, 22, CUI_COLOR_ACCENT);
            }
            cui_column(ui, .gap = 2) {
                cui_heading(ui, "技能列表 (Skills)", 3);
                cui_text(ui, "本机发现的所有技能全景大盘与工具分布",
                         .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);
            }
        }

        cui_spacer(ui);

        /* Action Buttons: Only current page's actions */
        cui_row(ui, .gap = 10, .align = CUI_ALIGN_CENTER) {
            if (cui_button(ui, " 刷新扫描", .variant = CUI_BUTTON_OUTLINE, .icon = CUI_ICON_REFRESH_CW)) {
                start_hub_scan_thread(s);
            }
            if (cui_button(ui, " 一键全量收录主库", .variant = CUI_BUTTON_PRIMARY, .icon = CUI_ICON_CHECK)) {
                start_adopt_all_thread(s);
            }
        }
    }

    cui_separator(ui);

    /* 5. Metric KPI Cards (4 Clear Core Metrics) */
    cui_row(ui, .width = CUI_FILL, .gap = 14) {
        char buf_uniq[32], buf_hub[32], buf_unman[32], buf_drift[32];
        int unman_count = s->hub_matrix.total_unique_skills - s->hub_matrix.hub_managed_skills;
        if (unman_count < 0) unman_count = 0;

        snprintf(buf_uniq, sizeof(buf_uniq), "%d 个", s->hub_matrix.total_unique_skills);
        snprintf(buf_hub, sizeof(buf_hub), "%d 个", s->hub_matrix.hub_managed_skills);
        snprintf(buf_unman, sizeof(buf_unman), "%d 个", unman_count);
        snprintf(buf_drift, sizeof(buf_drift), "%d 个", s->hub_matrix.drifted_skills_count);

        cui_card(ui, .width = CUI_GROW(1), .pad = 16) {
            cui_metric(ui, "独立技能总数", buf_uniq, .icon = CUI_ICON_LAYERS);
        }
        cui_card(ui, .width = CUI_GROW(1), .pad = 16) {
            cui_metric(ui, "主仓已收录", buf_hub, .icon = CUI_ICON_CIRCLE_CHECK);
        }
        cui_card(ui, .width = CUI_GROW(1), .pad = 16) {
            cui_metric(ui, "未收录主仓", buf_unman, .icon = CUI_ICON_PLUS_CIRCLE);
        }
        cui_card(ui, .width = CUI_GROW(1), .pad = 16) {
            cui_metric(ui, "存在版本分化", buf_drift, .icon = CUI_ICON_TRIANGLE_ALERT);
        }
    }

    /* 6. Search and Filter Tabs */
    cui_row(ui, .width = CUI_FILL, .gap = 12, .align = CUI_ALIGN_CENTER) {
        cui_search(ui, "hub_search_kw_box", s->hub_search_kw, sizeof(s->hub_search_kw),
                   .placeholder = "按技能名或描述搜索...", .width = CUI_PX(280));

        cui_row(ui, .gap = 6, .align = CUI_ALIGN_CENTER) {
            char all_lbl[64];
            snprintf(all_lbl, sizeof(all_lbl), "全部 (%d)", s->hub_matrix.total_unique_skills);
            if (cui_button(ui, all_lbl,
                           .variant = (s->hub_filter_mode == 0) ? CUI_BUTTON_PRIMARY : CUI_BUTTON_SECONDARY,
                           .size = CUI_SIZE_SM)) {
                s->hub_filter_mode = 0;
            }
            char drift_lbl[64];
            snprintf(drift_lbl, sizeof(drift_lbl), "存在分化 (%d)", s->hub_matrix.drifted_skills_count);
            if (cui_button(ui, drift_lbl,
                           .variant = (s->hub_filter_mode == 1) ? CUI_BUTTON_PRIMARY : CUI_BUTTON_SECONDARY,
                           .size = CUI_SIZE_SM)) {
                s->hub_filter_mode = 1;
            }
            int unman_cnt = s->hub_matrix.total_unique_skills - s->hub_matrix.hub_managed_skills;
            if (unman_cnt < 0) unman_cnt = 0;
            char unman_lbl[64];
            snprintf(unman_lbl, sizeof(unman_lbl), "未收录 (%d)", unman_cnt);
            if (cui_button(ui, unman_lbl,
                           .variant = (s->hub_filter_mode == 2) ? CUI_BUTTON_PRIMARY : CUI_BUTTON_SECONDARY,
                           .size = CUI_SIZE_SM)) {
                s->hub_filter_mode = 2;
            }
            if (cui_button(ui, "完全一致",
                           .variant = (s->hub_filter_mode == 3) ? CUI_BUTTON_PRIMARY : CUI_BUTTON_SECONDARY,
                           .size = CUI_SIZE_SM)) {
                s->hub_filter_mode = 3;
            }
        }
    }

    /* 7. Main Matrix Table (4 Consolidated Columns) */
    static const cui_table_column hub_cols[] = {
        { "技能名称与描述", CUI_PX(280), CUI_TEXT_LEFT,   true  },
        { "主仓状态",       CUI_PX(130), CUI_TEXT_LEFT,   true  },
        { "工具分布与状态", CUI_FILL,   CUI_TEXT_LEFT,   false },
        { "操作",           CUI_PX(180), CUI_TEXT_CENTER, false },
    };

    /* Count filtered items */
    int visible_count = 0;
    for (int i = 0; i < s->hub_matrix.skill_count; i++) {
        hub_skill_entry_t *e = &s->hub_matrix.skills[i];
        if (s->hub_search_kw[0] != '\0' &&
            !ci_contains(e->skill_name, s->hub_search_kw) &&
            !ci_contains(e->description, s->hub_search_kw)) {
            continue;
        }
        if (s->hub_filter_mode == 1 && e->modified_count == 0 && e->outdated_count == 0 && e->in_hub) {
            continue;
        }
        if (s->hub_filter_mode == 2 && e->in_hub) {
            continue;
        }
        if (s->hub_filter_mode == 3 && (!e->in_hub || e->modified_count > 0 || e->outdated_count > 0)) {
            continue;
        }
        visible_count++;
    }

    cui_card(ui, .width = CUI_FILL, .height = CUI_FILL, .pad = 0, .gap = 0) {
        if (visible_count == 0) {
            cui_column(ui, .width = CUI_FILL, .height = CUI_FILL,
                       .align = CUI_ALIGN_CENTER, .justify = CUI_JUSTIFY_CENTER) {
                if (cui_empty_state(ui, "未找到符合条件的技能项",
                                    .description = "切换上方筛选标签或点击刷新扫描",
                                    .icon = CUI_ICON_INBOX,
                                    .action = "刷新扫描")) {
                    start_hub_scan_thread(s);
                }
            }
        } else {
            cui_table(ui, "hub_matrix_table", hub_cols, 4,
                      .striped = true, .compact = true, .selectable = true, .height = CUI_FILL) {
                for (int i = 0; i < s->hub_matrix.skill_count; i++) {
                    hub_skill_entry_t *e = &s->hub_matrix.skills[i];

                    /* Filter checks */
                    if (s->hub_search_kw[0] != '\0' &&
                        !ci_contains(e->skill_name, s->hub_search_kw) &&
                        !ci_contains(e->description, s->hub_search_kw)) {
                        continue;
                    }
                    if (s->hub_filter_mode == 1 && e->modified_count == 0 && e->outdated_count == 0 && e->in_hub) {
                        continue;
                    }
                    if (s->hub_filter_mode == 2 && e->in_hub) {
                        continue;
                    }
                    if (s->hub_filter_mode == 3 && (!e->in_hub || e->modified_count > 0 || e->outdated_count > 0)) {
                        continue;
                    }

                    cui_push_id_int(ui, i);
                    cui_table_row(ui, false);

                    /* Col 0: Skill Name & Description */
                    cui_table_cell(ui);
                    cui_column(ui, .gap = 2) {
                        cui_row(ui, .gap = 6, .align = CUI_ALIGN_CENTER) {
                            cui_icon(ui, CUI_ICON_PACKAGE, 14, CUI_COLOR_ACCENT);
                            cui_text(ui, e->skill_name, .font = CUI_FONT_MEDIUM);
                        }
                        if (e->description[0] != '\0') {
                            cui_text(ui, e->description, .size = 11, .color = CUI_COLOR_TEXT_SECONDARY);
                        }
                    }

                    /* Col 1: Master Hub Status */
                    cui_table_cell(ui);
                    if (!e->in_hub) {
                        cui_badge(ui, "未收录", .tone = CUI_TONE_VIOLET, .size = CUI_SIZE_SM);
                    } else if (e->modified_count > 0) {
                        char mod_buf[32];
                        snprintf(mod_buf, sizeof(mod_buf), "%d处改动", e->modified_count);
                        cui_badge(ui, mod_buf, .tone = CUI_TONE_WARNING, .size = CUI_SIZE_SM);
                    } else if (e->outdated_count > 0) {
                        char out_buf[32];
                        snprintf(out_buf, sizeof(out_buf), "%d处待更新", e->outdated_count);
                        cui_badge(ui, out_buf, .tone = CUI_TONE_INFO, .size = CUI_SIZE_SM);
                    } else {
                        cui_badge(ui, "已收录", .tone = CUI_TONE_SUCCESS, .size = CUI_SIZE_SM);
                    }

                    /* Col 2: Tool copies presence & drift relations (Merged with count) */
                    cui_table_cell(ui);
                    cui_row(ui, .gap = 6, .align = CUI_ALIGN_CENTER) {
                        int shown = 0;
                        for (int t = 0; t < e->tool_count; t++) {
                            tool_copy_status_t *tc = &e->tool_copies[t];
                            if (tc->exists) {
                                if (tc->status == HUB_STATUS_IN_SYNC) {
                                    cui_badge(ui, tc->tool_name, .tone = CUI_TONE_SUCCESS, .size = CUI_SIZE_SM);
                                } else if (tc->status == HUB_STATUS_LOCAL_MODIFIED) {
                                    char t_lbl[80];
                                    snprintf(t_lbl, sizeof(t_lbl), "%s*", tc->tool_name);
                                    cui_badge(ui, t_lbl, .tone = CUI_TONE_WARNING, .size = CUI_SIZE_SM);
                                } else if (tc->status == HUB_STATUS_OUTDATED) {
                                    char t_lbl[80];
                                    snprintf(t_lbl, sizeof(t_lbl), "%s↓", tc->tool_name);
                                    cui_badge(ui, t_lbl, .tone = CUI_TONE_INFO, .size = CUI_SIZE_SM);
                                } else {
                                    cui_badge(ui, tc->tool_name, .tone = CUI_TONE_VIOLET, .size = CUI_SIZE_SM);
                                }
                                shown++;
                            }
                        }
                        if (shown == 0) {
                            cui_text(ui, "未安装", .size = 12, .color = CUI_COLOR_TEXT_MUTED);
                        }
                    }

                    /* Col 3: Action Buttons */
                    cui_table_cell(ui);
                    cui_row(ui, .gap = 6, .align = CUI_ALIGN_CENTER) {
                        if (cui_button(ui, " 对比", .variant = CUI_BUTTON_OUTLINE,
                                       .size = CUI_SIZE_SM, .icon = CUI_ICON_SEARCH)) {
                            s->prev_page = PAGE_ALL_SKILLS;
                            s->prev_hub_subview = HUB_SUBVIEW_SCAN_MATRIX;
                            open_hub_diff_view(s, e->skill_name);
                        }

                        if (e->in_hub) {
                            if (cui_button(ui, " 分发...", .variant = CUI_BUTTON_SECONDARY,
                                           .size = CUI_SIZE_SM, .icon = CUI_ICON_SEND)) {
                                s->show_distribute_modal = true;
                                strncpy(s->distribute_skill_name, e->skill_name, sizeof(s->distribute_skill_name) - 1);
                                s->distribute_skill_name[sizeof(s->distribute_skill_name) - 1] = '\0';
                            }
                        } else {
                            if (cui_button(ui, " 收录主库", .variant = CUI_BUTTON_PRIMARY,
                                           .size = CUI_SIZE_SM, .icon = CUI_ICON_CHECK)) {
                                for (int t = 0; t < e->tool_count; t++) {
                                    if (e->tool_copies[t].exists) {
                                        start_promote_to_master_thread(s, e->tool_copies[t].tool_skills_path, e->skill_name, e->tool_copies[t].tool_name, t);
                                        break;
                                    }
                                }
                            }
                        }
                    }

                    cui_pop_id(ui);
                }
            }
        }
    }
}

/* ========================================================================== */
/* Page 2: 主仓管理 (Master Hub - 基准库管理与全景分发)                             */
/* ========================================================================== */
void render_hub_matrix_page(cui_ctx *ui, app_state_t *s) {
    if (s->toast_frames > 0) {
        s->toast_frames--;
    }

    /* 1. Global Navigation Bar */
    render_global_navbar(ui, s);

    /* 2. Check Subview: Diff View or Master Explorer View */
    if (s->hub_subview == HUB_SUBVIEW_DIFF) {
        render_hub_diff_subview(ui, s);
        return;
    }
    if (s->hub_subview == HUB_SUBVIEW_EXPLORER) {
        render_master_hub_explorer_view(ui, s);
        return;
    }

    /* 3. Loading State with Radar Spinner & Progress */
    if (s->hub_scan.is_scanning || !s->hub_matrix_loaded) {
        if (!s->hub_scan.is_scanning && !s->hub_matrix_loaded) {
            start_hub_scan_thread(s);
        }

        cui_card(ui, .width = CUI_FILL, .height = CUI_FILL, .pad = 36) {
            cui_column(ui, .width = CUI_FILL, .height = CUI_FILL,
                       .align = CUI_ALIGN_CENTER, .justify = CUI_JUSTIFY_CENTER) {
                cui_spinner(ui, 48, .style = CUI_SPINNER_RADAR, .color = CUI_COLOR_ACCENT);
                cui_gap(ui, 18);
                cui_heading(ui, "正在检查主仓基准版本...", 3);
                cui_text(ui, "正在比对主版本库与本地工具状态",
                         .size = 13, .color = CUI_COLOR_TEXT_SECONDARY);
                cui_gap(ui, 18);
                cui_progress(ui, s->hub_scan.progress,
                             .width = CUI_PX(520),
                             .label = s->hub_scan.status_text[0] ? s->hub_scan.status_text : "分析中...",
                             .show_value = true,
                             .tone = CUI_TONE_ACCENT);
            }
        }
        return;
    }

    /* 4. Top Header inside Master Hub Management */
    cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
        cui_row(ui, .gap = 12, .align = CUI_ALIGN_CENTER) {
            cui_box(ui, .width = CUI_PX(40), .height = CUI_PX(40), .radius = 10,
                    .bg = cui_alpha(cui_color_of(ui, CUI_COLOR_ACCENT), 0.15f),
                    .dir = CUI_STACK, .align = CUI_ALIGN_CENTER, .justify = CUI_JUSTIFY_CENTER) {
                cui_icon(ui, CUI_ICON_ARCHIVE, 22, CUI_COLOR_ACCENT);
            }
            cui_column(ui, .gap = 2) {
                cui_heading(ui, "主仓管理 (Master Hub)", 3);
                char hub_sub[256];
                snprintf(hub_sub, sizeof(hub_sub), "基准版本库 · %s",
                         s->setting_hub_dir[0] ? s->setting_hub_dir : "~/.ai-space/skills");
                cui_text(ui, hub_sub, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);
            }
        }
    }

    cui_separator(ui);

    /* 5. Render Master Hub Skills View */
    render_master_hub_skills_view(ui, s);
}
