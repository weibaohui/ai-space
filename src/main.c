#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <cui/cui.h>
#include <cui/cui_app.h>
#include "db.h"
#include "models.h"
#include "skill_scanner.h"
#include "hub_manager.h"

typedef enum {
    PAGE_TOOL_LIST = 0,
    PAGE_HUB_MATRIX = 1,
    PAGE_TOOL_SKILLS = 2,
} page_view_t;

typedef struct {
    sqlite3 *db;
    char db_path[512];

    page_view_t current_page;
    page_view_t prev_page;

    /* Tool list state */
    tool_record_t *records;
    int record_count;
    tool_stats_t stats;

    char search_kw[128];
    char last_search_kw[128];

    cui_table_sort sort;
    int last_sort_col;
    bool last_sort_desc;

    char toast_msg[256];
    int toast_frames;
    bool needs_reload;

    /* Central Hub & Relation Matrix state */
    hub_matrix_t hub_matrix;
    bool hub_matrix_loaded;
    char hub_search_kw[128];
    int hub_filter_mode;        /* 0: 全部, 1: 存在分化, 2: 未建立主仓, 3: 完全一致 */
    int hub_subview;            /* 0: 关系全景表, 1: 差异对比与同步面板 */

    /* Hub Detail & Diff state */
    char selected_hub_skill[128];
    int selected_hub_tool_idx;  /* Index into entry->tool_copies */
    char diff_file_rel[256];    /* Relative path e.g. "SKILL.md" */
    char *diff_old_text;        /* Master Hub text */
    char *diff_new_text;        /* Tool copy text */
    int diff_current_change;    /* Selected change index for cui_diff_view */
    cui_diff_mode diff_mode;    /* CUI_DIFF_SPLIT (0) or CUI_DIFF_INLINE (1) */

    /* Tool skills view state */
    char active_tool_name[128];
    char active_tool_path[512];
    skill_item_t *skill_items;
    int skill_count;
    char skill_search_kw[128];
    int skill_view_mode;        /* 0: 树形浏览 (左树右内容), 1: 全部技能清单 */
    cui_table_sort skill_table_sort;

    /* Selected skill state */
    char active_skill_name[128];
    char active_skill_path[1024];
    file_node_t *skill_file_tree;

    /* Selected file state */
    char selected_file_path[1024];
    char selected_file_name[128];
    char *selected_file_content;
    size_t selected_file_size;
    int selected_file_lines;
    bool selected_file_is_binary;
    int doc_render_mode;        /* 0: 预览/高亮, 1: 源码/代码 */
} app_state_t;

static bool ci_contains(const char *haystack, const char *needle) {
    if (!haystack || !needle || needle[0] == '\0') return true;
    size_t nlen = strlen(needle);
    size_t hlen = strlen(haystack);
    if (nlen > hlen) return false;
    for (size_t i = 0; i <= hlen - nlen; i++) {
        size_t j = 0;
        while (j < nlen && tolower((unsigned char)haystack[i + j]) == tolower((unsigned char)needle[j])) {
            j++;
        }
        if (j == nlen) return true;
    }
    return false;
}

static void format_size(size_t bytes, char *buf, size_t cap) {
    if (bytes < 1024) {
        snprintf(buf, cap, "%zu B", bytes);
    } else if (bytes < 1024 * 1024) {
        snprintf(buf, cap, "%.1f KB", (double)bytes / 1024.0);
    } else {
        snprintf(buf, cap, "%.2f MB", (double)bytes / (1024.0 * 1024.0));
    }
}

static void reload_db_data(app_state_t *s) {
    db_query_stats(s->db, &s->stats);

    if (s->records) {
        free(s->records);
        s->records = NULL;
        s->record_count = 0;
    }

    db_query_tools(s->db, &s->records, &s->record_count,
                   s->search_kw, s->sort.column, s->sort.descending);

    strncpy(s->last_search_kw, s->search_kw, sizeof(s->last_search_kw) - 1);
    s->last_sort_col = s->sort.column;
    s->last_sort_desc = s->sort.descending;
    s->needs_reload = false;
}

static void do_scan_sync(app_state_t *s) {
    int inserted = 0, updated = 0, deleted = 0;
    if (db_scan_and_sync(s->db, &inserted, &updated, &deleted) == 0) {
        snprintf(s->toast_msg, sizeof(s->toast_msg),
                 "扫描同步完成: 新增 %d 个, 更新 %d 个, 移除 %d 个",
                 inserted, updated, deleted);
    } else {
        snprintf(s->toast_msg, sizeof(s->toast_msg), "扫描同步失败，请检查文件权限");
    }
    s->toast_frames = 240;
    reload_db_data(s);
}

static void clear_diff_texts(app_state_t *s) {
    if (s->diff_old_text) {
        free(s->diff_old_text);
        s->diff_old_text = NULL;
    }
    if (s->diff_new_text) {
        free(s->diff_new_text);
        s->diff_new_text = NULL;
    }
    s->diff_current_change = 0;
}

static void reload_hub_matrix(app_state_t *s) {
    hub_free_matrix(&s->hub_matrix);
    hub_scan_matrix(s->db, &s->hub_matrix);
    s->hub_matrix_loaded = true;
}

static void load_hub_diff(app_state_t *s, const char *skill_name, int tool_idx, const char *rel_file) {
    clear_diff_texts(s);
    if (!skill_name || skill_name[0] == '\0') {
        s->selected_hub_skill[0] = '\0';
        s->selected_hub_tool_idx = -1;
        s->diff_file_rel[0] = '\0';
        return;
    }

    strncpy(s->selected_hub_skill, skill_name, sizeof(s->selected_hub_skill) - 1);
    s->selected_hub_skill[sizeof(s->selected_hub_skill) - 1] = '\0';
    s->selected_hub_tool_idx = tool_idx;

    if (rel_file && rel_file[0] != '\0') {
        strncpy(s->diff_file_rel, rel_file, sizeof(s->diff_file_rel) - 1);
    } else {
        strncpy(s->diff_file_rel, "SKILL.md", sizeof(s->diff_file_rel) - 1);
    }
    s->diff_file_rel[sizeof(s->diff_file_rel) - 1] = '\0';

    hub_skill_entry_t *entry = NULL;
    for (int i = 0; i < s->hub_matrix.skill_count; i++) {
        if (strcmp(s->hub_matrix.skills[i].skill_name, skill_name) == 0) {
            entry = &s->hub_matrix.skills[i];
            break;
        }
    }
    if (!entry) return;

    /* 1. Master Hub text */
    if (entry->in_hub) {
        char m_path[1200];
        snprintf(m_path, sizeof(m_path), "%s/%s", entry->hub_path, s->diff_file_rel);
        size_t m_sz = 0;
        int m_lines = 0;
        bool m_bin = false;
        s->diff_old_text = read_file_text_content(m_path, 1024 * 1024, &m_sz, &m_lines, &m_bin);
    }
    if (!s->diff_old_text) {
        s->diff_old_text = strdup("");
    }

    /* 2. Tool copy text */
    if (tool_idx >= 0 && tool_idx < entry->tool_count && entry->tool_copies[tool_idx].exists) {
        char t_path[1200];
        snprintf(t_path, sizeof(t_path), "%s/%s", entry->tool_copies[tool_idx].skill_path, s->diff_file_rel);
        size_t t_sz = 0;
        int t_lines = 0;
        bool t_bin = false;
        s->diff_new_text = read_file_text_content(t_path, 1024 * 1024, &t_sz, &t_lines, &t_bin);
    }
    if (!s->diff_new_text) {
        s->diff_new_text = strdup("");
    }
}

static void open_hub_diff_view(app_state_t *s, const char *skill_name) {
    if (!s->hub_matrix_loaded) reload_hub_matrix(s);

    hub_skill_entry_t *entry = NULL;
    for (int i = 0; i < s->hub_matrix.skill_count; i++) {
        if (strcmp(s->hub_matrix.skills[i].skill_name, skill_name) == 0) {
            entry = &s->hub_matrix.skills[i];
            break;
        }
    }
    if (!entry) return;

    int chosen_idx = -1;
    for (int t = 0; t < entry->tool_count; t++) {
        if (entry->tool_copies[t].exists) {
            if (entry->tool_copies[t].status == HUB_STATUS_LOCAL_MODIFIED ||
                entry->tool_copies[t].status == HUB_STATUS_OUTDATED) {
                chosen_idx = t;
                break;
            }
            if (chosen_idx == -1) chosen_idx = t;
        }
    }
    if (chosen_idx == -1 && entry->tool_count > 0) chosen_idx = 0;

    s->hub_subview = 1;
    load_hub_diff(s, skill_name, chosen_idx, "SKILL.md");
}

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
    /* Read up to 2MB */
    s->selected_file_content = read_file_text_content(path, 2 * 1024 * 1024, &sz, &lines, &is_bin);
    s->selected_file_size = sz;
    s->selected_file_lines = lines;
    s->selected_file_is_binary = is_bin;

    /* If it's a markdown file, default doc_render_mode to 0 (Markdown preview), otherwise 1 (code view) */
    if (strstr(s->selected_file_name, ".md") || strstr(s->selected_file_name, ".markdown")) {
        s->doc_render_mode = 0;
    } else {
        s->doc_render_mode = 1;
    }
}

static void select_active_skill(app_state_t *s, const char *name, const char *path) {
    if (!name || !path) return;

    strncpy(s->active_skill_name, name, sizeof(s->active_skill_name) - 1);
    s->active_skill_name[sizeof(s->active_skill_name) - 1] = '\0';

    strncpy(s->active_skill_path, path, sizeof(s->active_skill_path) - 1);
    s->active_skill_path[sizeof(s->active_skill_path) - 1] = '\0';

    if (s->skill_file_tree) {
        free_skill_file_tree(s->skill_file_tree);
        free(s->skill_file_tree);
        s->skill_file_tree = NULL;
    }

    s->skill_file_tree = build_skill_file_tree(path, "", 0, 6);

    /* Priority 1: If SKILL.md exists, open it directly */
    char skill_md[1200];
    snprintf(skill_md, sizeof(skill_md), "%s/SKILL.md", path);
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

static void open_tool_skills_page(app_state_t *s, const char *tool_name, const char *tool_path) {
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

static void render_file_tree_node(cui_ctx *ui, app_state_t *s, file_node_t *node) {
    if (!node) return;

    if (node->is_dir) {
        cui_push_id(ui, node->abs_path);
        cui_tree(ui, node->name, &node->is_open,
                 .icon = node->is_open ? CUI_ICON_FOLDER_OPEN : CUI_ICON_FOLDER) {
            for (int i = 0; i < node->child_count; i++) {
                render_file_tree_node(ui, s, &node->children[i]);
            }
        }
        cui_pop_id(ui);
    } else {
        cui_push_id(ui, node->abs_path);
        bool dummy = false;
        bool is_sel = (strcmp(s->selected_file_path, node->abs_path) == 0);

        const char *icon = CUI_ICON_FILE_TEXT;
        if (strstr(node->name, ".md") || strstr(node->name, ".markdown")) {
            icon = CUI_ICON_BOOKMARK;
        } else if (strstr(node->name, ".py") || strstr(node->name, ".c") ||
                   strstr(node->name, ".h") || strstr(node->name, ".sh") ||
                   strstr(node->name, ".bash") || strstr(node->name, ".zsh") ||
                   strstr(node->name, ".js") || strstr(node->name, ".ts")) {
            icon = CUI_ICON_CODE;
        }

        /* Note: Leaf nodes return false from cui_tree_begin, so click check must be OUTSIDE braces */
        cui_tree(ui, node->name, &dummy, .icon = icon, .leaf = true, .selected = is_sel) {}
        if (cui_tree_clicked(ui)) {
            load_skill_file(s, node->abs_path, node->name);
        }

        cui_pop_id(ui);
    }
}

/* -------------------- Global Navigation Bar -------------------- */

static void render_global_navbar(cui_ctx *ui, app_state_t *s) {
    cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
        cui_row(ui, .gap = 10, .align = CUI_ALIGN_CENTER) {
            cui_box(ui, .width = CUI_PX(30), .height = CUI_PX(30), .radius = 8,
                    .bg = cui_alpha(cui_color_of(ui, CUI_COLOR_ACCENT), 0.18f),
                    .dir = CUI_STACK, .align = CUI_ALIGN_CENTER, .justify = CUI_JUSTIFY_CENTER) {
                cui_icon(ui, CUI_ICON_LAYERS, 16, CUI_COLOR_ACCENT);
            }
            cui_text(ui, "AI-Space", .font = CUI_FONT_DISPLAY, .size = 17, .color = CUI_COLOR_ACCENT);
            cui_text(ui, " · ", .color = CUI_COLOR_TEXT_MUTED);

            /* Tab 1: 本地工具目录 */
            bool is_tools = (s->current_page == PAGE_TOOL_LIST);
            if (cui_button(ui, "  本地工具目录",
                           .variant = is_tools ? CUI_BUTTON_PRIMARY : CUI_BUTTON_SECONDARY,
                           .size = CUI_SIZE_SM,
                           .icon = CUI_ICON_FOLDER)) {
                s->current_page = PAGE_TOOL_LIST;
            }

            /* Tab 2: 技能中心与全景关系 */
            bool is_hub = (s->current_page == PAGE_HUB_MATRIX);
            if (cui_button(ui, "  统一技能中心与全景关系",
                           .variant = is_hub ? CUI_BUTTON_PRIMARY : CUI_BUTTON_SECONDARY,
                           .size = CUI_SIZE_SM,
                           .icon = CUI_ICON_GRID_3X3)) {
                if (!s->hub_matrix_loaded) reload_hub_matrix(s);
                s->current_page = PAGE_HUB_MATRIX;
                s->hub_subview = 0;
            }
        }

        cui_spacer(ui);

        if (s->toast_frames > 0 && s->toast_msg[0] != '\0') {
            cui_row(ui, .gap = 6, .align = CUI_ALIGN_CENTER) {
                cui_icon(ui, CUI_ICON_INFO, 14, CUI_COLOR_ACCENT);
                cui_text(ui, s->toast_msg, .size = 13, .color = CUI_COLOR_ACCENT);
            }
        }
    }
}

/* -------------------- Subview: Side-by-Side Diff & Sync -------------------- */

static void render_hub_diff_subview(cui_ctx *ui, app_state_t *s) {
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

        cui_gap(ui, 10);

        cui_box(ui, .width = CUI_PX(36), .height = CUI_PX(36), .radius = 8,
                .bg = cui_alpha(cui_color_of(ui, CUI_COLOR_ACCENT), 0.15f),
                .dir = CUI_STACK, .align = CUI_ALIGN_CENTER, .justify = CUI_JUSTIFY_CENTER) {
            cui_icon(ui, CUI_ICON_LAYERS, 18, CUI_COLOR_ACCENT);
        }

        cui_column(ui, .gap = 2) {
            cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                char t_title[256];
                snprintf(t_title, sizeof(t_title), "技能对比与同步: %s", entry->skill_name);
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
                cui_text(ui, entry->description, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);
            }
        }

        cui_spacer(ui);

        /* Action: Broadcast to all tools */
        if (entry->in_hub) {
            if (cui_button(ui, " 🚀 全量广播主版本到所有工具", .variant = CUI_BUTTON_PRIMARY, .icon = CUI_ICON_SHARE_2)) {
                int count = 0;
                if (hub_broadcast_skill(&s->hub_matrix, entry->skill_name, &count) == 0) {
                    snprintf(s->toast_msg, sizeof(s->toast_msg),
                             "已全量广播主版本覆盖至 %d 个工具副本（均已安全备份）", count);
                    s->toast_frames = 240;
                    reload_hub_matrix(s);
                    load_hub_diff(s, entry->skill_name, s->selected_hub_tool_idx, s->diff_file_rel);
                }
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
                    cui_text(ui, "尚未建立主版本，请在下方点击某个工具的【采纳为主版本】",
                             .size = 11, .color = CUI_COLOR_TEXT_MUTED);
                }
            }

            cui_separator(ui);

            /* 2. Tool Copies Header */
            cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                cui_label(ui, "各工具物理副本");
                cui_spacer(ui);
                char copies_cnt[32];
                snprintf(copies_cnt, sizeof(copies_cnt), "%d / %d 已装", entry->installed_count, entry->tool_count);
                cui_text(ui, copies_cnt, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);
            }

            /* Scrollable Tool Copies */
            cui_scroll(ui, .id = "diff_tools_scroll", .width = CUI_FILL, .height = CUI_FILL) {
                cui_column(ui, .width = CUI_FILL, .gap = 8) {
                    for (int t = 0; t < entry->tool_count; t++) {
                        tool_copy_status_t *tc = &entry->tool_copies[t];
                        bool is_cur = (s->selected_hub_tool_idx == t);

                        cui_push_id_int(ui, t);
                        cui_card(ui, .width = CUI_FILL, .pad = 10, .gap = 6, .selected = is_cur) {

                            /* Tool Title & Status Badge */
                            cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                                cui_row(ui, .gap = 6, .align = CUI_ALIGN_CENTER) {
                                    cui_icon(ui, CUI_ICON_FOLDER, 14, is_cur ? CUI_COLOR_ACCENT : CUI_COLOR_FOREGROUND);
                                    cui_text(ui, tc->tool_name, .font = CUI_FONT_MEDIUM);
                                }
                                cui_spacer(ui);

                                if (!tc->exists) {
                                    cui_badge(ui, "未安装", .tone = CUI_TONE_NEUTRAL, .size = CUI_SIZE_SM);
                                } else if (tc->status == HUB_STATUS_IN_SYNC) {
                                    cui_badge(ui, "完全一致", .tone = CUI_TONE_SUCCESS, .size = CUI_SIZE_SM);
                                } else if (tc->status == HUB_STATUS_LOCAL_MODIFIED) {
                                    cui_badge(ui, "本地已改动", .tone = CUI_TONE_WARNING, .size = CUI_SIZE_SM);
                                } else if (tc->status == HUB_STATUS_OUTDATED) {
                                    cui_badge(ui, "落后主仓", .tone = CUI_TONE_INFO, .size = CUI_SIZE_SM);
                                } else {
                                    cui_badge(ui, "本地独立", .tone = CUI_TONE_VIOLET, .size = CUI_SIZE_SM);
                                }
                            }

                            if (tc->exists) {
                                char tc_sub[64];
                                snprintf(tc_sub, sizeof(tc_sub), "指纹: %.12s...", tc->hash);
                                cui_text(ui, tc_sub, .size = 11, .color = CUI_COLOR_TEXT_SECONDARY);

                                /* Buttons Row */
                                cui_row(ui, .gap = 6, .align = CUI_ALIGN_CENTER) {
                                    if (cui_button(ui, is_cur ? "当前比对" : "对比差异",
                                                   .variant = is_cur ? CUI_BUTTON_PRIMARY : CUI_BUTTON_OUTLINE,
                                                   .size = CUI_SIZE_SM, .icon = CUI_ICON_SEARCH)) {
                                        s->selected_hub_tool_idx = t;
                                        load_hub_diff(s, entry->skill_name, t, s->diff_file_rel);
                                    }

                                    if (cui_button(ui, "采纳为主版本",
                                                   .variant = CUI_BUTTON_OUTLINE,
                                                   .size = CUI_SIZE_SM, .icon = CUI_ICON_CHECK)) {
                                        if (hub_promote_to_master(tc->tool_skills_path, entry->skill_name) == 0) {
                                            snprintf(s->toast_msg, sizeof(s->toast_msg),
                                                     "已将 [%s] 副本采纳为主版本", tc->tool_name);
                                            s->toast_frames = 240;
                                            reload_hub_matrix(s);
                                            load_hub_diff(s, entry->skill_name, t, s->diff_file_rel);
                                        }
                                    }

                                    if (entry->in_hub) {
                                        if (cui_button(ui, "下发覆盖",
                                                       .variant = CUI_BUTTON_SECONDARY,
                                                       .size = CUI_SIZE_SM, .icon = CUI_ICON_DOWNLOAD)) {
                                            if (hub_sync_master_to_tool(tc->tool_skills_path, entry->skill_name, tc->tool_name) == 0) {
                                                snprintf(s->toast_msg, sizeof(s->toast_msg),
                                                         "已同步主版本到 [%s]（已自动备份）", tc->tool_name);
                                                s->toast_frames = 240;
                                                reload_hub_matrix(s);
                                                load_hub_diff(s, entry->skill_name, t, s->diff_file_rel);
                                            }
                                        }
                                    }
                                }
                            } else {
                                /* Not installed */
                                cui_row(ui, .gap = 6, .align = CUI_ALIGN_CENTER) {
                                    cui_text(ui, "此工具尚无该技能副本", .size = 11, .color = CUI_COLOR_TEXT_MUTED);
                                    cui_spacer(ui);
                                    if (entry->in_hub) {
                                        if (cui_button(ui, "复制安装",
                                                       .variant = CUI_BUTTON_OUTLINE,
                                                       .size = CUI_SIZE_SM, .icon = CUI_ICON_PLUS)) {
                                            if (hub_sync_master_to_tool(tc->tool_skills_path, entry->skill_name, tc->tool_name) == 0) {
                                                snprintf(s->toast_msg, sizeof(s->toast_msg),
                                                         "已将主版本复制安装至 [%s]", tc->tool_name);
                                                s->toast_frames = 240;
                                                reload_hub_matrix(s);
                                                load_hub_diff(s, entry->skill_name, t, s->diff_file_rel);
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

        /* Right Pane: Embedded CUI Diff View */
        cui_card(ui, .width = CUI_FILL, .height = CUI_FILL, .pad = 12, .gap = 8) {
            tool_copy_status_t *cur_tc = (s->selected_hub_tool_idx >= 0 && s->selected_hub_tool_idx < entry->tool_count) ?
                                         &entry->tool_copies[s->selected_hub_tool_idx] : NULL;

            /* Diff Toolbar */
            cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                    cui_icon(ui, CUI_ICON_CODE, 16, CUI_COLOR_ACCENT);
                    cui_text(ui, "比对文件: ", .color = CUI_COLOR_TEXT_SECONDARY);
                    cui_badge(ui, s->diff_file_rel[0] ? s->diff_file_rel : "SKILL.md",
                              .tone = CUI_TONE_ACCENT, .size = CUI_SIZE_SM);

                    if (cur_tc && cur_tc->exists) {
                        cui_text(ui, "  基准: 主仓版本 ⟷ 对比: ", .size = 12, .color = CUI_COLOR_TEXT_MUTED);
                        cui_badge(ui, cur_tc->tool_name, .tone = CUI_TONE_INFO, .size = CUI_SIZE_SM);
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
}

/* -------------------- View 2: Central Hub & Relation Matrix -------------------- */

static void render_hub_matrix_page(cui_ctx *ui, app_state_t *s) {
    if (!s->hub_matrix_loaded) {
        reload_hub_matrix(s);
    }

    if (s->toast_frames > 0) {
        s->toast_frames--;
    }

    /* 1. Global Navigation Bar */
    render_global_navbar(ui, s);

    /* 2. Check Subview: 0 = Overview Matrix, 1 = Side-by-Side Diff & Sync */
    if (s->hub_subview == 1) {
        render_hub_diff_subview(ui, s);
        return;
    }

    /* Subview 0: Matrix Overview */
    /* 2.1 Header Area */
    cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
        cui_row(ui, .gap = 12, .align = CUI_ALIGN_CENTER) {
            cui_box(ui, .width = CUI_PX(42), .height = CUI_PX(42), .radius = 10,
                    .bg = cui_alpha(cui_color_of(ui, CUI_COLOR_ACCENT), 0.15f),
                    .dir = CUI_STACK, .align = CUI_ALIGN_CENTER, .justify = CUI_JUSTIFY_CENTER) {
                cui_icon(ui, CUI_ICON_LAYERS, 22, CUI_COLOR_ACCENT);
            }
            cui_column(ui, .gap = 3) {
                cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                    cui_heading(ui, "统一技能中心与版本关系管理", 3);
                    cui_badge(ui, "物理复制同步", .tone = CUI_TONE_SUCCESS, .size = CUI_SIZE_SM);
                    cui_badge(ui, "自动快照备份", .tone = CUI_TONE_INFO, .size = CUI_SIZE_SM);
                }
                cui_text(ui, "集中维护技能主版本 (~/.ai-space/skills)，自动跟踪多工具物理副本分化与漂移，支持可视化对比与一键同步",
                         .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);
            }
        }

        cui_spacer(ui);

        /* Action Buttons */
        cui_row(ui, .gap = 10, .align = CUI_ALIGN_CENTER) {
            if (cui_button(ui, " 重新比对全景", .variant = CUI_BUTTON_OUTLINE, .icon = CUI_ICON_REFRESH_CW)) {
                reload_hub_matrix(s);
                snprintf(s->toast_msg, sizeof(s->toast_msg), "已重新比对全局技能分化关系");
                s->toast_frames = 200;
            }
            if (cui_button(ui, " 一键全量纳管主仓", .variant = CUI_BUTTON_PRIMARY, .icon = CUI_ICON_CHECK)) {
                int adopted = 0;
                if (hub_adopt_all_to_master(&s->hub_matrix, &adopted) == 0) {
                    snprintf(s->toast_msg, sizeof(s->toast_msg), "成功将 %d 个技能全量收录纳管入主版本库 (~/.ai-space/skills)", adopted);
                } else {
                    snprintf(s->toast_msg, sizeof(s->toast_msg), "纳管失败，请检查文件权限");
                }
                s->toast_frames = 240;
                reload_hub_matrix(s);
            }
        }
    }

    /* 2.2 Metric KPI Cards */
    cui_row(ui, .width = CUI_FILL, .gap = 14) {
        char buf_uniq[32], buf_hub[32], buf_drift[32], buf_tool[32];
        snprintf(buf_uniq, sizeof(buf_uniq), "%d 个", s->hub_matrix.total_unique_skills);
        snprintf(buf_hub, sizeof(buf_hub), "%d 个", s->hub_matrix.hub_managed_skills);
        snprintf(buf_drift, sizeof(buf_drift), "%d 个", s->hub_matrix.drifted_skills_count);
        snprintf(buf_tool, sizeof(buf_tool), "%d 个", s->hub_matrix.tool_count);

        cui_card(ui, .width = CUI_GROW(1), .pad = 16) {
            cui_metric(ui, "独立技能总数", buf_uniq, .icon = CUI_ICON_LAYERS);
        }
        cui_card(ui, .width = CUI_GROW(1), .pad = 16) {
            cui_metric(ui, "主版本已收录", buf_hub, .icon = CUI_ICON_CIRCLE_CHECK);
        }
        cui_card(ui, .width = CUI_GROW(1), .pad = 16) {
            cui_metric(ui, "存在分化 / 待同步", buf_drift, .icon = CUI_ICON_TRIANGLE_ALERT);
        }
        cui_card(ui, .width = CUI_GROW(1), .pad = 16) {
            cui_metric(ui, "涉及本地工具", buf_tool, .icon = CUI_ICON_BOX);
        }
    }

    /* 2.3 Search and Filter Tabs */
    cui_row(ui, .width = CUI_FILL, .gap = 12, .align = CUI_ALIGN_CENTER) {
        cui_search(ui, "hub_search_kw_box", s->hub_search_kw, sizeof(s->hub_search_kw),
                   .placeholder = "按技能名或描述搜索...", .width = CUI_PX(280));

        cui_row(ui, .gap = 6, .align = CUI_ALIGN_CENTER) {
            if (cui_button(ui, "全部技能",
                           .variant = (s->hub_filter_mode == 0) ? CUI_BUTTON_PRIMARY : CUI_BUTTON_SECONDARY,
                           .size = CUI_SIZE_SM)) {
                s->hub_filter_mode = 0;
            }
            char drift_lbl[64];
            snprintf(drift_lbl, sizeof(drift_lbl), "存在分化 / 改动 (%d)", s->hub_matrix.drifted_skills_count);
            if (cui_button(ui, drift_lbl,
                           .variant = (s->hub_filter_mode == 1) ? CUI_BUTTON_PRIMARY : CUI_BUTTON_SECONDARY,
                           .size = CUI_SIZE_SM)) {
                s->hub_filter_mode = 1;
            }
            int unman_count = s->hub_matrix.total_unique_skills - s->hub_matrix.hub_managed_skills;
            char unman_lbl[64];
            snprintf(unman_lbl, sizeof(unman_lbl), "未建立主仓 (%d)", unman_count);
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

        cui_spacer(ui);
        cui_textf(ui, "主版本库基准: %s", s->hub_matrix.hub_dir);
    }

    /* 2.4 Main Matrix Table */
    static const cui_table_column hub_cols[] = {
        { "技能名称与描述",               CUI_PX(260), CUI_TEXT_LEFT,   true  },
        { "主仓状态",                     CUI_PX(140), CUI_TEXT_LEFT,   true  },
        { "各工具物理副本状态 (关系矩阵)", CUI_FILL,   CUI_TEXT_LEFT,   false },
        { "分布",                         CUI_PX(90),  CUI_TEXT_CENTER, true  },
        { "同步与管理操作",               CUI_PX(210), CUI_TEXT_CENTER, false },
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
                                    .description = "切换上方筛选标签或点击重新比对全景",
                                    .icon = CUI_ICON_INBOX,
                                    .action = "重新比对全景")) {
                    reload_hub_matrix(s);
                }
            }
        } else {
            cui_table(ui, "hub_matrix_table", hub_cols, 5,
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
                            cui_icon(ui, CUI_ICON_LAYERS, 14, CUI_COLOR_ACCENT);
                            cui_text(ui, e->skill_name, .font = CUI_FONT_MEDIUM);
                        }
                        if (e->description[0] != '\0') {
                            cui_text(ui, e->description, .size = 11, .color = CUI_COLOR_TEXT_SECONDARY);
                        }
                    }

                    /* Col 1: Master Hub Status */
                    cui_table_cell(ui);
                    if (!e->in_hub) {
                        cui_badge(ui, "未建立主仓", .tone = CUI_TONE_VIOLET, .size = CUI_SIZE_SM);
                    } else if (e->modified_count > 0) {
                        char mod_buf[32];
                        snprintf(mod_buf, sizeof(mod_buf), "%d处改动", e->modified_count);
                        cui_badge(ui, mod_buf, .tone = CUI_TONE_WARNING, .size = CUI_SIZE_SM);
                    } else if (e->outdated_count > 0) {
                        char out_buf[32];
                        snprintf(out_buf, sizeof(out_buf), "%d处待更新", e->outdated_count);
                        cui_badge(ui, out_buf, .tone = CUI_TONE_INFO, .size = CUI_SIZE_SM);
                    } else {
                        cui_badge(ui, "全副本一致", .tone = CUI_TONE_SUCCESS, .size = CUI_SIZE_SM);
                    }

                    /* Col 2: Tool copies presence & drift relations */
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
                            cui_text(ui, "暂无工具安装", .size = 12, .color = CUI_COLOR_TEXT_MUTED);
                        }
                    }

                    /* Col 3: Distribution */
                    cui_table_cell(ui);
                    cui_textf(ui, "%d 工具", e->installed_count);

                    /* Col 4: Action Buttons */
                    cui_table_cell(ui);
                    cui_row(ui, .gap = 6, .align = CUI_ALIGN_CENTER) {
                        if (cui_button(ui, " 对比同步", .variant = CUI_BUTTON_OUTLINE,
                                       .size = CUI_SIZE_SM, .icon = CUI_ICON_SEARCH)) {
                            open_hub_diff_view(s, e->skill_name);
                        }

                        if (e->in_hub) {
                            if (cui_button(ui, " 广播", .variant = CUI_BUTTON_SECONDARY,
                                           .size = CUI_SIZE_SM, .icon = CUI_ICON_SHARE_2)) {
                                int synced = 0;
                                if (hub_broadcast_skill(&s->hub_matrix, e->skill_name, &synced) == 0) {
                                    snprintf(s->toast_msg, sizeof(s->toast_msg),
                                             "已将主版本广播覆盖至 %d 个工具副本", synced);
                                    s->toast_frames = 240;
                                    reload_hub_matrix(s);
                                }
                            }
                        } else {
                            if (cui_button(ui, " 纳管", .variant = CUI_BUTTON_PRIMARY,
                                           .size = CUI_SIZE_SM, .icon = CUI_ICON_CHECK)) {
                                for (int t = 0; t < e->tool_count; t++) {
                                    if (e->tool_copies[t].exists) {
                                        hub_promote_to_master(e->tool_copies[t].tool_skills_path, e->skill_name);
                                        snprintf(s->toast_msg, sizeof(s->toast_msg),
                                                 "已将 [%s] 副本采纳为技能主版本", e->tool_copies[t].tool_name);
                                        s->toast_frames = 240;
                                        reload_hub_matrix(s);
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

/* -------------------- View 1: Main Tools List (Database Driven) -------------------- */

static void render_tools_list_page(cui_ctx *ui, app_state_t *s) {
    if (strcmp(s->search_kw, s->last_search_kw) != 0 ||
        s->sort.column != s->last_sort_col ||
        s->sort.descending != s->last_sort_desc ||
        s->needs_reload) {
        reload_db_data(s);
    }

    if (s->toast_frames > 0) {
        s->toast_frames--;
    }

    /* 0. Global Navigation Bar */
    render_global_navbar(ui, s);

    /* 1. Top Header Area */
    cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
        cui_row(ui, .gap = 12, .align = CUI_ALIGN_CENTER) {
            cui_box(ui, .width = CUI_PX(42), .height = CUI_PX(42), .radius = 10,
                    .bg = cui_alpha(cui_color_of(ui, CUI_COLOR_ACCENT), 0.15f),
                    .dir = CUI_STACK, .align = CUI_ALIGN_CENTER, .justify = CUI_JUSTIFY_CENTER) {
                cui_icon(ui, CUI_ICON_BOX, 22, CUI_COLOR_ACCENT);
            }
            cui_column(ui, .gap = 3) {
                cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                    cui_heading(ui, "本地工具识别与展示", 3);
                    cui_badge(ui, "SQLite 本地存储", .tone = CUI_TONE_ACCENT, .size = CUI_SIZE_SM);
                }
                cui_text(ui, "按 ~/.xxx/skills 目录自动发现并持久化存储，点击右侧按钮查看技能详情",
                         .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);
            }
        }

        cui_spacer(ui);

        /* Action Buttons */
        cui_row(ui, .gap = 10, .align = CUI_ALIGN_CENTER) {
            if (cui_button(ui, " 重新扫描同步", .variant = CUI_BUTTON_PRIMARY, .icon = CUI_ICON_REFRESH_CW)) {
                do_scan_sync(s);
            }
            if (cui_button(ui, " 统一技能中心与版本关系", .variant = CUI_BUTTON_OUTLINE, .icon = CUI_ICON_LAYERS)) {
                if (!s->hub_matrix_loaded) reload_hub_matrix(s);
                s->current_page = PAGE_HUB_MATRIX;
                s->hub_subview = 0;
            }
        }
    }

    /* 2. Statistical Metric Cards */
    cui_row(ui, .width = CUI_FILL, .gap = 14) {
        char buf_tot[32], buf_skl[32];
        snprintf(buf_tot, sizeof(buf_tot), "%d 项", s->stats.total_tools);
        snprintf(buf_skl, sizeof(buf_skl), "%d 个", s->stats.total_skills);

        cui_card(ui, .width = CUI_GROW(1), .pad = 16) {
            cui_metric(ui, "本地工具总数", buf_tot, .icon = CUI_ICON_BOX);
        }
        cui_card(ui, .width = CUI_GROW(1), .pad = 16) {
            cui_metric(ui, "关联技能总数", buf_skl, .icon = CUI_ICON_LAYERS);
        }
        cui_card(ui, .width = CUI_GROW(1), .pad = 16) {
            cui_metric(ui, "发现规则路径", "~/.xxx/skills", .icon = CUI_ICON_FOLDER);
        }
        cui_card(ui, .width = CUI_GROW(1), .pad = 16) {
            const char *last_t = s->stats.last_scanned_at[0] ? s->stats.last_scanned_at : "刚刚";
            cui_metric(ui, "最近同步时间", last_t, .icon = CUI_ICON_CLOCK);
        }
    }

    /* 3. Search Bar */
    cui_row(ui, .width = CUI_FILL, .gap = 12, .align = CUI_ALIGN_CENTER) {
        cui_search(ui, "tool_search_box", s->search_kw, sizeof(s->search_kw),
                   .placeholder = "按工具名或路径搜索...", .width = CUI_PX(320));

        cui_spacer(ui);

        char count_str[64];
        snprintf(count_str, sizeof(count_str), "显示 %d / %d 项", s->record_count, s->stats.total_tools);
        cui_text(ui, count_str, .size = 13, .color = CUI_COLOR_TEXT_SECONDARY);
    }

    /* 4. Main Data Table */
    static const cui_table_column cols[] = {
        { "ID",             CUI_PX(52),  CUI_TEXT_CENTER, true  },
        { "工具名称 (xxx)", CUI_PX(160), CUI_TEXT_LEFT,   true  },
        { "技能数",         CUI_PX(84),  CUI_TEXT_RIGHT,  true  },
        { "发现路径 (~/.xxx/skills)", CUI_FILL, CUI_TEXT_LEFT, false },
        { "首次入库时间",   CUI_PX(170), CUI_TEXT_LEFT,   true  },
        { "最后更新时间",   CUI_PX(170), CUI_TEXT_LEFT,   true  },
        { "操作",           CUI_PX(130), CUI_TEXT_CENTER, false },
    };

    cui_card(ui, .width = CUI_FILL, .height = CUI_FILL, .pad = 0, .gap = 0) {
        if (s->record_count == 0) {
            cui_column(ui, .width = CUI_FILL, .height = CUI_FILL,
                       .align = CUI_ALIGN_CENTER, .justify = CUI_JUSTIFY_CENTER) {
                if (cui_empty_state(ui, "暂无匹配的本地工具记录",
                                    .description = "点击下方按钮重新扫描系统目录",
                                    .icon = CUI_ICON_INBOX,
                                    .action = "立即扫描同步")) {
                    do_scan_sync(s);
                }
            }
        } else {
            cui_table(ui, "tools_grid", cols, 7,
                      .sort = &s->sort, .striped = true, .compact = true,
                      .selectable = true, .height = CUI_FILL) {
                for (int i = 0; i < s->record_count; i++) {
                    tool_record_t *r = &s->records[i];
                    cui_push_id_int(ui, r->id);
                    cui_table_row(ui, false);

                    /* Col 0: ID */
                    cui_table_cell_textf(ui, "%d", r->id);

                    /* Col 1: Tool Name */
                    cui_table_cell(ui);
                    cui_row(ui, .gap = 6, .align = CUI_ALIGN_CENTER) {
                        cui_icon(ui, CUI_ICON_FOLDER, 14, CUI_COLOR_ACCENT);
                        cui_text(ui, r->name, .font = CUI_FONT_MEDIUM);
                    }

                    /* Col 2: Skill Count */
                    cui_table_cell(ui);
                    char sc_buf[16];
                    snprintf(sc_buf, sizeof(sc_buf), "%d 个", r->skill_count);
                    cui_text(ui, sc_buf, .tabular = true,
                             .color = (r->skill_count > 0) ? CUI_COLOR_FOREGROUND : CUI_COLOR_TEXT_MUTED);

                    /* Col 3: Path */
                    cui_table_cell_text(ui, r->path);

                    /* Col 4: Created At */
                    cui_table_cell_text(ui, r->created_at);

                    /* Col 5: Updated At */
                    cui_table_cell_text(ui, r->updated_at);

                    /* Col 6: Action Button -> "查看技能" */
                    cui_table_cell(ui);
                    if (cui_button(ui, " 查看技能", .variant = CUI_BUTTON_OUTLINE,
                                   .size = CUI_SIZE_SM, .icon = CUI_ICON_FOLDER)) {
                        open_tool_skills_page(s, r->name, r->path);
                    }

                    cui_pop_id(ui);
                }
            }
        }
    }

    /* 5. Bottom Status Bar */
    cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
        cui_textf(ui, "数据库: %s", s->db_path);

        if (s->toast_frames > 0 && s->toast_msg[0] != '\0') {
            cui_text(ui, " · ", .color = CUI_COLOR_TEXT_MUTED);
            cui_text(ui, s->toast_msg, .color = CUI_COLOR_ACCENT);
        }

        cui_spacer(ui);

        if (s->stats.last_scanned_at[0] != '\0') {
            cui_textf(ui, "最后扫描: %s", s->stats.last_scanned_at);
        }
    }
}

/* -------------------- View 2: Tool's Skills Page (On-the-fly Directory) -------------------- */

static void render_tool_skills_page(cui_ctx *ui, app_state_t *s) {
    /* 1. Navigation Top Bar */
    cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
        if (cui_button(ui, " 返回上一页", .variant = CUI_BUTTON_SECONDARY,
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

        /* View mode toggle */
        static const char *view_modes[] = { "树形结构与内容", "全部技能清单" };
        cui_tabs(ui, "skills_view_mode_tabs", view_modes, 2, &s->skill_view_mode);
    }

    /* 2. Main Content Body */
    if (s->skill_view_mode == 0) {
        /* Layout: Left Skills List (250px) | Middle File Tree (280px) | Right Content Viewer (Fill) */
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

            /* Middle Pane: 左树 (Skill 目录与文件树) */
            cui_card(ui, .width = CUI_PX(280), .height = CUI_FILL, .pad = 12, .gap = 10) {
                cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                    cui_label(ui, "目录结构树");
                    cui_spacer(ui);
                    if (s->active_skill_name[0] != '\0') {
                        cui_badge(ui, s->active_skill_name, .tone = CUI_TONE_ACCENT, .size = CUI_SIZE_SM);
                    }
                }

                cui_separator(ui);

                cui_scroll(ui, .id = "tree_pane_scroll", .width = CUI_FILL, .height = CUI_FILL) {
                    if (s->skill_file_tree && s->skill_file_tree->child_count > 0) {
                        for (int i = 0; i < s->skill_file_tree->child_count; i++) {
                            render_file_tree_node(ui, s, &s->skill_file_tree->children[i]);
                        }
                    } else {
                        cui_column(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER,
                                   .justify = CUI_JUSTIFY_CENTER) {
                            cui_gap(ui, 40);
                            cui_muted(ui, "当前技能目录下无文件");
                        }
                    }
                }
            }

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
                            cui_badge(ui, sz_txt, .tone = CUI_TONE_NEUTRAL, .size = CUI_SIZE_SM);
                        }

                        if (s->selected_file_lines > 0) {
                            char ln_txt[32];
                            snprintf(ln_txt, sizeof(ln_txt), "%d 行", s->selected_file_lines);
                            cui_badge(ui, ln_txt, .tone = CUI_TONE_NEUTRAL, .size = CUI_SIZE_SM);
                        }
                    }

                    cui_spacer(ui);

                    /* Action buttons: toggle render mode & copy */
                    cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                        bool is_md = (strstr(s->selected_file_name, ".md") != NULL ||
                                      strstr(s->selected_file_name, ".markdown") != NULL);
                        if (is_md) {
                            if (cui_button(ui, s->doc_render_mode == 0 ? " 代码/源码" : " Markdown 渲染",
                                           .variant = CUI_BUTTON_OUTLINE, .size = CUI_SIZE_SM,
                                           .icon = s->doc_render_mode == 0 ? CUI_ICON_CODE : CUI_ICON_BOOKMARK)) {
                                s->doc_render_mode = s->doc_render_mode == 0 ? 1 : 0;
                            }
                        }

                        if (s->selected_file_content && !s->selected_file_is_binary) {
                            cui_copy_button(ui, "copy_file_content_btn", s->selected_file_content);
                        }
                    }
                }

                cui_separator(ui);

                /* Content scroll container */
                cui_scroll(ui, .id = "file_view_scroll", .width = CUI_FILL, .height = CUI_FILL) {
                    if (!s->selected_file_content || s->selected_file_path[0] == '\0') {
                        cui_column(ui, .width = CUI_FILL, .height = CUI_FILL,
                                   .align = CUI_ALIGN_CENTER, .justify = CUI_JUSTIFY_CENTER) {
                            cui_empty_state(ui, "未选中任何文件",
                                            .description = "点击左侧目录树中的文件查看具体内容",
                                            .icon = CUI_ICON_FILE_TEXT);
                        }
                    } else if (s->selected_file_is_binary) {
                        cui_column(ui, .width = CUI_FILL, .height = CUI_FILL,
                                   .align = CUI_ALIGN_CENTER, .justify = CUI_JUSTIFY_CENTER) {
                            cui_empty_state(ui, "二进制或不可读文件",
                                            .description = "该文件包含二进制数据，暂不提供文本预览",
                                            .icon = CUI_ICON_BOX);
                        }
                    } else {
                        bool is_md = (strstr(s->selected_file_name, ".md") != NULL ||
                                      strstr(s->selected_file_name, ".markdown") != NULL);

                        if (is_md && s->doc_render_mode == 0) {
                            /* Markdown 渲染展示 */
                            cui_markdown(ui, "skill_md_viewer", s->selected_file_content, .size = 13.5f);
                        } else {
                            /* 脚本与文本：采用 cui_code_block 语法高亮与行号展示 */
                            const char *lang = detect_syntax_lang(s->selected_file_name);
                            cui_code_block(ui, "skill_code_viewer", s->selected_file_content,
                                           .lang = lang, .title = s->selected_file_name,
                                           .line_numbers = true, .size = 12.5f);
                        }
                    }
                }
            }
        }
    } else {
        /* Full Skills List Table */
        static const cui_table_column skill_cols[] = {
            { "#",              CUI_PX(48),  CUI_TEXT_CENTER, true  },
            { "Skill 名称",     CUI_PX(220), CUI_TEXT_LEFT,   true  },
            { "SKILL.md",       CUI_PX(96),  CUI_TEXT_CENTER, true  },
            { "包含文件数",     CUI_PX(96),  CUI_TEXT_RIGHT,  true  },
            { "功能说明 / 摘要", CUI_FILL,    CUI_TEXT_LEFT,   false },
            { "操作",           CUI_PX(150), CUI_TEXT_CENTER, false },
        };

        cui_card(ui, .width = CUI_FILL, .height = CUI_FILL, .pad = 0, .gap = 0) {
            cui_table(ui, "skills_all_table", skill_cols, 6,
                      .sort = &s->skill_table_sort, .striped = true, .compact = true,
                      .height = CUI_FILL) {
                for (int i = 0; i < s->skill_count; i++) {
                    skill_item_t *sk = &s->skill_items[i];
                    cui_push_id_int(ui, i);
                    cui_table_row(ui, false);

                    /* Col 0: Index */
                    cui_table_cell_textf(ui, "%d", i + 1);

                    /* Col 1: Name */
                    cui_table_cell(ui);
                    cui_row(ui, .gap = 6, .align = CUI_ALIGN_CENTER) {
                        cui_icon(ui, CUI_ICON_BOX, 14, CUI_COLOR_ACCENT);
                        cui_text(ui, sk->name, .font = CUI_FONT_MEDIUM);
                    }

                    /* Col 2: Has SKILL.md */
                    cui_table_cell(ui);
                    if (sk->has_skill_md) {
                        cui_badge(ui, "有", .tone = CUI_TONE_SUCCESS, .size = CUI_SIZE_SM);
                    } else {
                        cui_badge(ui, "无", .tone = CUI_TONE_NEUTRAL, .size = CUI_SIZE_SM);
                    }

                    /* Col 3: File count */
                    cui_table_cell_textf(ui, "%d 个", sk->file_count);

                    /* Col 4: Description */
                    cui_table_cell_text(ui, sk->description[0] ? sk->description : "-");

                    /* Col 5: Action Button -> switch to tree & content view */
                    cui_table_cell(ui);
                    if (cui_button(ui, "查看结构与内容", .variant = CUI_BUTTON_OUTLINE,
                                   .size = CUI_SIZE_SM, .icon = CUI_ICON_FOLDER)) {
                        select_active_skill(s, sk->name, sk->path);
                        s->skill_view_mode = 0;
                    }

                    cui_pop_id(ui);
                }
            }
        }
    }
}

/* -------------------- Main Frame Dispatcher -------------------- */

static void frame(cui_ctx *ui, void *user) {
    app_state_t *s = (app_state_t *)user;

    cui_column(ui, .width = CUI_FILL, .height = CUI_FILL, .pad = 20, .gap = 14,
               .bg = CUI_COLOR_BACKGROUND) {
        if (s->current_page == PAGE_TOOL_LIST) {
            render_tools_list_page(ui, s);
        } else if (s->current_page == PAGE_HUB_MATRIX) {
            render_hub_matrix_page(ui, s);
        } else if (s->current_page == PAGE_TOOL_SKILLS) {
            render_tool_skills_page(ui, s);
        }
    }
}

int main(void) {
    static app_state_t s = {0};
    strncpy(s.db_path, "data/ai_space.db", sizeof(s.db_path) - 1);

    if (db_init(s.db_path, &s.db) != 0) {
        fprintf(stderr, "Failed to initialize database at %s\n", s.db_path);
        return 1;
    }

    /* Initial synchronization on application launch */
    do_scan_sync(&s);

    const char *start_page = getenv("CUI_START_PAGE");
    if (start_page && strcmp(start_page, "hub") == 0) {
        reload_hub_matrix(&s);
        s.current_page = PAGE_HUB_MATRIX;
        s.hub_subview = 0;

        const char *start_diff = getenv("CUI_START_DIFF");
        if (start_diff && start_diff[0] != '\0') {
            open_hub_diff_view(&s, start_diff);
        }
    }

    const char *start_tool = getenv("CUI_START_TOOL");
    if (start_tool && start_tool[0] != '\0') {
        for (int i = 0; i < s.record_count; i++) {
            if (strcmp(s.records[i].name, start_tool) == 0) {
                open_tool_skills_page(&s, s.records[i].name, s.records[i].path);
                break;
            }
        }
        const char *start_skill = getenv("CUI_START_SKILL");
        if (start_skill && start_skill[0] != '\0') {
            for (int j = 0; j < s.skill_count; j++) {
                if (strcmp(s.skill_items[j].name, start_skill) == 0) {
                    select_active_skill(&s, s.skill_items[j].name, s.skill_items[j].path);
                    break;
                }
            }
        }
        const char *start_file = getenv("CUI_START_FILE");
        if (start_file && start_file[0] != '\0') {
            char full_f[1200];
            snprintf(full_f, sizeof(full_f), "%s/%s", s.active_skill_path, start_file);
            load_skill_file(&s, full_f, NULL);
        }
    }

    cui_app *app = cui_app_create(&(cui_app_desc){
        .title = "AI-Space 本地工具与技能管理器",
        .width = 1260,
        .height = 780,
        .min_width = 960,
        .min_height = 620,
        .accent = CUI_ACCENT_BLUE,
    });

    if (!app) {
        db_close(s.db);
        if (s.records) free(s.records);
        return 1;
    }

    while (cui_app_frame(app)) {
        cui_ctx *ui = cui_app_ui(app);
        static cui_font cjk;
        static bool font_done = false;

        /* Load CJK fallback font for Chinese characters */
        if (!font_done) {
            cjk = cui_font_load_file(ui, "fonts/NotoSansSC-Regular.otf");
            if (cjk > 0) {
                cui_font_add_fallback(ui, CUI_FONT_BODY, cjk);
                cui_font_add_fallback(ui, CUI_FONT_MEDIUM, cjk);
                cui_font_add_fallback(ui, CUI_FONT_DISPLAY, cjk);
                cui_font_add_fallback(ui, CUI_FONT_MONO, cjk);
            }
            font_done = true;
        }

        frame(ui, &s);
    }

    cui_app_destroy(app);

    if (s.records) {
        free(s.records);
        s.records = NULL;
    }
    if (s.skill_items) {
        free(s.skill_items);
        s.skill_items = NULL;
    }
    if (s.skill_file_tree) {
        free_skill_file_tree(s.skill_file_tree);
        free(s.skill_file_tree);
        s.skill_file_tree = NULL;
    }
    if (s.selected_file_content) {
        free(s.selected_file_content);
        s.selected_file_content = NULL;
    }
    hub_free_matrix(&s.hub_matrix);
    clear_diff_texts(&s);
    db_close(s.db);

    return 0;
}
