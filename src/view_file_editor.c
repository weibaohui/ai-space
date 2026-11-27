#include "view_file_editor.h"
#include "file_preview.h"
#include "skill_tokens.h"
#include "app_hub_tasks.h"
#include "view_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EDIT_FILE_MAX_BYTES (1024 * 1024)
#define EDIT_FILE_HEADROOM  (64 * 1024)
#define EDIT_FILE_MIN_CAP   (256 * 1024)

static bool is_markdown_file(const char *name) {
    if (!name || !name[0]) return false;
    size_t n = strlen(name);
    return (n >= 3 && strcmp(name + n - 3, ".md") == 0) ||
           (n >= 9 && strcmp(name + n - 9, ".markdown") == 0);
}

static bool is_script_file(const char *name) {
    if (!name || !name[0]) return false;
    return strstr(name, ".sh") != NULL || strstr(name, ".bash") != NULL ||
           strstr(name, ".zsh") != NULL || strstr(name, ".py") != NULL ||
           strstr(name, ".js") != NULL || strstr(name, ".ts") != NULL;
}

static int count_lines(const char *text) {
    if (!text || !text[0]) return 0;
    int lines = 1;
    for (const char *p = text; *p; p++) {
        if (*p == '\n') lines++;
    }
    return lines;
}

void app_load_skill_file_force(app_state_t *s, const char *path, const char *name) {
    if (!s) return;
    if (s->selected_file_content) {
        free(s->selected_file_content);
        s->selected_file_content = NULL;
    }
    s->selected_file_cap = 0;
    s->selected_file_size = 0;
    s->selected_file_lines = 0;
    s->selected_file_is_binary = false;
    s->selected_file_editable = false;
    s->selected_file_dirty = false;

    if (!path || path[0] == '\0') {
        s->selected_file_path[0] = '\0';
        s->selected_file_name[0] = '\0';
        return;
    }

    snprintf(s->selected_file_path, sizeof(s->selected_file_path), "%s", path);
    if (name && name[0] != '\0') {
        snprintf(s->selected_file_name, sizeof(s->selected_file_name), "%s", name);
    } else {
        const char *slash = strrchr(path, '/');
        snprintf(s->selected_file_name, sizeof(s->selected_file_name), "%s", slash ? slash + 1 : path);
    }

    s->selected_file_content = read_file_for_edit(
        path, EDIT_FILE_MAX_BYTES, EDIT_FILE_HEADROOM, EDIT_FILE_MIN_CAP,
        &s->selected_file_cap, &s->selected_file_size, &s->selected_file_lines,
        &s->selected_file_is_binary, &s->selected_file_editable);
    s->doc_render_mode = is_markdown_file(s->selected_file_name) ? 0 : 1;
}

void app_select_skill_file(app_state_t *s, const char *path, const char *name) {
    if (!s) return;
    if (path && strcmp(s->selected_file_path, path) == 0) return;

    if (s->selected_file_dirty && s->selected_file_path[0] != '\0') {
        snprintf(s->pending_file_path, sizeof(s->pending_file_path), "%s", path ? path : "");
        snprintf(s->pending_file_name, sizeof(s->pending_file_name), "%s", name ? name : "");
        s->show_unsaved_file_modal = true;
        return;
    }
    app_load_skill_file_force(s, path, name);
}

bool app_save_active_skill_file(cui_ctx *ui, app_state_t *s) {
    if (!s || !s->selected_file_path[0] || !s->selected_file_editable || !s->selected_file_content) {
        return false;
    }
    size_t len = strlen(s->selected_file_content);
    if (write_file_atomic(s->selected_file_path, s->selected_file_content, len) != 0) {
        if (ui) cui_toast(ui, "保存失败，请检查文件权限", .tone = CUI_TONE_DANGER);
        return false;
    }
    s->selected_file_size = len;
    s->selected_file_lines = count_lines(s->selected_file_content);
    s->selected_file_dirty = false;

    /* 同步刷新当前工具技能项的 Token 估算与主仓矩阵状态。 */
    for (int i = 0; i < s->skill_count; i++) {
        if (strcmp(s->skill_items[i].name, s->active_skill_name) == 0) {
            skill_measure_directory_tokens(s->skill_items[i].path,
                                           s->skill_items[i].name,
                                           &s->skill_items[i].tokens);
            break;
        }
    }
    s->hub_matrix_loaded = false;
    if (ui) cui_toast(ui, "已保存修改", .tone = CUI_TONE_SUCCESS);
    return true;
}

void render_skill_file_pane(cui_ctx *ui, app_state_t *s, const char *scope_id, const char *empty_hint) {
    cui_card(ui, .width = CUI_FILL, .height = CUI_FILL, .pad = 14, .gap = 10) {
        bool is_md = is_markdown_file(s->selected_file_name);
        bool is_script = is_script_file(s->selected_file_name);

        cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER, .gap = 8) {
            cui_icon(ui, CUI_ICON_FILE_TEXT, 18, CUI_COLOR_ACCENT);
            cui_label(ui, s->selected_file_name[0] ? s->selected_file_name : "选择文件");

            if (is_md) {
                cui_badge(ui, "Markdown", .tone = CUI_TONE_ACCENT, .size = CUI_SIZE_SM);
            } else if (is_script) {
                cui_badge(ui, "脚本文件", .tone = CUI_TONE_SUCCESS, .size = CUI_SIZE_SM);
            } else if (s->selected_file_name[0] != '\0') {
                cui_badge(ui, "文本配置", .tone = CUI_TONE_INFO, .size = CUI_SIZE_SM);
            }

            if (s->selected_file_dirty) {
                cui_badge(ui, "未保存", .tone = CUI_TONE_WARNING, .size = CUI_SIZE_SM);
            }

            if (s->selected_file_path[0] != '\0') {
                size_t cur_len = (!s->selected_file_is_binary && s->selected_file_content)
                    ? strlen(s->selected_file_content)
                    : s->selected_file_size;
                int cur_lines = (!s->selected_file_is_binary && s->selected_file_content)
                    ? count_lines(s->selected_file_content)
                    : s->selected_file_lines;
                char sz_txt[32];
                format_size(cur_len, sz_txt, sizeof(sz_txt));
                if (!s->selected_file_is_binary && s->selected_file_content) {
                    char tok_txt[24];
                    int ftok = skill_estimate_tokens(s->selected_file_content, cur_len);
                    skill_format_tokens(ftok, tok_txt, sizeof(tok_txt));
                    cui_textf(ui, "%s · %d 行 · %s tok", sz_txt, cur_lines, tok_txt);
                } else {
                    cui_textf(ui, "%s · %d 行", sz_txt, cur_lines);
                }
            }

            cui_spacer(ui);

            if (s->selected_file_editable && s->selected_file_dirty) {
                if (cui_button(ui, "放弃", .variant = CUI_BUTTON_GHOST, .size = CUI_SIZE_SM)) {
                    int mode = s->doc_render_mode;
                    app_load_skill_file_force(s, s->selected_file_path, s->selected_file_name);
                    s->doc_render_mode = mode;
                    cui_toast(ui, "已放弃未保存的修改", .tone = CUI_TONE_INFO);
                }
                if (cui_button(ui, "保存", .variant = CUI_BUTTON_PRIMARY, .size = CUI_SIZE_SM,
                               .icon = CUI_ICON_SAVE)) {
                    app_save_active_skill_file(ui, s);
                }
            }
        }

        if (is_md && s->selected_file_editable) {
            static const char *doc_modes[] = { "预览", "编辑" };
            char tabs_id[64];
            snprintf(tabs_id, sizeof(tabs_id), "%s_doc_mode_tabs", scope_id);
            cui_tabs(ui, tabs_id, doc_modes, 2, &s->doc_render_mode);
        }

        if (s->selected_file_path[0] != '\0') {
            cui_text(ui, s->selected_file_path, .width = CUI_FILL, .nowrap = true,
                     .size = 11, .color = CUI_COLOR_TEXT_MUTED);
        }

        cui_separator(ui);

        if (s->selected_file_path[0] == '\0') {
            cui_column(ui, .width = CUI_FILL, .height = CUI_FILL,
                       .align = CUI_ALIGN_CENTER, .justify = CUI_JUSTIFY_CENTER) {
                cui_icon(ui, CUI_ICON_FILE_TEXT, 36, CUI_COLOR_TEXT_MUTED);
                cui_gap(ui, 8);
                cui_text(ui, empty_hint ? empty_hint : "从文件树选择要预览的文件",
                         .color = CUI_COLOR_TEXT_MUTED);
            }
        } else if (s->selected_file_is_binary) {
            cui_column(ui, .width = CUI_FILL, .height = CUI_FILL,
                       .align = CUI_ALIGN_CENTER, .justify = CUI_JUSTIFY_CENTER) {
                cui_icon(ui, CUI_ICON_INFO, 28, CUI_COLOR_TEXT_MUTED);
                cui_gap(ui, 8);
                cui_muted(ui, "二进制文件不支持直接预览或编辑");
            }
        } else if (s->selected_file_content &&
                   (s->selected_file_content[0] != '\0' || s->selected_file_editable)) {
            if (is_md && s->doc_render_mode == 0) {
                char scroll_id[64], md_id[64];
                snprintf(scroll_id, sizeof(scroll_id), "%s_md_scroll", scope_id);
                snprintf(md_id, sizeof(md_id), "%s_md_preview", scope_id);
                cui_scroll(ui, .id = scroll_id, .width = CUI_FILL, .height = CUI_FILL) {
                    cui_markdown(ui, md_id, s->selected_file_content);
                }
            } else if (s->selected_file_editable && s->selected_file_cap > 0) {
                const char *lang = detect_syntax_lang(s->selected_file_name);
                if (strcmp(lang, "markdown") == 0) lang = "text";

                char editor_id[64];
                snprintf(editor_id, sizeof(editor_id), "%s_code_editor", scope_id);
                cui_code_editor_result r = cui_code_editor(
                    ui, editor_id, s->selected_file_content, s->selected_file_cap,
                    .lang = lang, .width = CUI_FILL, .height = -26.0f,
                    .placeholder = "在此输入文件内容...");
                if (r.changed) {
                    s->selected_file_dirty = true;
                }

                cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER, .gap = 12) {
                    cui_textf(ui, "Ln %d, Col %d · 共 %d 行", r.line, r.column, r.line_count);
                    if (r.find_matches > 0) {
                        cui_textf(ui, "匹配 %d/%d", r.find_index, r.find_matches);
                    }
                    cui_spacer(ui);
                    if (r.refused) {
                        cui_text(ui, "已达到单文件 1MB 编辑上限", .size = 11, .color = CUI_COLOR_DANGER);
                    } else {
                        cui_text(ui, "Ctrl/Cmd+F 查找 · Ctrl/Cmd+G 跳行 · Ctrl/Cmd+Z 撤销",
                                 .size = 11, .color = CUI_COLOR_TEXT_MUTED);
                    }
                }
            } else {
                const char *lang = detect_syntax_lang(s->selected_file_name);
                char scroll_id[64], code_id[64];
                snprintf(scroll_id, sizeof(scroll_id), "%s_ro_scroll", scope_id);
                snprintf(code_id, sizeof(code_id), "%s_ro_view", scope_id);
                cui_scroll(ui, .id = scroll_id, .width = CUI_FILL, .height = CUI_FILL) {
                    cui_code_block(ui, code_id, s->selected_file_content,
                                   .lang = lang, .line_numbers = true);
                }
            }
        } else {
            cui_column(ui, .width = CUI_FILL, .height = CUI_FILL,
                       .align = CUI_ALIGN_CENTER, .justify = CUI_JUSTIFY_CENTER) {
                cui_icon(ui, CUI_ICON_INFO, 28, CUI_COLOR_TEXT_MUTED);
                cui_gap(ui, 8);
                cui_muted(ui, "该文件为空或暂无内容");
            }
        }
    }
}

void render_unsaved_file_modal(cui_ctx *ui, app_state_t *s) {
    if (!s || !s->show_unsaved_file_modal) return;
    cui_dialog(ui, "有未保存的修改", &s->show_unsaved_file_modal, .width = 440) {
        cui_column(ui, .width = CUI_FILL, .gap = 12) {
            cui_textf(ui, "文件「%s」尚未保存，切换后当前编辑内容将会丢失。",
                      s->selected_file_name[0] ? s->selected_file_name : "当前文件");
            cui_row(ui, .width = CUI_FILL, .justify = CUI_JUSTIFY_END, .gap = 8) {
                if (cui_button(ui, "取消", .variant = CUI_BUTTON_SECONDARY)) {
                    s->show_unsaved_file_modal = false;
                }
                if (cui_button(ui, "放弃修改", .variant = CUI_BUTTON_DANGER)) {
                    s->show_unsaved_file_modal = false;
                    app_load_skill_file_force(s, s->pending_file_path, s->pending_file_name);
                }
                if (cui_button(ui, "保存并切换", .variant = CUI_BUTTON_PRIMARY, .icon = CUI_ICON_SAVE)) {
                    if (app_save_active_skill_file(ui, s)) {
                        s->show_unsaved_file_modal = false;
                        app_load_skill_file_force(s, s->pending_file_path, s->pending_file_name);
                    }
                }
            }
        }
    }
}
