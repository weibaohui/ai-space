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
    }
}

void render_hub_diff_subview(cui_ctx *ui, app_state_t *s) {
    hub_skill_entry_t *entry = NULL;
    for (int i = 0; i < s->hub_matrix.skill_count; i++) {
        if (strcmp(s->hub_matrix.skills[i].skill_name, s->selected_hub_skill) == 0) {
            entry = &s->hub_matrix.skills[i];
            break;
        }
    }
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

    /* Auto-load skill file manifest if not yet loaded */
    if (!s->diff_manifest_loaded || strcmp(s->diff_manifest.skill_name, entry->skill_name) != 0) {
        build_skill_diff_manifest(s, entry->skill_name, s->selected_hub_tool_idx);
        load_hub_diff(s, entry->skill_name, s->selected_hub_tool_idx, s->diff_file_rel);
    }

    /* Top Navigation Row */
    cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
        if (cui_button(ui, "返回", .variant = CUI_BUTTON_SECONDARY, .icon = CUI_ICON_CHEVRON_LEFT)) {
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
            cui_icon(ui, CUI_ICON_LAYERS, 18, CUI_COLOR_ACCENT);
        }

        cui_column(ui, .gap = 2, .width = CUI_PX(520)) {
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

    /* Main Body: Left Tool Copies (260px) | Middle Skill Files Tree (250px) | Right Diff View (Fill) */
    cui_row(ui, .width = CUI_FILL, .height = CUI_FILL, .gap = 12) {

        /* ---------------- Pane 1: Master Hub & Tool Copies (240px) ---------------- */
        cui_card(ui, .width = CUI_PX(240), .height = CUI_FILL, .pad = 10, .gap = 8) {

            /* 1. Master Hub Section */
            cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                cui_label(ui, "主版本库");
                cui_spacer(ui);
                if (entry->in_hub) {
                    cui_badge(ui, "基准", .tone = CUI_TONE_SUCCESS, .size = CUI_SIZE_SM);
                } else {
                    cui_badge(ui, "未收录", .tone = CUI_TONE_VIOLET, .size = CUI_SIZE_SM);
                }
            }

            cui_card(ui, .width = CUI_FILL, .pad = 8, .gap = 3, .muted = true) {
                cui_row(ui, .gap = 6, .align = CUI_ALIGN_CENTER) {
                    cui_icon(ui, CUI_ICON_ARCHIVE, 14, CUI_COLOR_ACCENT);
                    cui_text(ui, s->setting_hub_dir[0] ? s->setting_hub_dir : "~/.ai-space/skills", .size = 11, .font = CUI_FONT_MEDIUM);
                }
                if (entry->in_hub) {
                    char h_txt[64];
                    snprintf(h_txt, sizeof(h_txt), "指纹: %.12s...", entry->hub_hash);
                    cui_text(ui, h_txt, .size = 10, .color = CUI_COLOR_TEXT_SECONDARY);
                } else {
                    cui_text(ui, "未收录，点击工具采纳为基准",
                             .size = 10, .color = CUI_COLOR_TEXT_MUTED);
                }
            }

            cui_separator(ui);

            /* 2. Tool Copies Header */
            cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                cui_label(ui, "工具副本");
                cui_spacer(ui);
                char copies_cnt[32];
                snprintf(copies_cnt, sizeof(copies_cnt), "%d / %d 工具", entry->installed_count, entry->tool_count);
                cui_text(ui, copies_cnt, .size = 11, .color = CUI_COLOR_TEXT_SECONDARY);
            }

            /* Scrollable Tool Copies */
            cui_scroll(ui, .id = "diff_tools_scroll", .width = CUI_FILL, .height = CUI_FILL) {
                cui_column(ui, .width = CUI_FILL, .gap = 5) {
                    /* Section 1: Installed Tool Copies (Shown First & Clickable anywhere!) */
                    for (int t = 0; t < entry->tool_count; t++) {
                        tool_copy_status_t *tc = &entry->tool_copies[t];
                        if (!tc->exists) continue;
                        bool is_cur = (s->selected_hub_tool_idx == t);

                        cui_push_id_int(ui, t);

                        /* Make card directly clickable and compact */
                        bool card_clicked = cui_card_begin(ui, &(cui_card_opts){
                            .width = CUI_FILL,
                            .pad = 8,
                            .gap = 3,
                            .hoverable = true,
                            .selected = is_cur,
                        });

                        /* Tool Title & Status Badge */
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
                            } else if (tc->status == HUB_STATUS_OUTDATED) {
                                cui_badge(ui, "落后", .tone = CUI_TONE_INFO, .size = CUI_SIZE_SM);
                            } else {
                                cui_badge(ui, "独立", .tone = CUI_TONE_VIOLET, .size = CUI_SIZE_SM);
                            }
                        }

                        char tc_sub[64];
                        snprintf(tc_sub, sizeof(tc_sub), "指纹: %.10s... 点击选中", tc->hash);
                        cui_text(ui, tc_sub, .size = 10, .color = is_cur ? CUI_COLOR_ACCENT : CUI_COLOR_TEXT_SECONDARY);

                        cui_card_end(ui);

                        if (card_clicked) {
                            s->selected_hub_tool_idx = t;
                            build_skill_diff_manifest(s, entry->skill_name, t);
                            load_hub_diff(s, entry->skill_name, t, s->diff_file_rel);
                        }

                        cui_pop_id(ui);
                    }

                    /* Section 2: Uninstalled Tools */
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
            }
        }

        /* ---------------- Pane 2: 技能文件结构树 (复用统一组件) ---------------- */
        skill_tree_view_opts_t tree_opts = {
            .title = "技能文件结构树",
            .badge_label = s->diff_manifest.skill_name[0] ? s->diff_manifest.skill_name : NULL,
            .badge_tone = CUI_TONE_ACCENT,
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
            .width = 300,
        };
        render_skill_file_tree_component(ui, s->hub_diff_file_tree, &tree_opts, on_hub_diff_file_select, s);

        /* ---------------- Pane 3: Diff Viewer / Content Preview (Fill) ---------------- */
        cui_card(ui, .width = CUI_FILL, .height = CUI_FILL, .pad = 12, .gap = 8) {
            tool_copy_status_t *cur_tc = (s->selected_hub_tool_idx >= 0 && s->selected_hub_tool_idx < entry->tool_count) ?
                                         &entry->tool_copies[s->selected_hub_tool_idx] : NULL;

            hub_diff_file_item_t *cur_file = NULL;
            for (int i = 0; i < s->diff_manifest.count; i++) {
                if (strcmp(s->diff_manifest.items[i].rel_path, s->diff_file_rel) == 0) {
                    cur_file = &s->diff_manifest.items[i];
                    break;
                }
            }

            /* Diff Toolbar */
            cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                    cui_icon(ui, CUI_ICON_CODE, 16, CUI_COLOR_ACCENT);
                    cui_text(ui, "文件: ", .color = CUI_COLOR_TEXT_SECONDARY);
                    cui_badge(ui, s->diff_file_rel[0] ? s->diff_file_rel : "SKILL.md",
                              .tone = CUI_TONE_ACCENT, .size = CUI_SIZE_SM);

                    if (cur_file) {
                        if (cur_file->status == DIFF_FILE_IN_SYNC) {
                            cui_badge(ui, "两端内容完全一致", .tone = CUI_TONE_SUCCESS, .size = CUI_SIZE_SM);
                        } else if (cur_file->status == DIFF_FILE_MODIFIED) {
                            char m_info[64];
                            snprintf(m_info, sizeof(m_info), "+%d / -%d 行改动", cur_file->added_lines, cur_file->removed_lines);
                            cui_badge(ui, m_info, .tone = CUI_TONE_WARNING, .size = CUI_SIZE_SM);
                        } else if (cur_file->status == DIFF_FILE_ADDED) {
                            char a_info[64];
                            snprintf(a_info, sizeof(a_info), "+%d 行 (本地新增)", cur_file->added_lines);
                            cui_badge(ui, a_info, .tone = CUI_TONE_SUCCESS, .size = CUI_SIZE_SM);
                        } else if (cur_file->status == DIFF_FILE_DELETED) {
                            char d_info[64];
                            snprintf(d_info, sizeof(d_info), "-%d 行 (副本缺失)", cur_file->removed_lines);
                            cui_badge(ui, d_info, .tone = CUI_TONE_DANGER, .size = CUI_SIZE_SM);
                        }
                    }

                    if (cur_tc && cur_tc->exists) {
                        /* Direct sync actions on the diff toolbar */
                        if (cui_button(ui, "设为基准", .variant = CUI_BUTTON_OUTLINE,
                                       .size = CUI_SIZE_SM, .icon = CUI_ICON_CHECK)) {
                            start_promote_to_master_thread(s, cur_tc->tool_skills_path, entry->skill_name, cur_tc->tool_name, s->selected_hub_tool_idx);
                        }

                        if (entry->in_hub) {
                            if (cui_button(ui, "覆盖工具", .variant = CUI_BUTTON_SECONDARY,
                                           .size = CUI_SIZE_SM, .icon = CUI_ICON_DOWNLOAD)) {
                                start_sync_master_to_tool_thread(s, cur_tc->tool_skills_path, entry->skill_name, cur_tc->tool_name, s->selected_hub_tool_idx);
                            }
                        }
                    }
                }

                cui_spacer(ui);

                /* Mode Toggles */
                if (cur_file && cur_file->status == DIFF_FILE_MODIFIED) {
                    static const char *diff_modes[] = { "左右分屏", "单列内联" };
                    int d_mode_int = (int)s->diff_mode;
                    if (cui_tabs(ui, "diff_view_mode_tabs", diff_modes, 2, &d_mode_int)) {
                        s->diff_mode = (cui_diff_mode)d_mode_int;
                    }
                } else if (cur_file && cur_file->status == DIFF_FILE_IN_SYNC) {
                    static const char *sync_modes[] = { "文档预览", "双栏比对" };
                    cui_tabs(ui, "sync_view_mode_tabs", sync_modes, 2, &s->diff_sync_view_mode);
                }
            }

            cui_separator(ui);

            const char *old_txt = s->diff_old_text ? s->diff_old_text : "";
            const char *new_txt = s->diff_new_text ? s->diff_new_text : "";
            bool is_md = (strstr(s->diff_file_rel, ".md") != NULL || strstr(s->diff_file_rel, ".markdown") != NULL);

            if (cur_file && cur_file->status == DIFF_FILE_IN_SYNC) {
                if (s->diff_sync_view_mode == 1) {
                    /* Optional split comparison grid even for in-sync files */
                    char old_title_buf[256];
                    char new_title_buf[256];
                    snprintf(old_title_buf, sizeof(old_title_buf), "主版本库 (%s/%s)", entry->hub_path, s->diff_file_rel);
                    if (cur_tc && cur_tc->exists) {
                        snprintf(new_title_buf, sizeof(new_title_buf), "工具副本【%s】 (%s/%s)", cur_tc->tool_name, cur_tc->skill_path, s->diff_file_rel);
                    } else {
                        snprintf(new_title_buf, sizeof(new_title_buf), "工具副本 (未安装)");
                    }

                    cui_diff_view(ui, "hub_diff_control_sync",
                                  old_txt, new_txt,
                                  .mode = s->diff_mode,
                                  .language = detect_syntax_lang(s->diff_file_rel),
                                  .old_title = old_title_buf,
                                  .new_title = new_title_buf,
                                  .current = &s->diff_current_change,
                                  .height = -1);

                    cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                        cui_icon(ui, CUI_ICON_CIRCLE_CHECK, 14, CUI_COLOR_SUCCESS);
                        cui_text(ui, "两端文件内容完全一致，无文本差异", .size = 11, .color = CUI_COLOR_SUCCESS);
                        cui_spacer(ui);
                        cui_text(ui, "覆盖前自动生成快照备份",
                                 .size = 11, .color = CUI_COLOR_TEXT_MUTED);
                    }
                } else {
                    /* Case 1: Identical - Show clean success card & full file preview! NO empty split diff! */
                    cui_card(ui, .width = CUI_FILL, .pad = 8, .gap = 4, .muted = true) {
                        cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                            cui_icon(ui, CUI_ICON_CIRCLE_CHECK, 15, CUI_COLOR_SUCCESS);
                            cui_text(ui, "主版本与当前工具副本内容完全一致，未发生分化",
                                     .font = CUI_FONT_MEDIUM, .size = 12, .color = CUI_COLOR_SUCCESS);
                            cui_spacer(ui);
                            if (cur_tc) {
                                cui_textf(ui, "%s · %zu B", cur_tc->tool_name, cur_file->tool_size);
                            }
                        }
                    }

                    if (is_md) {
                        cui_scroll(ui, .id = "sync_md_scroll", .width = CUI_FILL, .height = CUI_FILL) {
                            cui_markdown(ui, "sync_md_view", new_txt[0] ? new_txt : old_txt);
                        }
                    } else {
                        const char *lang = detect_syntax_lang(s->diff_file_rel);
                        cui_scroll(ui, .id = "sync_code_scroll", .width = CUI_FILL, .height = CUI_FILL) {
                            cui_code_block(ui, "sync_code_view", new_txt[0] ? new_txt : old_txt,
                                           .lang = lang, .line_numbers = true);
                        }
                    }
                }

            } else if (cur_file && cur_file->status == DIFF_FILE_ADDED) {
                /* Case 2: Added only in Tool */
                cui_card(ui, .width = CUI_FILL, .pad = 8, .gap = 4, .muted = true) {
                    cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                        cui_icon(ui, CUI_ICON_INFO, 15, CUI_COLOR_ACCENT);
                        cui_text(ui, "此文件仅在本地工具副本中新增，主版本库中尚未收录",
                                 .font = CUI_FONT_MEDIUM, .size = 12, .color = CUI_COLOR_ACCENT);
                        cui_spacer(ui);
                        cui_badge(ui, "+ 本地独有", .tone = CUI_TONE_SUCCESS, .size = CUI_SIZE_SM);
                    }
                }

                if (is_md) {
                    cui_scroll(ui, .id = "add_md_scroll", .width = CUI_FILL, .height = CUI_FILL) {
                        cui_markdown(ui, "add_md_view", new_txt);
                    }
                } else {
                    const char *lang = detect_syntax_lang(s->diff_file_rel);
                    cui_scroll(ui, .id = "add_code_scroll", .width = CUI_FILL, .height = CUI_FILL) {
                        cui_code_block(ui, "add_code_view", new_txt, .lang = lang, .line_numbers = true);
                    }
                }

            } else if (cur_file && cur_file->status == DIFF_FILE_DELETED) {
                /* Case 3: In Master, missing in Tool */
                cui_card(ui, .width = CUI_FILL, .pad = 8, .gap = 4, .muted = true) {
                    cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                        cui_icon(ui, CUI_ICON_TRIANGLE_ALERT, 15, CUI_COLOR_DANGER);
                        cui_text(ui, "此文件在主版本库中存在，但当前工具副本中已缺失",
                                 .font = CUI_FONT_MEDIUM, .size = 12, .color = CUI_COLOR_DANGER);
                        cui_spacer(ui);
                        cui_badge(ui, "- 工具缺失", .tone = CUI_TONE_DANGER, .size = CUI_SIZE_SM);
                    }
                }

                if (is_md) {
                    cui_scroll(ui, .id = "del_md_scroll", .width = CUI_FILL, .height = CUI_FILL) {
                        cui_markdown(ui, "del_md_view", old_txt);
                    }
                } else {
                    const char *lang = detect_syntax_lang(s->diff_file_rel);
                    cui_scroll(ui, .id = "del_code_scroll", .width = CUI_FILL, .height = CUI_FILL) {
                        cui_code_block(ui, "del_code_view", old_txt, .lang = lang, .line_numbers = true);
                    }
                }

            } else {
                /* Case 4: Modified - Show split diff! */
                char old_title_buf[256];
                char new_title_buf[256];
                snprintf(old_title_buf, sizeof(old_title_buf), "主版本库 (%s/%s)", entry->hub_path, s->diff_file_rel);
                if (cur_tc && cur_tc->exists) {
                    snprintf(new_title_buf, sizeof(new_title_buf), "工具副本【%s】 (%s/%s)", cur_tc->tool_name, cur_tc->skill_path, s->diff_file_rel);
                } else {
                    snprintf(new_title_buf, sizeof(new_title_buf), "工具副本 (未安装)");
                }

                cui_diff_result r = cui_diff_view(ui, "hub_diff_control",
                                                  old_txt, new_txt,
                                                  .mode = s->diff_mode,
                                                  .language = detect_syntax_lang(s->diff_file_rel),
                                                  .old_title = old_title_buf,
                                                  .new_title = new_title_buf,
                                                  .current = &s->diff_current_change,
                                                  .height = -1);

                /* Bottom Status Footer */
                cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                    if (r.changes > 0) {
                        cui_textf(ui, "+%d 行 / -%d 行 (共 %d 处差异)", r.added, r.removed, r.changes);
                    } else {
                        cui_badge(ui, "未检测到文本行差异", .tone = CUI_TONE_NEUTRAL, .size = CUI_SIZE_SM);
                    }

                    cui_spacer(ui);
                    cui_text(ui, "覆盖前自动生成快照备份",
                             .size = 11, .color = CUI_COLOR_TEXT_MUTED);
                }
            }
        }
    }
}
