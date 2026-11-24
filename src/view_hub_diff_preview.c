#include "view_hub_diff_preview.h"
#include "app_hub_tasks.h"
#include "file_preview.h"
#include "view_common.h"

#include <stdio.h>
#include <string.h>

static void render_preview_toolbar(cui_ctx *ui, app_state_t *s,
                                   const hub_skill_entry_t *entry, const tool_copy_status_t *cur_tc,
                                   const hub_diff_file_item_t *cur_file) {
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
        }
    }

    if (cur_tc && cur_tc->exists) {
        cui_row(ui, .width = CUI_FILL, .gap = 8, .align = CUI_ALIGN_CENTER, .wrap = true) {
            cui_badge(ui, cur_tc->tool_name, .tone = CUI_TONE_NEUTRAL, .size = CUI_SIZE_SM);
            if (cui_button(ui, "副本设为基准", .variant = CUI_BUTTON_GHOST,
                           .size = CUI_SIZE_SM, .icon = ICON_COLLECT)) {
                start_promote_to_master_thread(s, cur_tc->tool_skills_path, entry->skill_name, cur_tc->tool_name, s->selected_hub_tool_idx);
            }

            if (entry->in_hub &&
                cui_button(ui, "用主仓覆盖副本", .variant = CUI_BUTTON_GHOST,
                           .size = CUI_SIZE_SM, .icon = ICON_OVERWRITE)) {
                start_sync_master_to_tool_thread(s, cur_tc->tool_skills_path, entry->skill_name, cur_tc->tool_name, s->selected_hub_tool_idx);
            }
        }
    }

    cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
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
}

static void render_compare_files(cui_ctx *ui, app_state_t *s, const hub_skill_entry_t *entry,
                                 const tool_copy_status_t *copy, bool identical,
                                 const char *old_text, const char *new_text) {
    char old_title[256], new_title[256];
    snprintf(old_title, sizeof(old_title), "主版本库 (%s/%s)", entry->hub_path, s->diff_file_rel);
    if (copy && copy->exists) {
        snprintf(new_title, sizeof(new_title), "工具副本【%s】 (%s/%s)", copy->tool_name,
                 copy->skill_path, s->diff_file_rel);
    } else {
        snprintf(new_title, sizeof(new_title), "工具副本 (未安装)");
    }

    const char *id = identical ? "hub_diff_control_sync" : "hub_diff_control";
    cui_diff_result result = cui_diff_view(ui, id, old_text, new_text,
                                          .mode = s->diff_mode,
                                          .language = detect_syntax_lang(s->diff_file_rel),
                                          .old_title = old_title, .new_title = new_title,
                                          .current = &s->diff_current_change, .height = -1);
    cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
        if (identical) {
            cui_icon(ui, CUI_ICON_CIRCLE_CHECK, 14, CUI_COLOR_SUCCESS);
            cui_text(ui, "两端文件内容完全一致，无文本差异", .size = 11, .color = CUI_COLOR_SUCCESS);
        } else if (result.changes > 0) {
            cui_textf(ui, "+%d 行 / -%d 行 (共 %d 处差异)", result.added, result.removed, result.changes);
        } else {
            cui_badge(ui, "未检测到文本行差异", .tone = CUI_TONE_NEUTRAL, .size = CUI_SIZE_SM);
        }
        cui_spacer(ui);
        cui_text(ui, "覆盖前自动生成快照备份", .size = 11, .color = CUI_COLOR_TEXT_MUTED);
    }
}

static bool preview_is_markdown(const char *path) {
    return strstr(path, ".md") != NULL || strstr(path, ".markdown") != NULL;
}

static void render_preview_document(cui_ctx *ui, const char *path, const char *text,
                                    const char *scroll_id, const char *document_id, bool is_markdown) {
    cui_scroll(ui, .id = scroll_id, .width = CUI_FILL, .height = CUI_FILL) {
        if (is_markdown) {
            cui_markdown(ui, document_id, text);
        } else {
            cui_code_block(ui, document_id, text, .lang = detect_syntax_lang(path), .line_numbers = true);
        }
    }
}

static void render_identical_preview(cui_ctx *ui, const app_state_t *s, const tool_copy_status_t *copy,
                                     const hub_diff_file_item_t *file, const char *text) {
    cui_card(ui, .width = CUI_FILL, .pad = 8, .gap = 4, .muted = true) {
        cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
            cui_icon(ui, CUI_ICON_CIRCLE_CHECK, 15, CUI_COLOR_SUCCESS);
            cui_text(ui, "主版本与当前工具副本内容完全一致，未发生分化",
                     .font = CUI_FONT_MEDIUM, .size = 12, .color = CUI_COLOR_SUCCESS);
            cui_spacer(ui);
            if (copy) cui_textf(ui, "%s · %zu B", copy->tool_name, file->tool_size);
        }
    }
    bool md = preview_is_markdown(s->diff_file_rel);
    render_preview_document(ui, s->diff_file_rel, text, md ? "sync_md_scroll" : "sync_code_scroll",
                            md ? "sync_md_view" : "sync_code_view", md);
}

static void render_one_sided_preview(cui_ctx *ui, const app_state_t *s, bool added, const char *text) {
    cui_card(ui, .width = CUI_FILL, .pad = 8, .gap = 4, .muted = true) {
        cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
            cui_icon(ui, added ? CUI_ICON_INFO : CUI_ICON_TRIANGLE_ALERT, 15,
                     added ? CUI_COLOR_ACCENT : CUI_COLOR_DANGER);
            cui_text(ui, added ? "此文件仅在本地工具副本中新增，主版本库中尚未收录"
                              : "此文件在主版本库中存在，但当前工具副本中已缺失",
                     .font = CUI_FONT_MEDIUM, .size = 12,
                     .color = added ? CUI_COLOR_ACCENT : CUI_COLOR_DANGER);
            cui_spacer(ui);
            cui_badge(ui, added ? "+ 本地独有" : "- 工具缺失",
                      .tone = added ? CUI_TONE_SUCCESS : CUI_TONE_DANGER, .size = CUI_SIZE_SM);
        }
    }
    bool md = preview_is_markdown(s->diff_file_rel);
    const char *scroll_id = added ? (md ? "add_md_scroll" : "add_code_scroll")
                                 : (md ? "del_md_scroll" : "del_code_scroll");
    const char *document_id = added ? (md ? "add_md_view" : "add_code_view")
                                   : (md ? "del_md_view" : "del_code_view");
    render_preview_document(ui, s->diff_file_rel, text, scroll_id, document_id, md);
}

static void render_preview_content(cui_ctx *ui, app_state_t *s, const hub_skill_entry_t *entry,
                                   const tool_copy_status_t *copy, const hub_diff_file_item_t *file) {
    const char *old_text = s->diff_old_text ? s->diff_old_text : "";
    const char *new_text = s->diff_new_text ? s->diff_new_text : "";
    if (file && file->status == DIFF_FILE_IN_SYNC && s->diff_sync_view_mode != 1) {
        render_identical_preview(ui, s, copy, file, new_text[0] ? new_text : old_text);
    } else if (file && (file->status == DIFF_FILE_ADDED || file->status == DIFF_FILE_DELETED)) {
        bool added = file->status == DIFF_FILE_ADDED;
        render_one_sided_preview(ui, s, added, added ? new_text : old_text);
    } else {
        render_compare_files(ui, s, entry, copy, file && file->status == DIFF_FILE_IN_SYNC,
                             old_text, new_text);
    }
}

void render_hub_diff_preview(cui_ctx *ui, app_state_t *s, const hub_skill_entry_t *entry) {
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
        render_preview_toolbar(ui, s, entry, cur_tc, cur_file);
        render_preview_content(ui, s, entry, cur_tc, cur_file);
    }
}
