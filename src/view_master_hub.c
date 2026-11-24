#include "hub_files.h"
#include "skill_file_tree.h"
#include "file_preview.h"
#include "app_hub_tasks.h"
#include "app_diff.h"
#include "view_master_hub.h"
#include "view_common.h"
#include "view_skill_tree.h"
#include "view_hub_diff.h"
#include "skill_scanner.h"
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

    /* 主仓目录不存在时，用第一个可用副本打开浏览器。 */
    struct stat st_dir;
    if (stat(s->active_skill_path, &st_dir) != 0) {
        const hub_skill_entry_t *entry = hub_find_skill(&s->hub_matrix, skill_name);
        for (int i = 0; entry && i < entry->tool_count; i++) {
            const tool_copy_status_t *copy = &entry->tool_copies[i];
            if (!copy->exists || !copy->skill_path[0]) continue;
            strncpy(s->active_skill_path, copy->skill_path, sizeof(s->active_skill_path) - 1);
            break;
        }
    }

    if (s->master_skill_file_tree) {
        free_skill_file_tree(s->master_skill_file_tree);
        s->master_skill_file_tree = NULL;
    }
    s->master_skill_file_tree = build_skill_file_tree(s->active_skill_path, "", 0, 10);

    /* 切换技能时释放旧的预览内容。 */
    s->selected_file_path[0] = '\0';
    s->selected_file_name[0] = '\0';
    if (s->selected_file_content) {
        free(s->selected_file_content);
        s->selected_file_content = NULL;
    }
    s->selected_file_size = 0;
    s->selected_file_lines = 0;

    /* 默认打开技能入口文件。 */
    char skill_md_path[1024];
    snprintf(skill_md_path, sizeof(skill_md_path), "%s/SKILL.md", s->active_skill_path);
    struct stat st;
    if (stat(skill_md_path, &st) == 0) {
        strncpy(s->selected_file_path, skill_md_path, sizeof(s->selected_file_path) - 1);
        strncpy(s->selected_file_name, "SKILL.md", sizeof(s->selected_file_name) - 1);
        s->selected_file_content = read_file_preview(skill_md_path, 256 * 1024, NULL,
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

    s->selected_file_content = read_file_preview(node->abs_path, 256 * 1024, NULL,
                                                      &s->selected_file_size,
                                                      &s->selected_file_lines,
                                                      &s->selected_file_is_binary);
}

void render_master_hub_explorer_view(cui_ctx *ui, app_state_t *s) {
    const hub_skill_entry_t *entry = hub_find_skill(&s->hub_matrix, s->active_skill_name);
    bool is_in_hub = entry && entry->in_hub;

    /* 顶部显示技能来源和收录入口。 */
    cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
        if (cui_button(ui, "返回", .variant = CUI_BUTTON_GHOST, .icon = CUI_ICON_CHEVRON_LEFT)) {
            s->hub_subview = s->prev_hub_subview;
        }

        cui_gap(ui, 12);
        cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
            cui_heading(ui, s->active_skill_name, 3);
            if (is_in_hub) {
                cui_badge(ui, "主仓基准版本", .tone = CUI_TONE_SUCCESS, .size = CUI_SIZE_SM);
            } else {
                cui_badge(ui, "未收录主仓", .tone = CUI_TONE_VIOLET, .size = CUI_SIZE_SM);
            }
        }

        if (!is_in_hub) {
            cui_gap(ui, 8);
            if (cui_button(ui, "收录", .variant = CUI_BUTTON_PRIMARY,
                           .icon = ICON_COLLECT, .size = CUI_SIZE_SM)) {
                for (int t = 0; entry && t < entry->tool_count; t++) {
                    const tool_copy_status_t *copy = &entry->tool_copies[t];
                    if (!copy->exists) continue;
                    start_promote_to_master_thread(s, copy->tool_skills_path, s->active_skill_name,
                                                   copy->tool_name, t);
                    break;
                }
            }
        }

        cui_spacer(ui);
        cui_text(ui, s->active_skill_path, .size = 12, .color = CUI_COLOR_TEXT_MUTED);
    }

    /* 目录树和文件预览并排展示。 */
    cui_row(ui, .width = CUI_FILL, .height = CUI_FILL, .gap = 12) {
        skill_tree_view_opts_t tree_opts = {
            .title = is_in_hub ? "主仓目录结构" : "技能目录结构",
            .selected_path = s->selected_file_path,
            .match_by_rel_path = false,
            .show_diff_badges = false,
            .show_summary_badges = false,
            .width = CUI_PX(220),
        };
        render_skill_file_tree_component(ui, s->master_skill_file_tree, &tree_opts, on_master_file_select, s);

        /* 文件预览。 */
        cui_card(ui, .width = CUI_FILL, .height = CUI_FILL, .pad = 14, .gap = 10) {
            bool is_md = (strstr(s->selected_file_name, ".md") != NULL ||
                          strstr(s->selected_file_name, ".markdown") != NULL);
            cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                    cui_icon(ui, CUI_ICON_FILE_TEXT, 18, CUI_COLOR_ACCENT);
                    cui_label(ui, s->selected_file_name[0] ? s->selected_file_name : "请在左侧选择文件");

                    if (is_md) {
                        cui_badge(ui, "Markdown", .tone = CUI_TONE_ACCENT, .size = CUI_SIZE_SM);
                    }

                    if (s->selected_file_size > 0) {
                        char sz_txt[32];
                        format_size(s->selected_file_size, sz_txt, sizeof(sz_txt));
                        cui_textf(ui, "%s · %d 行", sz_txt, s->selected_file_lines);
                    }
                }
            }

            if (is_md) {
                static const char *doc_modes[] = { "预览", "源码" };
                cui_tabs(ui, "master_doc_render_mode_tabs", doc_modes, 2, &s->doc_render_mode);
            }

            if (s->selected_file_path[0] != '\0') {
                cui_text(ui, s->selected_file_path, .size = 11, .color = CUI_COLOR_TEXT_MUTED);
            }

            cui_separator(ui);

            if (s->selected_file_content && s->selected_file_content[0] != '\0') {
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

typedef enum {
    MASTER_FILTER_ALL,
    MASTER_FILTER_CHANGED,
    MASTER_FILTER_UNDISTRIBUTED,
} master_skill_filter_t;

void render_master_hub_skills_view(cui_ctx *ui, app_state_t *s) {
    /* 主仓、覆盖工具和副本差异概览。 */
    cui_row(ui, .width = CUI_FILL, .gap = 14) {
        char buf_hub[32], buf_tool[32], buf_drift[32];
        snprintf(buf_hub, sizeof(buf_hub), "%d 个", s->hub_matrix.hub_managed_skills);

        /* 每个工具只计一次覆盖。 */
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

        /* 按存在差异的工具副本计数。 */
        int master_drift_copies = 0;
        for (int i = 0; i < s->hub_matrix.skill_count; i++) {
            hub_skill_entry_t *entry = &s->hub_matrix.skills[i];
            if (entry->in_hub) {
                master_drift_copies += (entry->modified_count + entry->outdated_count);
            }
        }
        snprintf(buf_drift, sizeof(buf_drift), "%d 处", master_drift_copies);

        cui_metric(ui, "主仓技能", buf_hub, .icon = ICON_MASTER_HUB, .width = CUI_GROW(1));
        cui_metric(ui, "覆盖工具", buf_tool, .icon = ICON_TOOLS, .width = CUI_GROW(1));
        cui_metric(ui, "副本差异", buf_drift, .icon = ICON_COMPARE, .width = CUI_GROW(1));
    }

    cui_row(ui, .width = CUI_FILL, .gap = 12, .align = CUI_ALIGN_CENTER) {
        static const char *filters[] = { "全部", "有差异", "未分发" };
        cui_search(ui, "search_master_skills", s->master_skill_search_kw, sizeof(s->master_skill_search_kw),
                   .placeholder = "搜索主仓技能名称或描述", .width = CUI_FILL);
        cui_segmented(ui, "master_skill_filter", filters, 3, &s->master_skill_filter, .size = CUI_SIZE_SM);
    }

    int selected = 0;
    for (int i = 0; i < s->hub_matrix.skill_count; i++) {
        const hub_skill_entry_t *entry = &s->hub_matrix.skills[i];
        if (entry->in_hub && entry->selected) selected++;
    }
    bool busy = s->hub_scan.is_scanning || s->sync_is_busy || s->op_progress.active;
    if (selected > 0) {
        cui_row(ui, .width = CUI_FILL, .gap = 8, .align = CUI_ALIGN_CENTER) {
            char selection[32];
            snprintf(selection, sizeof(selection), "已选 %d 项", selected);
            cui_badge(ui, selection, .tone = CUI_TONE_ACCENT, .size = CUI_SIZE_SM);
            if (cui_button(ui, "取消选择", .variant = CUI_BUTTON_GHOST, .size = CUI_SIZE_SM)) {
                for (int i = 0; i < s->hub_matrix.skill_count; i++) s->hub_matrix.skills[i].selected = false;
            }
            cui_spacer(ui);
            if (cui_button(ui, "批量移出", .variant = CUI_BUTTON_GHOST, .size = CUI_SIZE_SM,
                           .icon = CUI_ICON_TRASH_2, .color = CUI_COLOR_DANGER, .disabled = busy)) {
                s->remove_master_skill[0] = '\0';
                s->show_remove_master_modal = true;
            }
        }
    }

    /* 按筛选条件展示主仓技能卡片。 */
    if (s->hub_matrix.hub_managed_skills == 0) {
        /* 无匹配技能时显示空状态。 */
        cui_card(ui, .width = CUI_FILL, .height = CUI_FILL, .pad = 40) {
            cui_column(ui, .width = CUI_FILL, .height = CUI_FILL,
                       .align = CUI_ALIGN_CENTER, .justify = CUI_JUSTIFY_CENTER) {
                cui_icon(ui, CUI_ICON_ARCHIVE, 48, CUI_COLOR_TEXT_MUTED);
                cui_gap(ui, 16);
                cui_heading(ui, "主仓尚未收录基准技能", 3);
                cui_gap(ui, 6);
                cui_text(ui, "可前往【技能列表】收录本机已发现的技能，或在主仓目录直接创建技能。",
                         .color = CUI_COLOR_TEXT_SECONDARY, .size = 13);
                cui_gap(ui, 20);
                if (cui_button(ui, "前往收录", .variant = CUI_BUTTON_PRIMARY, .icon = ICON_COLLECT)) {
                    s->current_page = PAGE_ALL_SKILLS;
                    s->hub_subview = HUB_SUBVIEW_SCAN_MATRIX;
                    s->hub_filter_mode = SKILL_FILTER_UNCOLLECTED;
                    s->hub_search_kw[0] = '\0';
                }
            }
        }
    } else {
        cui_scroll(ui, .id = "master_skills_scroll_list", .width = CUI_FILL, .height = CUI_FILL) {
            cui_column(ui, .width = CUI_FILL, .gap = 12) {
                static int s_last_master_card = -1;
                static double s_last_master_time = 0.0;

                int matched = 0;
                for (int i = 0; i < s->hub_matrix.skill_count; i++) {
                    hub_skill_entry_t *entry = &s->hub_matrix.skills[i];
                    if (!entry->in_hub) continue;
                    if (s->master_skill_filter == MASTER_FILTER_CHANGED &&
                        entry->modified_count == 0 && entry->outdated_count == 0 &&
                        !entry->hub_unreadable && entry->unreadable_count == 0) continue;
                    if (s->master_skill_filter == MASTER_FILTER_UNDISTRIBUTED && entry->installed_count > 0) continue;

                    if (s->master_skill_search_kw[0] != '\0') {
                        if (!ci_contains(entry->skill_name, s->master_skill_search_kw) &&
                            !ci_contains(entry->description, s->master_skill_search_kw)) {
                            continue;
                        }
                    }
                    matched++;

                    cui_push_id_int(ui, i);
                    bool card_clicked = cui_card_begin(ui, &(cui_card_opts){
                        .width = CUI_FILL, .pad = 14, .gap = 10, .hoverable = true,
                        .selected = (s_last_master_card == i)
                    });
                    if (card_clicked) {
                        double now = cui_time(ui);
                        if (s_last_master_card == i && (now - s_last_master_time) < 0.40) {
                            s_last_master_card = -1;
                            s_last_master_time = 0.0;
                            open_master_skill_explorer(s, entry->skill_name);
                        } else {
                            s_last_master_card = i;
                            s_last_master_time = now;
                        }
                    }
                    cui_row(ui, .width = CUI_FILL, .gap = 8, .align = CUI_ALIGN_CENTER) {
                        cui_checkbox(ui, "", &entry->selected);
                        cui_icon(ui, ICON_MASTER_HUB, 16, CUI_COLOR_TEXT_SECONDARY);
                        cui_column(ui, .width = CUI_FILL, .gap = 4) {
                            cui_text(ui, entry->skill_name, .font = CUI_FONT_MEDIUM, .size = 15,
                                     .width = CUI_FILL, .nowrap = true);
                            if (entry->hub_unreadable || entry->unreadable_count > 0) {
                                cui_badge(ui, "无法比较", .tone = CUI_TONE_WARNING, .size = CUI_SIZE_SM);
                            } else if (entry->modified_count > 0) {
                                cui_badge(ui, "副本有改动", .tone = CUI_TONE_WARNING, .size = CUI_SIZE_SM);
                            } else if (entry->outdated_count > 0) {
                                cui_badge(ui, "副本待更新", .tone = CUI_TONE_INFO, .size = CUI_SIZE_SM);
                            } else if (entry->installed_count > 0) {
                                cui_badge(ui, "副本已一致", .tone = CUI_TONE_SUCCESS, .size = CUI_SIZE_SM);
                            } else {
                                cui_badge(ui, "未分发", .tone = CUI_TONE_NEUTRAL, .size = CUI_SIZE_SM);
                            }
                        }

                        cui_row(ui, .gap = 6, .align = CUI_ALIGN_CENTER) {
                            if (cui_button(ui, "浏览文件", .variant = CUI_BUTTON_GHOST,
                                           .icon = CUI_ICON_FOLDER_OPEN, .size = CUI_SIZE_SM)) {
                                open_master_skill_explorer(s, entry->skill_name);
                            }
                            if (entry->installed_count > 0 &&
                                cui_button(ui, "对比", .variant = CUI_BUTTON_GHOST,
                                           .icon = ICON_COMPARE, .size = CUI_SIZE_SM)) {
                                s->prev_hub_subview = HUB_SUBVIEW_MASTER_SKILLS;
                                open_hub_diff_view(s, entry->skill_name);
                            }
                            if (cui_button(ui, "分发", .variant = CUI_BUTTON_GHOST,
                                           .icon = ICON_DISTRIBUTE, .size = CUI_SIZE_SM)) {
                                s->show_distribute_modal = true;
                                snprintf(s->distribute_skill_name, sizeof(s->distribute_skill_name), "%s", entry->skill_name);
                            }
                            cui_menu(ui, "更多操作", .icon_only = true, .variant = CUI_BUTTON_GHOST) {
                                if (cui_menu_item(ui, "移出主仓", .icon = CUI_ICON_TRASH_2,
                                                  .danger = true, .disabled = busy)) {
                                    snprintf(s->remove_master_skill, sizeof(s->remove_master_skill), "%s", entry->skill_name);
                                    s->show_remove_master_modal = true;
                                }
                            }
                        }
                    }

                    if (entry->description[0]) {
                        cui_text(ui, entry->description, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY,
                                 .width = CUI_FILL, .nowrap = true);
                    }

                    /* 工具徽标展示该技能的分发状态。 */
                    cui_row(ui, .width = CUI_FILL, .gap = 6, .align = CUI_ALIGN_CENTER, .wrap = true) {
                        cui_textf(ui, "分发覆盖 (%d/%d):", entry->installed_count, s->hub_matrix.tool_count);
                        if (entry->installed_count > 0) {
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
                                } else if (tc->status == HUB_STATUS_UNREADABLE) {
                                    tone = CUI_TONE_WARNING;
                                    snprintf(label, sizeof(label), "%s (无法比较)", tc->tool_name);
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
                    cui_card_end(ui);
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

static bool is_master_removal_target(const app_state_t *s, const hub_skill_entry_t *entry) {
    if (!entry->in_hub) return false;
    if (s->remove_master_skill[0] == '\0') return entry->selected;
    return strcmp(s->remove_master_skill, entry->skill_name) == 0;
}

void render_remove_master_modal(cui_ctx *ui, app_state_t *s) {
    if (!s->show_remove_master_modal) return;

    bool busy = s->hub_scan.is_scanning || s->sync_is_busy || s->op_progress.active;
    int count = 0;
    if (!busy) {
        for (int i = 0; i < s->hub_matrix.skill_count; i++) {
            if (is_master_removal_target(s, &s->hub_matrix.skills[i])) count++;
        }
    }

    cui_dialog(ui, "移出主仓", &s->show_remove_master_modal, .width = 480) {
        cui_column(ui, .width = CUI_FILL, .gap = 14) {
            if (busy) {
                cui_text(ui, "主仓正在更新，请稍后重试。", .width = CUI_FILL);
            } else if (count == 0) {
                cui_text(ui, "选中的技能已不在主仓。", .width = CUI_FILL);
            } else if (s->remove_master_skill[0]) {
                cui_text(ui, s->remove_master_skill, .font = CUI_FONT_MEDIUM, .width = CUI_FILL);
            } else {
                cui_textf(ui, "将选中的 %d 个技能移出主仓。", count);
                cui_scroll(ui, .id = "remove_master_targets", .width = CUI_FILL,
                           .height = CUI_PX((count < 4 ? count : 4) * 24), .gap = 4) {
                    for (int i = 0; i < s->hub_matrix.skill_count; i++) {
                        hub_skill_entry_t *entry = &s->hub_matrix.skills[i];
                        if (is_master_removal_target(s, entry)) {
                            cui_text(ui, entry->skill_name, .width = CUI_FILL);
                        }
                    }
                }
            }
            if (count > 0) {
                cui_text(ui, "主仓中的基准版本将被移除，已分发到本地工具的副本会保留。",
                         .color = CUI_COLOR_TEXT_SECONDARY, .width = CUI_FILL);
            }

            cui_row(ui, .width = CUI_FILL, .gap = 8, .justify = CUI_JUSTIFY_END) {
                if (cui_button(ui, "取消", .variant = CUI_BUTTON_OUTLINE)) {
                    s->show_remove_master_modal = false;
                }
                if (cui_button(ui, "确认移出", .variant = CUI_BUTTON_DANGER,
                               .disabled = count == 0 || busy)) {
                    int removed = 0;
                    for (int i = 0; i < s->hub_matrix.skill_count; i++) {
                        hub_skill_entry_t *entry = &s->hub_matrix.skills[i];
                        if (is_master_removal_target(s, entry) &&
                            hub_delete_master_skill(entry->skill_name) == 0) {
                            entry->selected = false;
                            removed++;
                        }
                    }

                    char result[128];
                    if (removed == count) {
                        snprintf(result, sizeof(result), "已移出 %d 个技能", removed);
                    } else {
                        snprintf(result, sizeof(result), "已移出 %d 个技能，%d 个移出失败", removed, count - removed);
                    }
                    cui_toast(ui, result, .tone = removed == count ? CUI_TONE_SUCCESS : CUI_TONE_WARNING);
                    s->show_remove_master_modal = false;
                    app_storage_invalidate(s);
                    start_hub_scan_thread(s);
                }
            }
        }
    }
}
