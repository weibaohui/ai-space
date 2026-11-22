#include "view_master_hub.h"
#include "view_common.h"
#include "view_skill_tree.h"
#include "view_hub_diff.h"
#include "skill_scanner.h"
#include "hub_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const char *detect_lang(const char *path) {
    if (!path) return "text";
    const char *ext = strrchr(path, '.');
    if (!ext) return "text";
    if (strcmp(ext, ".md") == 0 || strcmp(ext, ".markdown") == 0) return "markdown";
    if (strcmp(ext, ".c") == 0 || strcmp(ext, ".h") == 0) return "c";
    if (strcmp(ext, ".py") == 0) return "python";
    if (strcmp(ext, ".sh") == 0 || strcmp(ext, ".bash") == 0) return "bash";
    if (strcmp(ext, ".json") == 0) return "json";
    if (strcmp(ext, ".yaml") == 0 || strcmp(ext, ".yml") == 0) return "yaml";
    if (strcmp(ext, ".js") == 0 || strcmp(ext, ".ts") == 0) return "javascript";
    if (strcmp(ext, ".html") == 0) return "html";
    return "text";
}

void open_master_skill_explorer(app_state_t *s, const char *skill_name) {
    if (!s || !skill_name) return;
    s->prev_hub_subview = s->hub_subview;
    s->hub_subview = HUB_SUBVIEW_EXPLORER;

    strncpy(s->active_skill_name, skill_name, sizeof(s->active_skill_name) - 1);
    s->active_skill_name[sizeof(s->active_skill_name) - 1] = '\0';
    snprintf(s->active_skill_path, sizeof(s->active_skill_path), "%s/%s", s->hub_matrix.hub_dir, skill_name);

    if (s->master_skill_file_tree) {
        free_skill_file_tree(s->master_skill_file_tree);
        s->master_skill_file_tree = NULL;
    }
    s->master_skill_file_tree = build_skill_file_tree(s->active_skill_path, "", 0, 10);

    /* Clear previously selected file */
    s->selected_file_path[0] = '\0';
    s->selected_file_name[0] = '\0';
    if (s->selected_file_content) {
        free(s->selected_file_content);
        s->selected_file_content = NULL;
    }
    s->selected_file_size = 0;
    s->selected_file_lines = 0;

    /* Select SKILL.md by default */
    char skill_md_path[1024];
    snprintf(skill_md_path, sizeof(skill_md_path), "%s/SKILL.md", s->active_skill_path);
    struct stat st;
    if (stat(skill_md_path, &st) == 0) {
        strncpy(s->selected_file_path, skill_md_path, sizeof(s->selected_file_path) - 1);
        strncpy(s->selected_file_name, "SKILL.md", sizeof(s->selected_file_name) - 1);
        s->selected_file_content = read_file_text_content(skill_md_path, 256 * 1024,
                                                          &s->selected_file_size,
                                                          &s->selected_file_lines,
                                                          &s->selected_file_is_binary);
    }
}

static void on_master_file_select(file_node_t *node, void *user_data) {
    app_state_t *s = (app_state_t *)user_data;
    if (!s || !node || node->is_dir) return;

    strncpy(s->selected_file_path, node->abs_path, sizeof(s->selected_file_path) - 1);
    strncpy(s->selected_file_name, node->name, sizeof(s->selected_file_name) - 1);

    if (s->selected_file_content) {
        free(s->selected_file_content);
        s->selected_file_content = NULL;
    }

    s->selected_file_content = read_file_text_content(node->abs_path, 256 * 1024,
                                                      &s->selected_file_size,
                                                      &s->selected_file_lines,
                                                      &s->selected_file_is_binary);
}

void render_master_hub_explorer_view(cui_ctx *ui, app_state_t *s) {
    /* Header & Breadcrumb */
    cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
        if (cui_button(ui, "  返回主仓技能列表", .variant = CUI_BUTTON_SECONDARY, .icon = CUI_ICON_CHEVRON_LEFT)) {
            s->hub_subview = s->prev_hub_subview;
        }

        cui_gap(ui, 12);
        cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
            cui_heading(ui, s->active_skill_name, 3);
            cui_badge(ui, "主仓基准版本", .tone = CUI_TONE_ACCENT, .size = CUI_SIZE_SM);
        }

        cui_spacer(ui);
        cui_text(ui, s->active_skill_path, .size = 12, .color = CUI_COLOR_TEXT_MUTED);
    }

    /* 2 Columns: File Tree + Content Viewer */
    cui_row(ui, .width = CUI_FILL, .height = CUI_FILL, .gap = 12) {
        skill_tree_view_opts_t tree_opts = {
            .title = "主仓目录结构",
            .badge_label = s->active_skill_name,
            .badge_tone = CUI_TONE_ACCENT,
            .selected_path = s->selected_file_path,
            .match_by_rel_path = false,
            .show_diff_badges = false,
            .show_summary_badges = false,
            .width = 280,
        };
        render_skill_file_tree_component(ui, s->master_skill_file_tree, &tree_opts, on_master_file_select, s);

        /* Content Viewer */
        cui_card(ui, .width = CUI_FILL, .height = CUI_FILL, .pad = 14, .gap = 10) {
            cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                    cui_icon(ui, CUI_ICON_FILE_TEXT, 18, CUI_COLOR_ACCENT);
                    cui_label(ui, s->selected_file_name[0] ? s->selected_file_name : "请在左侧选择文件");

                    bool is_md = (strstr(s->selected_file_name, ".md") != NULL ||
                                  strstr(s->selected_file_name, ".markdown") != NULL);
                    if (is_md) {
                        cui_badge(ui, "Markdown", .tone = CUI_TONE_ACCENT, .size = CUI_SIZE_SM);
                    }

                    if (s->selected_file_size > 0) {
                        char sz_txt[32];
                        format_size(s->selected_file_size, sz_txt, sizeof(sz_txt));
                        cui_textf(ui, "%s · %d 行", sz_txt, s->selected_file_lines);
                    }
                }

                cui_spacer(ui);

                bool is_md = (strstr(s->selected_file_name, ".md") != NULL ||
                              strstr(s->selected_file_name, ".markdown") != NULL);
                if (is_md) {
                    static const char *doc_modes[] = { "富文本预览", "源码" };
                    cui_tabs(ui, "master_doc_render_mode_tabs", doc_modes, 2, &s->doc_render_mode);
                }
            }

            if (s->selected_file_path[0] != '\0') {
                cui_text(ui, s->selected_file_path, .size = 11, .color = CUI_COLOR_TEXT_MUTED);
            }

            cui_separator(ui);

            if (s->selected_file_content && s->selected_file_content[0] != '\0') {
                bool is_md = (strstr(s->selected_file_name, ".md") != NULL ||
                              strstr(s->selected_file_name, ".markdown") != NULL);
                if (is_md && s->doc_render_mode == 0) {
                    cui_scroll(ui, .id = "master_md_preview_scroll", .width = CUI_FILL, .height = CUI_FILL) {
                        cui_markdown(ui, "master_md_preview", s->selected_file_content);
                    }
                } else {
                    const char *lang = detect_lang(s->selected_file_name);
                    cui_scroll(ui, .id = "master_code_block_scroll", .width = CUI_FILL, .height = CUI_FILL) {
                        cui_code_block(ui, "master_code_view", s->selected_file_content,
                                       .lang = lang, .line_numbers = true);
                    }
                }
            } else if (s->selected_file_path[0] != '\0') {
                cui_column(ui, .width = CUI_FILL, .height = CUI_FILL,
                           .align = CUI_ALIGN_CENTER, .justify = CUI_JUSTIFY_CENTER) {
                    cui_icon(ui, CUI_ICON_INFO, 28, CUI_COLOR_TEXT_MUTED);
                    cui_gap(ui, 8);
                    cui_muted(ui, "该文件为空或暂无内容");
                }
            } else {
                cui_column(ui, .width = CUI_FILL, .height = CUI_FILL,
                           .align = CUI_ALIGN_CENTER, .justify = CUI_JUSTIFY_CENTER) {
                    cui_icon(ui, CUI_ICON_FILE_TEXT, 36, CUI_COLOR_TEXT_MUTED);
                    cui_gap(ui, 8);
                    cui_text(ui, "请在左侧目录结构树中选择文件预览内容", .color = CUI_COLOR_TEXT_MUTED);
                }
            }
        }
    }
}

void render_master_hub_skills_view(cui_ctx *ui, app_state_t *s) {
    /* 1. Metric Cards */
    cui_row(ui, .width = CUI_FILL, .gap = 14) {
        char buf_hub[32], buf_tool[32], buf_drift[32];
        snprintf(buf_hub, sizeof(buf_hub), "%d 个", s->hub_matrix.hub_managed_skills);

        /* Count covered tools */
        int covered_tools = 0;
        for (int t = 0; t < s->hub_matrix.tool_count; t++) {
            bool has_any = false;
            for (int i = 0; i < s->hub_matrix.skill_count; i++) {
                if (s->hub_matrix.skills[i].in_hub && s->hub_matrix.skills[i].tool_copies[t].exists) {
                    has_any = true;
                    break;
                }
            }
            if (has_any) covered_tools++;
        }
        snprintf(buf_tool, sizeof(buf_tool), "%d / %d 个", covered_tools, s->hub_matrix.tool_count);
        snprintf(buf_drift, sizeof(buf_drift), "%d 项", s->hub_matrix.drifted_skills_count);

        cui_card(ui, .width = CUI_GROW(1), .pad = 16) {
            cui_metric(ui, "主仓已收录技能", buf_hub, .icon = CUI_ICON_ARCHIVE);
        }
        cui_card(ui, .width = CUI_GROW(1), .pad = 16) {
            cui_metric(ui, "分发覆盖本地工具", buf_tool, .icon = CUI_ICON_BOX);
        }
        cui_card(ui, .width = CUI_GROW(1), .pad = 16) {
            cui_metric(ui, "本地副本待同步/分化", buf_drift, .icon = CUI_ICON_TRIANGLE_ALERT);
        }
        cui_card(ui, .width = CUI_GROW(1), .pad = 16) {
            cui_metric(ui, "主仓存储路径", "~/.ai-space/skills", .icon = CUI_ICON_FOLDER);
        }
    }

    /* 2. Search & Action Toolbar */
    cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
        cui_input(ui, "search_master_skills", s->master_skill_search_kw, sizeof(s->master_skill_search_kw),
                  .placeholder = "搜索主仓技能名、描述...",
                  .icon = CUI_ICON_SEARCH,
                  .width = CUI_PX(320));

        cui_spacer(ui);

        cui_row(ui, .gap = 10, .align = CUI_ALIGN_CENTER) {
            if (cui_button(ui, " 重新比对全景", .variant = CUI_BUTTON_OUTLINE, .icon = CUI_ICON_REFRESH_CW)) {
                start_hub_scan_thread(s);
            }
            if (cui_button(ui, " 打开全景扫描矩阵", .variant = CUI_BUTTON_SECONDARY, .icon = CUI_ICON_GRID_3X3)) {
                s->hub_subview = HUB_SUBVIEW_SCAN_MATRIX;
            }
            if (cui_button(ui, " 一键全量收录主库", .variant = CUI_BUTTON_SECONDARY, .icon = CUI_ICON_CHECK)) {
                start_adopt_all_thread(s);
            }
            if (cui_button(ui, " 🚀 全量分发到所有工具", .variant = CUI_BUTTON_PRIMARY, .icon = CUI_ICON_SEND)) {
                start_distribute_all_thread(s);
            }
        }
    }

    /* 3. Skill Cards List */
    if (s->hub_matrix.hub_managed_skills == 0) {
        /* Empty State */
        cui_card(ui, .width = CUI_FILL, .height = CUI_FILL, .pad = 40) {
            cui_column(ui, .width = CUI_FILL, .height = CUI_FILL,
                       .align = CUI_ALIGN_CENTER, .justify = CUI_JUSTIFY_CENTER) {
                cui_icon(ui, CUI_ICON_ARCHIVE, 48, CUI_COLOR_TEXT_MUTED);
                cui_gap(ui, 16);
                cui_heading(ui, "当前主仓 (~/.ai-space/skills) 尚未收录基准技能", 3);
                cui_gap(ui, 6);
                cui_text(ui, "主仓用于集中沉淀与统一管理跨工具的标准基准技能版本。可切换至【全景扫描】一键全量收录主库，或单独比对采纳。",
                         .color = CUI_COLOR_TEXT_SECONDARY, .size = 13);
                cui_gap(ui, 20);
                if (cui_button(ui, " 前往技能全景扫描与比对", .variant = CUI_BUTTON_PRIMARY, .icon = CUI_ICON_GRID_3X3)) {
                    s->hub_subview = HUB_SUBVIEW_SCAN_MATRIX;
                }
            }
        }
    } else {
        cui_scroll(ui, .id = "master_skills_scroll_list", .width = CUI_FILL, .height = CUI_FILL) {
            cui_column(ui, .width = CUI_FILL, .gap = 12) {
                int matched = 0;
                for (int i = 0; i < s->hub_matrix.skill_count; i++) {
                    hub_skill_entry_t *entry = &s->hub_matrix.skills[i];
                    if (!entry->in_hub) continue;

                    if (s->master_skill_search_kw[0] != '\0') {
                        if (!ci_contains(entry->skill_name, s->master_skill_search_kw) &&
                            !ci_contains(entry->description, s->master_skill_search_kw)) {
                            continue;
                        }
                    }
                    matched++;

                    cui_push_id_int(ui, i);
                    cui_card(ui, .width = CUI_FILL, .pad = 14, .gap = 10) {
                        /* Row 1: Title, Status, Action Buttons */
                        cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                            cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                                cui_icon(ui, CUI_ICON_ARCHIVE, 18, CUI_COLOR_ACCENT);
                                cui_heading(ui, entry->skill_name, 4);

                                if (entry->modified_count > 0) {
                                    char b_txt[32];
                                    snprintf(b_txt, sizeof(b_txt), "%d 个工具副本有改动", entry->modified_count);
                                    cui_badge(ui, b_txt, .tone = CUI_TONE_WARNING, .size = CUI_SIZE_SM);
                                } else if (entry->outdated_count > 0) {
                                    char b_txt[32];
                                    snprintf(b_txt, sizeof(b_txt), "%d 个工具副本落后", entry->outdated_count);
                                    cui_badge(ui, b_txt, .tone = CUI_TONE_INFO, .size = CUI_SIZE_SM);
                                } else if (entry->installed_count > 0) {
                                    cui_badge(ui, "所有副本与主仓一致", .tone = CUI_TONE_SUCCESS, .size = CUI_SIZE_SM);
                                } else {
                                    cui_badge(ui, "主仓独有 (未分发)", .tone = CUI_TONE_NEUTRAL, .size = CUI_SIZE_SM);
                                }
                            }

                            cui_spacer(ui);

                            cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                                if (cui_button(ui, " 浏览主仓文件", .variant = CUI_BUTTON_SECONDARY,
                                               .icon = CUI_ICON_FOLDER_OPEN, .size = CUI_SIZE_SM)) {
                                    open_master_skill_explorer(s, entry->skill_name);
                                }

                                /* Core Distribution Entry Point */
                                if (cui_button(ui, (entry->installed_count == 0) ? " 🚀 分发到工具..." : " 🚀 分发管理...",
                                               .variant = (entry->installed_count == 0) ? CUI_BUTTON_PRIMARY : CUI_BUTTON_SECONDARY,
                                               .icon = CUI_ICON_SEND, .size = CUI_SIZE_SM)) {
                                    s->show_distribute_modal = true;
                                    strncpy(s->distribute_skill_name, entry->skill_name, sizeof(s->distribute_skill_name) - 1);
                                }

                                if (entry->installed_count > 0) {
                                    if (cui_button(ui, " 全景比对", .variant = CUI_BUTTON_OUTLINE,
                                                   .icon = CUI_ICON_GIT_BRANCH, .size = CUI_SIZE_SM)) {
                                        s->prev_hub_subview = HUB_SUBVIEW_MASTER_SKILLS;
                                        open_hub_diff_view(s, entry->skill_name);
                                    }

                                    if (entry->modified_count > 0 || entry->outdated_count > 0) {
                                        if (cui_button(ui, " ↺ 覆盖同步所有副本", .variant = CUI_BUTTON_OUTLINE,
                                                       .icon = CUI_ICON_REFRESH_CW, .size = CUI_SIZE_SM)) {
                                            start_broadcast_thread(s, entry->skill_name, 0);
                                        }
                                    }
                                }

                                if (cui_button(ui, " 从主仓移除", .variant = CUI_BUTTON_GHOST,
                                               .icon = CUI_ICON_TRASH_2, .size = CUI_SIZE_SM)) {
                                    if (hub_delete_master_skill(entry->skill_name) == 0) {
                                        char tmsg[128];
                                        snprintf(tmsg, sizeof(tmsg), "已将【%s】移出主仓（已创建快照备份）", entry->skill_name);
                                        app_set_toast(s, tmsg);
                                        start_hub_scan_thread(s);
                                    }
                                }
                            }
                        }

                        /* Row 2: Description */
                        if (entry->description[0] != '\0') {
                            cui_text(ui, entry->description, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);
                        } else {
                            cui_text(ui, "（暂无技能描述，可在 SKILL.md 的 description 中添加）",
                                     .size = 12, .color = CUI_COLOR_TEXT_MUTED);
                        }

                        /* Row 3: Tool Distribution Badges */
                        cui_row(ui, .gap = 6, .align = CUI_ALIGN_CENTER) {
                            cui_textf(ui, "分发覆盖 (%d/%d):", entry->installed_count, s->hub_matrix.tool_count);
                            if (entry->installed_count == 0) {
                                cui_text(ui, "尚未安装到任何本地工具，点击上方【🚀 分发到工具...】开始安装下发",
                                         .size = 12, .color = CUI_COLOR_TEXT_MUTED);
                            } else {
                                for (int t = 0; t < entry->tool_count; t++) {
                                    tool_copy_status_t *tc = &entry->tool_copies[t];
                                    if (!tc->exists) continue;

                                    cui_tone tone = CUI_TONE_NEUTRAL;
                                    char label[96];
                                    if (tc->status == HUB_STATUS_IN_SYNC) {
                                        tone = CUI_TONE_SUCCESS;
                                        snprintf(label, sizeof(label), "%s (一致)", tc->tool_name);
                                    } else if (tc->status == HUB_STATUS_LOCAL_MODIFIED) {
                                        tone = CUI_TONE_WARNING;
                                        snprintf(label, sizeof(label), "%s (有改动)", tc->tool_name);
                                    } else if (tc->status == HUB_STATUS_OUTDATED) {
                                        tone = CUI_TONE_INFO;
                                        snprintf(label, sizeof(label), "%s (落后)", tc->tool_name);
                                    } else {
                                        snprintf(label, sizeof(label), "%s", tc->tool_name);
                                    }
                                    cui_badge(ui, label, .tone = tone, .size = CUI_SIZE_SM);
                                }
                            }
                        }
                    }
                    cui_pop_id(ui);
                }

                if (matched == 0) {
                    cui_column(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER, .pad = 30) {
                        cui_muted(ui, "未找到匹配的主仓技能");
                    }
                }
            }
        }
    }
}
