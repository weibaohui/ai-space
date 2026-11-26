#include "view_hub_diff_preview.h"
#include "app_hub_tasks.h"
#include "app_diff.h"
#include "view_hub_diff.h"
#include "view_common.h"
#include "view_skill_tree.h"
#include <stdio.h>
#include <string.h>

static void on_hub_diff_file_select(file_node_t *node, void *user_data) {
    app_state_t *s = (app_state_t *)user_data;
    if (s && node && !node->is_dir) {
        strncpy(s->diff_file_rel, node->rel_path, sizeof(s->diff_file_rel) - 1);
        s->diff_file_rel[sizeof(s->diff_file_rel) - 1] = '\0';
        load_hub_diff(s, s->selected_hub_skill, s->selected_hub_tool_idx, s->diff_file_rel);
        s->diff_detail_tab = 0;
    }
}

static void render_diff_heading(cui_ctx *ui, app_state_t *s, const hub_skill_entry_t *entry) {
    cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
        if (cui_button(ui, "返回", .variant = CUI_BUTTON_GHOST, .icon = CUI_ICON_CHEVRON_LEFT)) {
            if (s->prev_page == PAGE_ALL_SKILLS) {
                s->current_page = PAGE_ALL_SKILLS;
            } else if (s->prev_page == PAGE_HUB_MATRIX) {
                s->current_page = PAGE_HUB_MATRIX;
            }
            s->hub_subview = HUB_SUBVIEW_MASTER_SKILLS;
        }

        cui_gap(ui, 12);

        cui_box(ui, .width = CUI_PX(36), .height = CUI_PX(36), .radius = 8,
                .bg = cui_alpha(cui_color_of(ui, CUI_COLOR_ACCENT), 0.15f),
                .dir = CUI_STACK, .align = CUI_ALIGN_CENTER, .justify = CUI_JUSTIFY_CENTER) {
            cui_icon(ui, ICON_COMPARE, 18, CUI_COLOR_ACCENT);
        }

        cui_column(ui, .gap = 2, .width = CUI_FILL) {
            cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                char t_title[128];
                snprintf(t_title, sizeof(t_title), "技能对比: %s", entry->skill_name);
                cui_heading(ui, t_title, 4);

                if (!entry->in_hub) {
                    cui_badge(ui, "未建立主版本", .tone = CUI_TONE_VIOLET, .size = CUI_SIZE_SM);
                } else if (entry->modified_count > 0) {
                    cui_badge(ui, "存在分支分化", .tone = CUI_TONE_WARNING, .size = CUI_SIZE_SM);
                } else if (entry->outdated_count > 0) {
                    cui_badge(ui, "副本待更新", .tone = CUI_TONE_INFO, .size = CUI_SIZE_SM);
                } else {
                    cui_badge(ui, "全副本一致", .tone = CUI_TONE_SUCCESS, .size = CUI_SIZE_SM);
                }
            }
            if (entry->description[0] != '\0') {
                char short_desc[100];
                strncpy(short_desc, entry->description, sizeof(short_desc) - 1);
                short_desc[sizeof(short_desc) - 1] = '\0';
                if (strlen(entry->description) > 80) {
                    strcpy(short_desc + 77, "...");
                }
                cui_text(ui, short_desc, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);
            }
        }
    }
}

static void render_baseline_summary(cui_ctx *ui, app_state_t *s, const hub_skill_entry_t *entry) {
    cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
        cui_label(ui, "主版本库");
        cui_spacer(ui);
        if (entry->hub_unreadable) {
            cui_badge(ui, "读取失败", .tone = CUI_TONE_WARNING, .size = CUI_SIZE_SM);
        } else if (entry->in_hub) {
            cui_badge(ui, "基准", .tone = CUI_TONE_SUCCESS, .size = CUI_SIZE_SM);
        } else {
            cui_badge(ui, "未收录", .tone = CUI_TONE_VIOLET, .size = CUI_SIZE_SM);
        }
    }

    cui_card(ui, .width = CUI_FILL, .pad = 8, .gap = 3, .muted = true) {
        cui_row(ui, .gap = 6, .align = CUI_ALIGN_CENTER) {
            cui_icon(ui, CUI_ICON_ARCHIVE, 14, CUI_COLOR_ACCENT);
            cui_text(ui, s->setting_hub_dir[0] ? s->setting_hub_dir : "~/.skillboard/skills", .size = 11, .font = CUI_FONT_MEDIUM);
        }
        if (entry->hub_unreadable) {
            cui_text(ui, "无法读取主仓指纹", .size = 10, .color = CUI_COLOR_TEXT_SECONDARY);
        } else if (entry->in_hub) {
            char h_txt[64];
            snprintf(h_txt, sizeof(h_txt), "指纹: %.12s...", entry->hub_hash);
            cui_text(ui, h_txt, .size = 10, .color = CUI_COLOR_TEXT_SECONDARY);
        } else {
            cui_text(ui, "未收录，点击工具采纳为基准",
                     .size = 10, .color = CUI_COLOR_TEXT_MUTED);
        }
    }

    cui_separator(ui);
}

static void render_installed_copy(cui_ctx *ui, app_state_t *s, const hub_skill_entry_t *entry, int t) {
    tool_copy_status_t *tc = &entry->tool_copies[t];
    if (!tc->exists) return;
    bool is_cur = (s->selected_hub_tool_idx == t);

    cui_push_id_int(ui, t);

    /* 将整张工具卡片作为选中区域。 */
    bool card_clicked = cui_card_begin(ui, &(cui_card_opts){
        .width = CUI_FILL,
        .pad = 8,
        .gap = 3,
        .hoverable = true,
        .selected = is_cur,
    });

    /* 工具名称和副本状态共用一行。 */
    cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
        cui_row(ui, .gap = 5, .align = CUI_ALIGN_CENTER) {
            cui_icon(ui, CUI_ICON_FOLDER, 13, is_cur ? CUI_COLOR_ACCENT : CUI_COLOR_FOREGROUND);
            cui_text(ui, tc->tool_name, .size = 12, .font = CUI_FONT_MEDIUM);
        }
        cui_spacer(ui);

        if (tc->status == HUB_STATUS_IN_SYNC) {
            cui_badge(ui, "一致", .tone = CUI_TONE_SUCCESS, .size = CUI_SIZE_SM);
        } else if (tc->status == HUB_STATUS_LOCAL_MODIFIED) {
            cui_badge(ui, "改动", .tone = CUI_TONE_WARNING, .size = CUI_SIZE_SM);
        } else if (tc->status == HUB_STATUS_UNREADABLE) {
            cui_badge(ui, "无法比较", .tone = CUI_TONE_WARNING, .size = CUI_SIZE_SM);
        } else if (tc->status == HUB_STATUS_OUTDATED) {
            cui_badge(ui, "落后", .tone = CUI_TONE_INFO, .size = CUI_SIZE_SM);
        } else {
            cui_badge(ui, "独立", .tone = CUI_TONE_VIOLET, .size = CUI_SIZE_SM);
        }
    }

    char tc_sub[64];
    if (tc->hash[0]) snprintf(tc_sub, sizeof(tc_sub), "指纹: %.10s... 点击选中", tc->hash);
    else snprintf(tc_sub, sizeof(tc_sub), "指纹不可用");
    cui_text(ui, tc_sub, .size = 10, .color = is_cur ? CUI_COLOR_ACCENT : CUI_COLOR_TEXT_SECONDARY);

    cui_card_end(ui);

    if (card_clicked) {
        s->selected_hub_tool_idx = t;
        build_skill_diff_manifest(s, entry->skill_name, t);
        load_hub_diff(s, entry->skill_name, t, s->diff_file_rel);
        s->diff_detail_tab = 0;
    }

    cui_pop_id(ui);
}

static void render_uninstalled_copies(cui_ctx *ui, app_state_t *s, const hub_skill_entry_t *entry) {
    int uninstalled_count = entry->tool_count - entry->installed_count;
    if (uninstalled_count > 0) {
        cui_gap(ui, 4);
        cui_separator(ui);
        char unins_hdr[64];
        snprintf(unins_hdr, sizeof(unins_hdr), "未安装的工具 (%d)", uninstalled_count);
        cui_text(ui, unins_hdr, .size = 11, .color = CUI_COLOR_TEXT_MUTED);

        for (int t = 0; t < entry->tool_count; t++) {
            tool_copy_status_t *tc = &entry->tool_copies[t];
            if (tc->exists) continue;

            cui_push_id_int(ui, 1000 + t);
            cui_card(ui, .width = CUI_FILL, .pad = 6, .gap = 3, .muted = true) {
                cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                    cui_row(ui, .gap = 5, .align = CUI_ALIGN_CENTER) {
                        cui_icon(ui, CUI_ICON_FOLDER, 13, CUI_COLOR_TEXT_MUTED);
                        cui_text(ui, tc->tool_name, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);
                    }
                    cui_spacer(ui);
                    if (entry->in_hub) {
                        if (cui_button(ui, "安装",
                                       .variant = CUI_BUTTON_OUTLINE,
                                       .size = CUI_SIZE_SM, .icon = CUI_ICON_PLUS)) {
                            start_copy_install_thread(s, tc->tool_skills_path, entry->skill_name, tc->tool_name, t);
                        }
                    } else {
                        cui_badge(ui, "未装", .tone = CUI_TONE_NEUTRAL, .size = CUI_SIZE_SM);
                    }
                }
            }
            cui_pop_id(ui);
        }
    }
}

static void render_diff_copies(cui_ctx *ui, app_state_t *s, const hub_skill_entry_t *entry, bool compact) {
    cui_card(ui, .width = compact ? CUI_FILL : CUI_PX(208), .height = CUI_FILL, .pad = 10, .gap = 8) {
        render_baseline_summary(ui, s, entry);

        cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
            cui_label(ui, "工具副本");
            cui_spacer(ui);
            char copies_cnt[32];
            snprintf(copies_cnt, sizeof(copies_cnt), "%d / %d 工具", entry->installed_count, entry->tool_count);
            cui_text(ui, copies_cnt, .size = 11, .color = CUI_COLOR_TEXT_SECONDARY);
        }

        cui_scroll(ui, .id = "diff_tools_scroll", .width = CUI_FILL, .height = CUI_FILL) {
            cui_column(ui, .width = CUI_FILL, .gap = 5) {
                for (int t = 0; t < entry->tool_count; t++) render_installed_copy(ui, s, entry, t);
                render_uninstalled_copies(ui, s, entry);
            }
        }
    }
}

static void render_diff_tree(cui_ctx *ui, app_state_t *s, bool compact) {
    skill_tree_view_opts_t tree_opts = {
        .title = "技能文件结构树",
        .selected_path = s->diff_file_rel,
        .match_by_rel_path = true,
        .show_diff_badges = true,
        .show_summary_badges = true,
        .summary_total_files = s->diff_manifest.count,
        .summary_modified = s->diff_manifest.modified_count,
        .summary_added = s->diff_manifest.added_count,
        .summary_deleted = s->diff_manifest.deleted_count,
        .summary_total_added_lines = s->diff_manifest.total_added_lines,
        .summary_total_removed_lines = s->diff_manifest.total_removed_lines,
        .width = compact ? CUI_FILL : CUI_PX(220),
    };
    render_skill_file_tree_component(ui, s->hub_diff_file_tree, &tree_opts, on_hub_diff_file_select, s);
}

void render_hub_diff_subview(cui_ctx *ui, app_state_t *s) {
    const hub_skill_entry_t *entry = hub_find_skill(&s->hub_matrix, s->selected_hub_skill);
    if (!entry) {
        s->hub_subview = s->prev_hub_subview;
        return;
    }

    if (s->selected_hub_tool_idx < 0 || s->selected_hub_tool_idx >= entry->tool_count) {
        for (int t = 0; t < entry->tool_count; t++) {
            if (entry->tool_copies[t].exists) {
                s->selected_hub_tool_idx = t;
                break;
            }
        }
        if (s->selected_hub_tool_idx < 0 && entry->tool_count > 0) {
            s->selected_hub_tool_idx = 0;
        }
    }

    /* 当前清单与选中技能不匹配时重新加载。 */
    if (!s->diff_manifest_loaded || strcmp(s->diff_manifest.skill_name, entry->skill_name) != 0) {
        build_skill_diff_manifest(s, entry->skill_name, s->selected_hub_tool_idx);
        load_hub_diff(s, entry->skill_name, s->selected_hub_tool_idx, s->diff_file_rel);
    }

    render_diff_heading(ui, s, entry);

    bool compact = cui_viewport_size(ui).x < COMPACT_WORKSPACE_WIDTH;
    if (compact) {
        static const char *tabs[] = { "查看差异", "版本副本", "文件树" };
        cui_tabs(ui, "diff_detail_tabs", tabs, 3, &s->diff_detail_tab);
    }
    int tab = s->diff_detail_tab;

    cui_row(ui, .width = CUI_FILL, .height = CUI_FILL, .gap = 12) {

        if (!compact || tab == 1) render_diff_copies(ui, s, entry, compact);
        if (!compact || tab == 2) render_diff_tree(ui, s, compact);
        if (!compact || tab == 0) render_hub_diff_preview(ui, s, entry);
    }
}
