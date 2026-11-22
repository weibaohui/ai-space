#include "view_hub_diff.h"
#include "view_common.h"
#include <stdio.h>
#include <string.h>

void render_hub_diff_subview(cui_ctx *ui, app_state_t *s) {
    hub_skill_entry_t *entry = NULL;
    for (int i = 0; i < s->hub_matrix.skill_count; i++) {
        if (strcmp(s->hub_matrix.skills[i].skill_name, s->selected_hub_skill) == 0) {
            entry = &s->hub_matrix.skills[i];
            break;
        }
    }
    if (!entry) {
        s->hub_subview = 0;
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

    /* Top Navigation Row */
    cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
        if (cui_button(ui, " 返回全景列表", .variant = CUI_BUTTON_SECONDARY, .icon = CUI_ICON_CHEVRON_LEFT)) {
            s->hub_subview = 0;
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

        cui_spacer(ui);

        /* Action: Broadcast to all tools */
        if (entry->in_hub) {
            if (cui_button(ui, " 🚀 全量广播主版本到所有工具", .variant = CUI_BUTTON_PRIMARY, .icon = CUI_ICON_SHARE_2)) {
                start_broadcast_thread(s, entry->skill_name, s->selected_hub_tool_idx);
            }
        }
    }

    /* Main Body: Left Tool Copies List (360px) | Right Diff View (Fill) */
    cui_row(ui, .width = CUI_FILL, .height = CUI_FILL, .gap = 14) {

        /* Left Pane: Master Hub & Tool Copies */
        cui_card(ui, .width = CUI_PX(360), .height = CUI_FILL, .pad = 12, .gap = 10) {

            /* 1. Master Hub Section */
            cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                cui_label(ui, "主版本库 (Master Hub)");
                cui_spacer(ui);
                if (entry->in_hub) {
                    cui_badge(ui, "标准基准", .tone = CUI_TONE_SUCCESS, .size = CUI_SIZE_SM);
                } else {
                    cui_badge(ui, "未收录", .tone = CUI_TONE_VIOLET, .size = CUI_SIZE_SM);
                }
            }

            cui_card(ui, .width = CUI_FILL, .pad = 10, .gap = 4, .muted = true) {
                cui_row(ui, .gap = 6, .align = CUI_ALIGN_CENTER) {
                    cui_icon(ui, CUI_ICON_ARCHIVE, 14, CUI_COLOR_ACCENT);
                    cui_text(ui, "~/.ai-space/skills", .size = 12, .font = CUI_FONT_MEDIUM);
                }
                if (entry->in_hub) {
                    char h_txt[64];
                    snprintf(h_txt, sizeof(h_txt), "内容指纹: %.12s...", entry->hub_hash);
                    cui_text(ui, h_txt, .size = 11, .color = CUI_COLOR_TEXT_SECONDARY);
                } else {
                    cui_text(ui, "尚未建立主版本，请点击右侧某个工具的【采纳为基准】",
                             .size = 11, .color = CUI_COLOR_TEXT_MUTED);
                }
            }

            cui_separator(ui);

            /* 2. Tool Copies Header */
            cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                cui_label(ui, "已安装的工具副本 (点击选择)");
                cui_spacer(ui);
                char copies_cnt[32];
                snprintf(copies_cnt, sizeof(copies_cnt), "%d / %d 工具", entry->installed_count, entry->tool_count);
                cui_text(ui, copies_cnt, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);
            }

            /* Scrollable Tool Copies */
            cui_scroll(ui, .id = "diff_tools_scroll", .width = CUI_FILL, .height = CUI_FILL) {
                cui_column(ui, .width = CUI_FILL, .gap = 6) {
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
                            .gap = 4,
                            .hoverable = true,
                            .selected = is_cur,
                        });

                        /* Tool Title & Status Badge */
                        cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                            cui_row(ui, .gap = 6, .align = CUI_ALIGN_CENTER) {
                                cui_icon(ui, CUI_ICON_FOLDER, 14, is_cur ? CUI_COLOR_ACCENT : CUI_COLOR_FOREGROUND);
                                cui_text(ui, tc->tool_name, .font = CUI_FONT_MEDIUM);
                            }
                            cui_spacer(ui);

                            if (tc->status == HUB_STATUS_IN_SYNC) {
                                cui_badge(ui, "完全一致", .tone = CUI_TONE_SUCCESS, .size = CUI_SIZE_SM);
                            } else if (tc->status == HUB_STATUS_LOCAL_MODIFIED) {
                                cui_badge(ui, "本地已改动", .tone = CUI_TONE_WARNING, .size = CUI_SIZE_SM);
                            } else if (tc->status == HUB_STATUS_OUTDATED) {
                                cui_badge(ui, "落后主仓", .tone = CUI_TONE_INFO, .size = CUI_SIZE_SM);
                            } else {
                                cui_badge(ui, "本地独立", .tone = CUI_TONE_VIOLET, .size = CUI_SIZE_SM);
                            }
                        }

                        char tc_sub[64];
                        snprintf(tc_sub, sizeof(tc_sub), "指纹: %.12s...  · 点击对比", tc->hash);
                        cui_text(ui, tc_sub, .size = 11, .color = is_cur ? CUI_COLOR_ACCENT : CUI_COLOR_TEXT_SECONDARY);

                        cui_card_end(ui);

                        if (card_clicked) {
                            s->selected_hub_tool_idx = t;
                            load_hub_diff(s, entry->skill_name, t, s->diff_file_rel);
                        }

                        cui_pop_id(ui);
                    }

                    /* Section 2: Uninstalled Tools */
                    int uninstalled_count = entry->tool_count - entry->installed_count;
                    if (uninstalled_count > 0) {
                        cui_gap(ui, 6);
                        cui_separator(ui);
                        char unins_hdr[64];
                        snprintf(unins_hdr, sizeof(unins_hdr), "未安装此技能的工具 (%d)", uninstalled_count);
                        cui_text(ui, unins_hdr, .size = 12, .color = CUI_COLOR_TEXT_MUTED);

                        for (int t = 0; t < entry->tool_count; t++) {
                            tool_copy_status_t *tc = &entry->tool_copies[t];
                            if (tc->exists) continue;

                            cui_push_id_int(ui, 1000 + t);
                            cui_card(ui, .width = CUI_FILL, .pad = 8, .gap = 4, .muted = true) {
                                cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                                    cui_row(ui, .gap = 6, .align = CUI_ALIGN_CENTER) {
                                        cui_icon(ui, CUI_ICON_FOLDER, 14, CUI_COLOR_TEXT_MUTED);
                                        cui_text(ui, tc->tool_name, .size = 13, .color = CUI_COLOR_TEXT_SECONDARY);
                                    }
                                    cui_spacer(ui);
                                    if (entry->in_hub) {
                                        if (cui_button(ui, "复制安装",
                                                       .variant = CUI_BUTTON_OUTLINE,
                                                       .size = CUI_SIZE_SM, .icon = CUI_ICON_PLUS)) {
                                            start_copy_install_thread(s, tc->tool_skills_path, entry->skill_name, tc->tool_name, t);
                                        }
                                    } else {
                                        cui_badge(ui, "未安装", .tone = CUI_TONE_NEUTRAL, .size = CUI_SIZE_SM);
                                    }
                                }
                            }
                            cui_pop_id(ui);
                        }
                    }
                }
            }
        }

        /* Right Pane: Embedded CUI Diff View */
        cui_card(ui, .width = CUI_FILL, .height = CUI_FILL, .pad = 12, .gap = 8) {
            tool_copy_status_t *cur_tc = (s->selected_hub_tool_idx >= 0 && s->selected_hub_tool_idx < entry->tool_count) ?
                                         &entry->tool_copies[s->selected_hub_tool_idx] : NULL;

            /* Diff Toolbar */
            cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                    cui_icon(ui, CUI_ICON_CODE, 16, CUI_COLOR_ACCENT);
                    cui_text(ui, "比对: ", .color = CUI_COLOR_TEXT_SECONDARY);
                    cui_badge(ui, s->diff_file_rel[0] ? s->diff_file_rel : "SKILL.md",
                              .tone = CUI_TONE_ACCENT, .size = CUI_SIZE_SM);

                    if (cur_tc && cur_tc->exists) {
                        cui_text(ui, " 基准: 主仓 ⟷ 工具: ", .size = 12, .color = CUI_COLOR_TEXT_MUTED);
                        cui_badge(ui, cur_tc->tool_name,
                                  .tone = (cur_tc->status == HUB_STATUS_IN_SYNC) ? CUI_TONE_SUCCESS :
                                          ((cur_tc->status == HUB_STATUS_LOCAL_MODIFIED) ? CUI_TONE_WARNING : CUI_TONE_INFO),
                                  .size = CUI_SIZE_SM);

                        /* Direct sync actions on the diff toolbar */
                        if (cui_button(ui, " 采纳为基准", .variant = CUI_BUTTON_OUTLINE,
                                       .size = CUI_SIZE_SM, .icon = CUI_ICON_CHECK)) {
                            start_promote_to_master_thread(s, cur_tc->tool_skills_path, entry->skill_name, cur_tc->tool_name, s->selected_hub_tool_idx);
                        }

                        if (entry->in_hub) {
                            if (cui_button(ui, " 覆盖此工具", .variant = CUI_BUTTON_SECONDARY,
                                           .size = CUI_SIZE_SM, .icon = CUI_ICON_DOWNLOAD)) {
                                start_sync_master_to_tool_thread(s, cur_tc->tool_skills_path, entry->skill_name, cur_tc->tool_name, s->selected_hub_tool_idx);
                            }
                        }
                    }
                }

                cui_spacer(ui);

                /* Diff Mode Toggle */
                static const char *diff_modes[] = { "左右分屏", "单列内联" };
                int d_mode_int = (int)s->diff_mode;
                if (cui_tabs(ui, "diff_view_mode_tabs", diff_modes, 2, &d_mode_int)) {
                    s->diff_mode = (cui_diff_mode)d_mode_int;
                }
            }

            cui_separator(ui);

            /* Embedded cui_diff_view */
            char old_title_buf[256];
            char new_title_buf[256];
            snprintf(old_title_buf, sizeof(old_title_buf), "主版本库 (%s)", entry->hub_path);
            if (cur_tc && cur_tc->exists) {
                snprintf(new_title_buf, sizeof(new_title_buf), "工具副本【%s】 (%s)", cur_tc->tool_name, cur_tc->skill_path);
            } else {
                snprintf(new_title_buf, sizeof(new_title_buf), "工具副本 (未安装)");
            }

            const char *old_txt = s->diff_old_text ? s->diff_old_text : "";
            const char *new_txt = s->diff_new_text ? s->diff_new_text : "";

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
                } else if (old_txt[0] == '\0' && new_txt[0] == '\0') {
                    cui_text(ui, "双侧内容为空或文件未收录", .color = CUI_COLOR_TEXT_MUTED);
                } else {
                    cui_badge(ui, "内容完全一致，未发生分化", .tone = CUI_TONE_SUCCESS, .size = CUI_SIZE_SM);
                }

                cui_spacer(ui);
                cui_text(ui, "🛡️ 所有同步均为物理文件拷贝，覆盖前自动在 ~/.ai-space/backups/ 生成安全备份快照",
                         .size = 11, .color = CUI_COLOR_TEXT_MUTED);
            }
        }
    }

    render_op_progress_modal(ui, s);
}
