#ifndef VIEW_SKILL_TREE_H
#define VIEW_SKILL_TREE_H

#include <cui/cui.h>
#include <stdbool.h>
#include "skill_scanner.h"
#include "app_state.h"

/* Configuration options for the reusable skill file tree component */
typedef struct {
    const char *title;              /* e.g. "目录结构树" or "技能文件结构树" */
    const char *selected_path;      /* current selected relative path or abs path */
    bool match_by_rel_path;         /* true: match by node->rel_path; false: match by node->abs_path */
    bool show_diff_badges;          /* true: render +X -Y / +新增 / -缺失 / 一致 badges */
    bool show_summary_badges;       /* true: render summary badges under header */
    int summary_total_files;
    int summary_modified;
    int summary_added;
    int summary_deleted;
    int summary_total_added_lines;
    int summary_total_removed_lines;
    cui_size width;                 /* fixed or fill, defaults to 280 points */
} skill_tree_view_opts_t;

/* Callback signature when a file node in the tree is selected */
typedef void (*skill_tree_on_select_fn)(file_node_t *node, void *user_data);

/*
 * Builds a hierarchical file_node_t tree from a flat hub_skill_diff_manifest_t.
 * Bubble-up diff lines and statuses to directory nodes.
 * Returns the root node (must be freed with free_skill_file_tree + free).
 */
file_node_t *build_tree_from_diff_manifest(const hub_skill_diff_manifest_t *manifest,
                                           const char *master_base_path,
                                           const char *tool_base_path);

/*
 * Renders the reusable skill file tree component card (header, summary badges, scrollable tree).
 */
void render_skill_file_tree_component(cui_ctx *ui,
                                      file_node_t *root,
                                      const skill_tree_view_opts_t *opts,
                                      skill_tree_on_select_fn on_select,
                                      void *user_data);

#endif /* VIEW_SKILL_TREE_H */
