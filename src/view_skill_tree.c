#include "view_skill_tree.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------- Tree Builder from Diff Manifest -------------------- */

static file_node_t *get_or_create_child_dir(file_node_t *parent, const char *dname, const char *rel_path, const char *base_path) {
    for (int i = 0; i < parent->child_count; i++) {
        if (parent->children[i].is_dir && strcmp(parent->children[i].name, dname) == 0) {
            return &parent->children[i];
        }
    }

    file_node_t *new_children = (file_node_t *)realloc(parent->children, sizeof(file_node_t) * (parent->child_count + 1));
    if (!new_children) return NULL;
    parent->children = new_children;

    file_node_t *child = &parent->children[parent->child_count++];
    memset(child, 0, sizeof(file_node_t));
    strncpy(child->name, dname, sizeof(child->name) - 1);
    strncpy(child->rel_path, rel_path, sizeof(child->rel_path) - 1);
    if (base_path && base_path[0] != '\0') {
        snprintf(child->abs_path, sizeof(child->abs_path), "%s/%s", base_path, rel_path);
    }
    child->is_dir = true;
    child->is_open = true; /* Folders open by default */
    child->has_diff_info = true;
    child->diff_status = 0;
    return child;
}

static void bubble_up_diff_info(file_node_t *node) {
    if (!node || !node->is_dir) return;

    for (int i = 0; i < node->child_count; i++) {
        if (node->children[i].is_dir) {
            bubble_up_diff_info(&node->children[i]);
        }
        node->added_lines += node->children[i].added_lines;
        node->removed_lines += node->children[i].removed_lines;
        if (node->children[i].diff_status != 0 && node->diff_status == 0) {
            node->diff_status = node->children[i].diff_status;
        }
    }
}

static int compare_tree_nodes(const void *a, const void *b) {
    const file_node_t *na = (const file_node_t *)a;
    const file_node_t *nb = (const file_node_t *)b;

    /* SKILL.md always at the top */
    if (strcmp(na->name, "SKILL.md") == 0) return -1;
    if (strcmp(nb->name, "SKILL.md") == 0) return 1;

    /* Folders before files */
    if (na->is_dir != nb->is_dir) {
        return na->is_dir ? -1 : 1;
    }

    return strcmp(na->name, nb->name);
}

static void sort_tree_recursive(file_node_t *node) {
    if (!node || !node->is_dir || node->child_count <= 1) return;
    qsort(node->children, node->child_count, sizeof(file_node_t), compare_tree_nodes);
    for (int i = 0; i < node->child_count; i++) {
        if (node->children[i].is_dir) {
            sort_tree_recursive(&node->children[i]);
        }
    }
}

file_node_t *build_tree_from_diff_manifest(const hub_skill_diff_manifest_t *manifest,
                                           const char *master_base_path,
                                           const char *tool_base_path) {
    if (!manifest) return NULL;

    file_node_t *root = (file_node_t *)calloc(1, sizeof(file_node_t));
    if (!root) return NULL;

    strncpy(root->name, manifest->skill_name, sizeof(root->name) - 1);
    root->is_dir = true;
    root->is_open = true;
    root->has_diff_info = true;

    const char *base = (tool_base_path && tool_base_path[0] != '\0') ? tool_base_path : master_base_path;
    if (base && base[0] != '\0') {
        strncpy(root->abs_path, base, sizeof(root->abs_path) - 1);
    }

    for (int i = 0; i < manifest->count; i++) {
        const hub_diff_file_item_t *item = &manifest->items[i];

        char temp[512];
        strncpy(temp, item->rel_path, sizeof(temp) - 1);
        temp[sizeof(temp) - 1] = '\0';

        char *curr = temp;
        char *slash;
        file_node_t *cur_dir = root;
        char cur_rel[512] = "";

        while ((slash = strchr(curr, '/')) != NULL) {
            *slash = '\0';
            if (cur_rel[0] != '\0') {
                strncat(cur_rel, "/", sizeof(cur_rel) - strlen(cur_rel) - 1);
            }
            strncat(cur_rel, curr, sizeof(cur_rel) - strlen(cur_rel) - 1);

            cur_dir = get_or_create_child_dir(cur_dir, curr, cur_rel, base);
            if (!cur_dir) break;

            curr = slash + 1;
        }

        if (!cur_dir) continue;

        /* Append file leaf to cur_dir */
        file_node_t *new_children = (file_node_t *)realloc(cur_dir->children, sizeof(file_node_t) * (cur_dir->child_count + 1));
        if (!new_children) continue;
        cur_dir->children = new_children;

        file_node_t *leaf = &cur_dir->children[cur_dir->child_count++];
        memset(leaf, 0, sizeof(file_node_t));
        strncpy(leaf->name, curr, sizeof(leaf->name) - 1);
        strncpy(leaf->rel_path, item->rel_path, sizeof(leaf->rel_path) - 1);
        if (base && base[0] != '\0') {
            snprintf(leaf->abs_path, sizeof(leaf->abs_path), "%s/%s", base, leaf->rel_path);
        }
        leaf->is_dir = false;
        leaf->is_open = false;
        leaf->size = item->tool_size ? item->tool_size : item->master_size;
        leaf->has_diff_info = true;
        leaf->diff_status = (int)item->status;
        leaf->added_lines = item->added_lines;
        leaf->removed_lines = item->removed_lines;
    }

    bubble_up_diff_info(root);
    sort_tree_recursive(root);

    return root;
}

/* -------------------- Unified Tree Component Renderer -------------------- */

static const char *get_file_node_icon(const char *name) {
    if (!name) return CUI_ICON_FILE;
    if (strstr(name, ".md") || strstr(name, ".markdown")) {
        return CUI_ICON_BOOKMARK;
    }
    if (strstr(name, ".py") || strstr(name, ".sh") ||
        strstr(name, ".bash") || strstr(name, ".zsh") ||
        strstr(name, ".c") || strstr(name, ".h") ||
        strstr(name, ".js") || strstr(name, ".ts") ||
        strstr(name, ".json") || strstr(name, ".html")) {
        return CUI_ICON_CODE;
    }
    return CUI_ICON_FILE_TEXT;
}

static void render_tree_node(cui_ctx *ui, file_node_t *node, int depth,
                             const skill_tree_view_opts_t *opts,
                             skill_tree_on_select_fn on_select,
                             void *user_data) {
    if (!node) return;

    if (node->is_dir) {
        cui_push_id(ui, node->rel_path[0] ? node->rel_path : node->name);

        bool row_clicked = cui_card_begin(ui, &(cui_card_opts){
            .width = CUI_FILL,
            .pad = 5,
            .gap = 0,
            .hoverable = true,
        });

        cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
            if (depth > 0) {
                cui_gap(ui, depth * 14);
            }

            cui_icon(ui, node->is_open ? CUI_ICON_CHEVRON_DOWN : CUI_ICON_CHEVRON_RIGHT, 12, CUI_COLOR_TEXT_MUTED);
            cui_gap(ui, 4);

            cui_icon(ui, node->is_open ? CUI_ICON_FOLDER_OPEN : CUI_ICON_FOLDER, 14, CUI_COLOR_ACCENT);
            cui_gap(ui, 6);

            cui_text(ui, node->name, .font = CUI_FONT_MEDIUM, .size = 11);

            cui_spacer(ui);

            /* Folder Diff Indicator (aggregate changes inside) */
            if (opts->show_diff_badges && node->has_diff_info) {
                if (node->added_lines > 0 || node->removed_lines > 0) {
                    cui_row(ui, .gap = 3, .align = CUI_ALIGN_CENTER) {
                        if (node->added_lines > 0) {
                            char a_buf[24];
                            snprintf(a_buf, sizeof(a_buf), "+%d", node->added_lines);
                            cui_badge(ui, a_buf, .tone = CUI_TONE_SUCCESS, .size = CUI_SIZE_SM);
                        }
                        if (node->removed_lines > 0) {
                            char r_buf[24];
                            snprintf(r_buf, sizeof(r_buf), "-%d", node->removed_lines);
                            cui_badge(ui, r_buf, .tone = CUI_TONE_DANGER, .size = CUI_SIZE_SM);
                        }
                    }
                }
            }
        }
        cui_card_end(ui);

        if (row_clicked) {
            node->is_open = !node->is_open;
        }

        if (node->is_open) {
            for (int i = 0; i < node->child_count; i++) {
                render_tree_node(ui, &node->children[i], depth + 1, opts, on_select, user_data);
            }
        }

        cui_pop_id(ui);

    } else {
        /* File Leaf Node */
        cui_push_id(ui, node->rel_path[0] ? node->rel_path : node->name);

        bool is_selected = false;
        if (opts->selected_path && opts->selected_path[0] != '\0') {
            if (opts->match_by_rel_path) {
                is_selected = (strcmp(opts->selected_path, node->rel_path) == 0);
            } else {
                is_selected = (strcmp(opts->selected_path, node->abs_path) == 0);
            }
        }

        bool row_clicked = cui_card_begin(ui, &(cui_card_opts){
            .width = CUI_FILL,
            .pad = 5,
            .gap = 0,
            .hoverable = true,
            .selected = is_selected,
        });

        cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
            if (depth > 0) {
                cui_gap(ui, depth * 14);
            }
            cui_gap(ui, 16); /* Compensate for folder chevron gap */

            const char *icon = get_file_node_icon(node->name);
            cui_icon(ui, icon, 13, is_selected ? CUI_COLOR_ACCENT : CUI_COLOR_FOREGROUND);
            cui_gap(ui, 6);

            cui_text(ui, node->name, .size = 11,
                     .font = is_selected ? CUI_FONT_MEDIUM : CUI_FONT_BODY,
                     .color = is_selected ? CUI_COLOR_ACCENT : CUI_COLOR_FOREGROUND);

            cui_spacer(ui);

            /* Badges with + and - */
            if (opts->show_diff_badges && node->has_diff_info) {
                if (node->diff_status == 1 /* MODIFIED */) {
                    cui_row(ui, .gap = 3, .align = CUI_ALIGN_CENTER) {
                        if (node->added_lines > 0) {
                            char a_buf[24];
                            snprintf(a_buf, sizeof(a_buf), "+%d", node->added_lines);
                            cui_badge(ui, a_buf, .tone = CUI_TONE_SUCCESS, .size = CUI_SIZE_SM);
                        }
                        if (node->removed_lines > 0) {
                            char r_buf[24];
                            snprintf(r_buf, sizeof(r_buf), "-%d", node->removed_lines);
                            cui_badge(ui, r_buf, .tone = CUI_TONE_DANGER, .size = CUI_SIZE_SM);
                        }
                        if (node->added_lines == 0 && node->removed_lines == 0) {
                            cui_badge(ui, "改动", .tone = CUI_TONE_WARNING, .size = CUI_SIZE_SM);
                        }
                    }
                } else if (node->diff_status == 2 /* ADDED */) {
                    char a_buf[24];
                    if (node->added_lines > 0) {
                        snprintf(a_buf, sizeof(a_buf), "+%d", node->added_lines);
                    } else {
                        snprintf(a_buf, sizeof(a_buf), "+新增");
                    }
                    cui_badge(ui, a_buf, .tone = CUI_TONE_SUCCESS, .size = CUI_SIZE_SM);
                } else if (node->diff_status == 3 /* DELETED */) {
                    char r_buf[24];
                    if (node->removed_lines > 0) {
                        snprintf(r_buf, sizeof(r_buf), "-%d", node->removed_lines);
                    } else {
                        snprintf(r_buf, sizeof(r_buf), "-缺失");
                    }
                    cui_badge(ui, r_buf, .tone = CUI_TONE_DANGER, .size = CUI_SIZE_SM);
                } else {
                    cui_badge(ui, "一致", .tone = CUI_TONE_NEUTRAL, .size = CUI_SIZE_SM);
                }
            }
        }
        cui_card_end(ui);

        if (row_clicked) {
            if (on_select) {
                on_select(node, user_data);
            }
        }

        cui_pop_id(ui);
    }
}

void render_skill_file_tree_component(cui_ctx *ui,
                                      file_node_t *root,
                                      const skill_tree_view_opts_t *opts,
                                      skill_tree_on_select_fn on_select,
                                      void *user_data) {
    if (!opts) return;
    float w = opts->width > 0 ? opts->width : 280.0f;

    cui_card(ui, .width = CUI_PX(w), .height = CUI_FILL, .pad = 10, .gap = 8) {
        /* 1. Header */
        cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
            cui_row(ui, .gap = 6, .align = CUI_ALIGN_CENTER) {
                cui_icon(ui, CUI_ICON_FOLDER_OPEN, 15, CUI_COLOR_ACCENT);
                cui_label(ui, opts->title ? opts->title : "目录结构树");
            }
            cui_spacer(ui);
            if (opts->badge_label && opts->badge_label[0] != '\0') {
                cui_badge(ui, opts->badge_label, .tone = opts->badge_tone, .size = CUI_SIZE_SM);
            }
        }

        /* 2. Optional Summary Badges */
        if (opts->show_summary_badges) {
            cui_row(ui, .gap = 4, .align = CUI_ALIGN_CENTER) {
                if (opts->summary_modified > 0) {
                    char m_buf[32];
                    snprintf(m_buf, sizeof(m_buf), "%d 改动", opts->summary_modified);
                    cui_badge(ui, m_buf, .tone = CUI_TONE_WARNING, .size = CUI_SIZE_SM);
                }
                if (opts->summary_added > 0) {
                    char a_buf[32];
                    snprintf(a_buf, sizeof(a_buf), "%d 新增", opts->summary_added);
                    cui_badge(ui, a_buf, .tone = CUI_TONE_SUCCESS, .size = CUI_SIZE_SM);
                }
                if (opts->summary_deleted > 0) {
                    char d_buf[32];
                    snprintf(d_buf, sizeof(d_buf), "%d 缺失", opts->summary_deleted);
                    cui_badge(ui, d_buf, .tone = CUI_TONE_DANGER, .size = CUI_SIZE_SM);
                }
                if (opts->summary_total_added_lines > 0 || opts->summary_total_removed_lines > 0) {
                    char l_buf[48];
                    snprintf(l_buf, sizeof(l_buf), "+%d / -%d 行",
                             opts->summary_total_added_lines, opts->summary_total_removed_lines);
                    cui_badge(ui, l_buf, .tone = CUI_TONE_ACCENT, .size = CUI_SIZE_SM);
                }
                if (opts->summary_modified == 0 &&
                    opts->summary_added == 0 &&
                    opts->summary_deleted == 0 &&
                    opts->summary_total_files > 0) {
                    cui_badge(ui, "全文件一致", .tone = CUI_TONE_SUCCESS, .size = CUI_SIZE_SM);
                }
            }
        }

        cui_separator(ui);

        /* 3. Scrollable Tree Body */
        cui_scroll(ui, .id = "skill_tree_scroll_area", .width = CUI_FILL, .height = CUI_FILL) {
            if (root && root->child_count > 0) {
                cui_column(ui, .width = CUI_FILL, .gap = 2) {
                    for (int i = 0; i < root->child_count; i++) {
                        render_tree_node(ui, &root->children[i], 0, opts, on_select, user_data);
                    }
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
}
