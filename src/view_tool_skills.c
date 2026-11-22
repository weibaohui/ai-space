#include "view_tool_skills.h"
#include "view_common.h"
#include "view_skill_tree.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void load_skill_file(app_state_t *s, const char *path, const char *name) {
    if (!path || path[0] == '\0') {
        s->selected_file_path[0] = '\0';
        s->selected_file_name[0] = '\0';
        if (s->selected_file_content) {
            free(s->selected_file_content);
            s->selected_file_content = NULL;
        }
        s->selected_file_size = 0;
        s->selected_file_lines = 0;
        s->selected_file_is_binary = false;
        return;
    }

    strncpy(s->selected_file_path, path, sizeof(s->selected_file_path) - 1);
    s->selected_file_path[sizeof(s->selected_file_path) - 1] = '\0';

    if (name && name[0] != '\0') {
        strncpy(s->selected_file_name, name, sizeof(s->selected_file_name) - 1);
        s->selected_file_name[sizeof(s->selected_file_name) - 1] = '\0';
    } else {
        const char *slash = strrchr(path, '/');
        strncpy(s->selected_file_name, slash ? slash + 1 : path, sizeof(s->selected_file_name) - 1);
        s->selected_file_name[sizeof(s->selected_file_name) - 1] = '\0';
    }

    if (s->selected_file_content) {
        free(s->selected_file_content);
        s->selected_file_content = NULL;
    }

    size_t sz = 0;
    int lines = 0;
    bool is_bin = false;
    s->selected_file_content = read_file_text_content(path, 1024 * 1024, &sz, &lines, &is_bin);
    s->selected_file_size = sz;
    s->selected_file_lines = lines;
    s->selected_file_is_binary = is_bin;
    s->doc_render_mode = 0;
}

void select_active_skill(app_state_t *s, const char *skill_name, const char *skill_path) {
    strncpy(s->active_skill_name, skill_name, sizeof(s->active_skill_name) - 1);
    s->active_skill_name[sizeof(s->active_skill_name) - 1] = '\0';

    strncpy(s->active_skill_path, skill_path, sizeof(s->active_skill_path) - 1);
    s->active_skill_path[sizeof(s->active_skill_path) - 1] = '\0';

    if (s->skill_file_tree) {
        free_skill_file_tree(s->skill_file_tree);
        free(s->skill_file_tree);
        s->skill_file_tree = NULL;
    }

    s->skill_file_tree = build_skill_file_tree(skill_path, "", 0, 5);

    /* Priority 1: SKILL.md */
    char skill_md[1200];
    snprintf(skill_md, sizeof(skill_md), "%s/SKILL.md", skill_path);
    FILE *f = fopen(skill_md, "r");
    if (f) {
        fclose(f);
        load_skill_file(s, skill_md, "SKILL.md");
        return;
    }

    /* Priority 2: First non-directory file */
    if (s->skill_file_tree) {
        file_node_t *first_f = NULL;
        for (int i = 0; i < s->skill_file_tree->child_count; i++) {
            if (!s->skill_file_tree->children[i].is_dir) {
                first_f = &s->skill_file_tree->children[i];
                break;
            }
        }
        if (first_f) {
            load_skill_file(s, first_f->abs_path, first_f->name);
            return;
        }
    }

    load_skill_file(s, NULL, "");
}

void open_tool_skills_page(app_state_t *s, const char *tool_name, const char *tool_path) {
    s->prev_page = s->current_page;
    strncpy(s->active_tool_name, tool_name, sizeof(s->active_tool_name) - 1);
    s->active_tool_name[sizeof(s->active_tool_name) - 1] = '\0';

    strncpy(s->active_tool_path, tool_path, sizeof(s->active_tool_path) - 1);
    s->active_tool_path[sizeof(s->active_tool_path) - 1] = '\0';

    if (s->skill_items) {
        free(s->skill_items);
        s->skill_items = NULL;
        s->skill_count = 0;
    }

    scan_tool_skills(tool_path, &s->skill_items, &s->skill_count);

    s->skill_search_kw[0] = '\0';
    s->skill_view_mode = 0;

    if (s->skill_count > 0) {
        select_active_skill(s, s->skill_items[0].name, s->skill_items[0].path);
    } else {
        s->active_skill_name[0] = '\0';
        s->active_skill_path[0] = '\0';
        if (s->skill_file_tree) {
            free_skill_file_tree(s->skill_file_tree);
            free(s->skill_file_tree);
            s->skill_file_tree = NULL;
        }
        load_skill_file(s, NULL, "");
    }

    s->current_page = PAGE_TOOL_SKILLS;
}

static void on_tool_skill_file_select(file_node_t *node, void *user_data) {
    app_state_t *s = (app_state_t *)user_data;
    if (s && node && !node->is_dir) {
        load_skill_file(s, node->abs_path, node->name);
    }
}

void render_tool_skills_page(cui_ctx *ui, app_state_t *s) {
    /* 1. Navigation Top Bar */
    cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
        if (cui_button(ui, "返回", .variant = CUI_BUTTON_SECONDARY,
                       .icon = CUI_ICON_CHEVRON_LEFT)) {
            s->current_page = (s->prev_page == PAGE_HUB_MATRIX) ? PAGE_HUB_MATRIX : PAGE_TOOL_LIST;
        }

        cui_gap(ui, 12);

        cui_box(ui, .width = CUI_PX(36), .height = CUI_PX(36), .radius = 8,
                .bg = cui_alpha(cui_color_of(ui, CUI_COLOR_ACCENT), 0.15f),
                .dir = CUI_STACK, .align = CUI_ALIGN_CENTER, .justify = CUI_JUSTIFY_CENTER) {
            cui_icon(ui, CUI_ICON_FOLDER_OPEN, 18, CUI_COLOR_ACCENT);
        }

        cui_column(ui, .gap = 2) {
            cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                char title_buf[256];
                snprintf(title_buf, sizeof(title_buf), "工具【%s】技能列表", s->active_tool_name);
                cui_heading(ui, title_buf, 4);

                cui_badge(ui, "实时目录读取", .tone = CUI_TONE_INFO, .size = CUI_SIZE_SM);
            }
            char path_sub[600];
            snprintf(path_sub, sizeof(path_sub), "路径: %s · 发现 %d 个 Skill 目录",
                     s->active_tool_path, s->skill_count);
            cui_text(ui, path_sub, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);
        }

        cui_spacer(ui);

        if (s->active_skill_name[0] != '\0') {
            if (cui_button(ui, "卸载", .variant = CUI_BUTTON_OUTLINE, .icon = CUI_ICON_TRASH_2)) {
                s->show_uninstall_tool_modal = true;
                strncpy(s->uninstall_tool_skill_name, s->active_skill_name, sizeof(s->uninstall_tool_skill_name) - 1);
                s->uninstall_tool_skill_name[sizeof(s->uninstall_tool_skill_name) - 1] = '\0';
            }
        }
    }

    /* 2. Main Content Body: Left Skills List (250px) | Middle File Tree (280px) | Right Content Viewer (Fill) */
    cui_row(ui, .width = CUI_FILL, .height = CUI_FILL, .gap = 14) {

            /* Left Pane: Skills List */
            cui_card(ui, .width = CUI_PX(250), .height = CUI_FILL, .pad = 12, .gap = 10) {
                cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                    cui_label(ui, "技能目录");
                    cui_spacer(ui);
                    char cnt_badge[32];
                    snprintf(cnt_badge, sizeof(cnt_badge), "%d 项", s->skill_count);
                    cui_badge(ui, cnt_badge, .tone = CUI_TONE_NEUTRAL, .size = CUI_SIZE_SM);
                }

                cui_search(ui, "skills_list_filter", s->skill_search_kw, sizeof(s->skill_search_kw),
                           .placeholder = "过滤技能名...", .width = CUI_FILL);

                cui_separator(ui);

                cui_scroll(ui, .id = "skills_side_scroll", .width = CUI_FILL, .height = CUI_FILL) {
                    cui_column(ui, .width = CUI_FILL, .gap = 2) {
                        for (int i = 0; i < s->skill_count; i++) {
                            skill_item_t *sk = &s->skill_items[i];
                            if (s->skill_search_kw[0] != '\0' && !ci_contains(sk->name, s->skill_search_kw)) {
                                continue;
                            }

                            cui_push_id_int(ui, i);
                            bool is_sel = (strcmp(s->active_skill_name, sk->name) == 0);
                            char fl_info[32];
                            snprintf(fl_info, sizeof(fl_info), "%d files", sk->file_count);

                            if (cui_list_item(ui, sk->name, .icon = CUI_ICON_BOX,
                                              .trailing = fl_info, .selected = is_sel)) {
                                select_active_skill(s, sk->name, sk->path);
                            }
                            cui_pop_id(ui);
                        }
                    }
                }
            }

            /* Middle Pane: 技能目录结构树 (复用统一组件) */
            skill_tree_view_opts_t tree_opts = {
                .title = "目录结构树",
                .badge_label = s->active_skill_name[0] ? s->active_skill_name : NULL,
                .badge_tone = CUI_TONE_ACCENT,
                .selected_path = s->selected_file_path,
                .match_by_rel_path = false,
                .show_diff_badges = false,
                .show_summary_badges = false,
                .width = 280,
            };
            render_skill_file_tree_component(ui, s->skill_file_tree, &tree_opts, on_tool_skill_file_select, s);

            /* Right Pane: 右具体内容 (File Content Viewer) */
            cui_card(ui, .width = CUI_FILL, .height = CUI_FILL, .pad = 14, .gap = 10) {
                /* Top file metadata bar */
                cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                    cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                        cui_icon(ui, CUI_ICON_FILE_TEXT, 18, CUI_COLOR_ACCENT);
                        cui_label(ui, s->selected_file_name[0] ? s->selected_file_name : "请在左树选择文件");

                        /* File kind badge */
                        bool is_md = (strstr(s->selected_file_name, ".md") != NULL ||
                                      strstr(s->selected_file_name, ".markdown") != NULL);
                        bool is_script = (strstr(s->selected_file_name, ".sh") != NULL ||
                                          strstr(s->selected_file_name, ".bash") != NULL ||
                                          strstr(s->selected_file_name, ".zsh") != NULL ||
                                          strstr(s->selected_file_name, ".py") != NULL ||
                                          strstr(s->selected_file_name, ".js") != NULL ||
                                          strstr(s->selected_file_name, ".ts") != NULL);

                        if (is_md) {
                            cui_badge(ui, "Markdown", .tone = CUI_TONE_ACCENT, .size = CUI_SIZE_SM);
                        } else if (is_script) {
                            cui_badge(ui, "脚本文件", .tone = CUI_TONE_SUCCESS, .size = CUI_SIZE_SM);
                        } else if (s->selected_file_name[0] != '\0') {
                            cui_badge(ui, "文本配置", .tone = CUI_TONE_INFO, .size = CUI_SIZE_SM);
                        }

                        if (s->selected_file_size > 0) {
                            char sz_txt[32];
                            format_size(s->selected_file_size, sz_txt, sizeof(sz_txt));
                            cui_textf(ui, "%s · %d 行", sz_txt, s->selected_file_lines);
                        }
                    }

                    cui_spacer(ui);

                    /* Toggle preview vs raw code if Markdown */
                    bool is_md = (strstr(s->selected_file_name, ".md") != NULL ||
                                  strstr(s->selected_file_name, ".markdown") != NULL);
                    if (is_md) {
                        static const char *doc_modes[] = { "富文本预览", "源码" };
                        cui_tabs(ui, "doc_render_mode_tabs", doc_modes, 2, &s->doc_render_mode);
                    }
                }

                if (s->selected_file_path[0] != '\0') {
                    cui_text(ui, s->selected_file_path, .size = 11, .color = CUI_COLOR_TEXT_MUTED);
                }

                cui_separator(ui);

                /* Viewer body */
                if (s->selected_file_content && s->selected_file_content[0] != '\0') {
                    bool is_md = (strstr(s->selected_file_name, ".md") != NULL ||
                                  strstr(s->selected_file_name, ".markdown") != NULL);

                    if (is_md && s->doc_render_mode == 0) {
                        cui_scroll(ui, .id = "md_preview_scroll", .width = CUI_FILL, .height = CUI_FILL) {
                            cui_markdown(ui, "md_preview", s->selected_file_content);
                        }
                    } else {
                        const char *lang = detect_syntax_lang(s->selected_file_name);
                        cui_scroll(ui, .id = "code_block_scroll", .width = CUI_FILL, .height = CUI_FILL) {
                            cui_code_block(ui, "code_view", s->selected_file_content,
                                           .lang = lang,
                                           .line_numbers = true);
                        }
                    }
                } else if (s->selected_file_path[0] != '\0') {
                    cui_column(ui, .width = CUI_FILL, .height = CUI_FILL,
                               .align = CUI_ALIGN_CENTER, .justify = CUI_JUSTIFY_CENTER) {
                        cui_icon(ui, CUI_ICON_INFO, 28, CUI_COLOR_TEXT_MUTED);
                        cui_gap(ui, 8);
                        cui_muted(ui, "该文件为空或不支持直接预览");
                    }
                } else {
                    cui_column(ui, .width = CUI_FILL, .height = CUI_FILL,
                               .align = CUI_ALIGN_CENTER, .justify = CUI_JUSTIFY_CENTER) {
                        cui_icon(ui, CUI_ICON_FILE_TEXT, 36, CUI_COLOR_TEXT_MUTED);
                        cui_gap(ui, 8);
                        cui_text(ui, "请在左侧或中间树形中选择文件查看详情", .color = CUI_COLOR_TEXT_MUTED);
                    }
                }
            }
        }
    }
