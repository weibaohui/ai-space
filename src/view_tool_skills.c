#include "skill_file_tree.h"
#include "file_preview.h"
#include "view_tool_skills.h"
#include "view_file_editor.h"
#include "view_uninstall.h"
#include "view_common.h"
#include "view_skill_tree.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
        app_load_skill_file_force(s, skill_md, "SKILL.md");
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
            app_load_skill_file_force(s, first_f->abs_path, first_f->name);
            return;
        }
    }

    app_load_skill_file_force(s, NULL, "");
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
    s->tool_detail_tab = 0;
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
        app_load_skill_file_force(s, NULL, "");
    }

    s->current_page = PAGE_TOOL_SKILLS;
}

static void on_tool_skill_file_select(file_node_t *node, void *user_data) {
    app_state_t *s = (app_state_t *)user_data;
    if (s && node && !node->is_dir) {
        app_select_skill_file(s, node->abs_path, node->name);
        s->tool_detail_tab = 0;
    }
}

void render_tool_skills_page(cui_ctx *ui, app_state_t *s) {
    /* 1. Navigation Top Bar */
    cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
        if (cui_button(ui, "返回", .variant = CUI_BUTTON_GHOST,
                       .icon = CUI_ICON_CHEVRON_LEFT)) {
            s->current_page = (s->prev_page == PAGE_HUB_MATRIX) ? PAGE_HUB_MATRIX : PAGE_TOOL_LIST;
        }

        cui_gap(ui, 12);

        cui_box(ui, .width = CUI_PX(36), .height = CUI_PX(36), .radius = 8,
                .bg = cui_alpha(cui_color_of(ui, CUI_COLOR_ACCENT), 0.15f),
                .dir = CUI_STACK, .align = CUI_ALIGN_CENTER, .justify = CUI_JUSTIFY_CENTER) {
            cui_icon(ui, ICON_TOOLS, 18, CUI_COLOR_ACCENT);
        }

        cui_column(ui, .width = CUI_FILL, .gap = 2) {
            cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                char title_buf[256];
                snprintf(title_buf, sizeof(title_buf), "工具【%s】技能列表", s->active_tool_name);
                cui_heading(ui, title_buf, 4);

                cui_badge(ui, "实时目录读取", .tone = CUI_TONE_INFO, .size = CUI_SIZE_SM);
            }
            char path_sub[600];
            snprintf(path_sub, sizeof(path_sub), "路径: %s · 发现 %d 个 Skill 目录",
                     s->active_tool_path, s->skill_count);
            cui_text(ui, path_sub, .width = CUI_FILL, .nowrap = true,
                     .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);
        }

        cui_gap(ui, 8);

        if (s->active_skill_name[0] != '\0') {
            if (cui_button(ui, "卸载", .variant = CUI_BUTTON_GHOST,
                           .icon = CUI_ICON_TRASH_2, .color = CUI_COLOR_DANGER)) {
                s->show_uninstall_tool_modal = true;
                strncpy(s->uninstall_tool_skill_name, s->active_skill_name, sizeof(s->uninstall_tool_skill_name) - 1);
                s->uninstall_tool_skill_name[sizeof(s->uninstall_tool_skill_name) - 1] = '\0';
            }
        }

        /* 清空针对整个工具目录，不依赖当前选中的技能。 */
        if (cui_button(ui, "清空", .variant = CUI_BUTTON_GHOST,
                       .icon = CUI_ICON_TRASH_2, .color = CUI_COLOR_DANGER)) {
            request_clear_tool(s, s->active_tool_name, s->active_tool_path);
        }
    }

    float viewport_width = cui_viewport_size(ui).x;
    bool compact = viewport_width < COMPACT_WORKSPACE_WIDTH;
    float skill_list_width = 272 + (viewport_width - COMPACT_WORKSPACE_WIDTH) * 0.20f;
    if (skill_list_width > 336) skill_list_width = 336;
    if (compact) {
        static const char *tabs[] = { "文件预览", "技能目录", "文件树" };
        cui_tabs(ui, "tool_detail_tabs", tabs, 3, &s->tool_detail_tab);
    }
    int tab = s->tool_detail_tab;

    /* File preview takes the full workspace when the navigation panes are hidden. */
    cui_row(ui, .width = CUI_FILL, .height = CUI_FILL, .gap = 14) {

        if (!compact || tab == 1) {
            /* Left Pane: Skills List */
            cui_card(ui, .width = compact ? CUI_FILL : CUI_PX(skill_list_width),
                     .height = CUI_FILL, .pad = 12, .gap = 10) {
                cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER, .gap = 6) {
                    cui_label(ui, "技能目录");
                    cui_spacer(ui);
                    int total_prompt_tokens = 0;
                    for (int i = 0; i < s->skill_count; i++) {
                        total_prompt_tokens += s->skill_items[i].tokens.prompt_tokens;
                    }
                    if (total_prompt_tokens > 0) {
                        char tok_num[24], tok_badge[48];
                        skill_format_tokens(total_prompt_tokens, tok_num, sizeof(tok_num));
                        snprintf(tok_badge, sizeof(tok_badge), "常驻 ≈%s", tok_num);
                        cui_badge(ui, tok_badge,
                                  .tone = total_prompt_tokens > 4000 ? CUI_TONE_WARNING : CUI_TONE_NEUTRAL,
                                  .size = CUI_SIZE_SM);
                        cui_tooltip(ui, "该工具全部技能常驻注入（name + description）估算总 Token");
                    }
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
                            bool clicked = cui_list_item(ui, sk->name, .icon = ICON_SKILLS, .selected = is_sel);
                            char tooltip[sizeof sk->name + 96];
                            snprintf(tooltip, sizeof(tooltip), "%s\n%d 个文件 · 注入 ≈%d tok · 全文 ≈%d tok",
                                     sk->name, sk->file_count,
                                     sk->tokens.prompt_tokens, sk->tokens.full_tokens);
                            cui_tooltip(ui, tooltip);
                            if (clicked) {
                                select_active_skill(s, sk->name, sk->path);
                                s->tool_detail_tab = 0;
                            }
                            cui_pop_id(ui);
                        }
                    }
                }
            }
        }

        if (!compact || tab == 2) {
            /* Middle Pane: 技能目录结构树 (复用统一组件) */
            skill_tree_view_opts_t tree_opts = {
                .title = "文件树",
                .selected_path = s->selected_file_path,
                .match_by_rel_path = false,
                .show_diff_badges = false,
                .show_summary_badges = false,
                .width = compact ? CUI_FILL : CUI_PX(220),
            };
            render_skill_file_tree_component(ui, s->skill_file_tree, &tree_opts, on_tool_skill_file_select, s);
        }

        if (!compact || tab == 0) {
            render_skill_file_pane(ui, s, "tool", "从文件树选择要预览或编辑的文件");
        }
    }
}
