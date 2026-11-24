#include "skill_file_tree.h"
#include "file_preview.h"
#include "app_diff.h"
#include "app_hub_tasks.h"
#include "view_skill_tree.h"
#include "skill_scanner.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "portability.h"

void clear_diff_texts(app_state_t *s) {
    if (!s) return;
    if (s->diff_old_text) {
        free(s->diff_old_text);
        s->diff_old_text = NULL;
    }
    if (s->diff_new_text) {
        free(s->diff_new_text);
        s->diff_new_text = NULL;
    }
}


void build_skill_diff_manifest(app_state_t *s, const char *skill_name, int tool_idx) {
    if (!s || !skill_name) return;

    free_skill_diff_manifest(&s->diff_manifest);
    if (s->hub_diff_file_tree) {
        free_skill_file_tree(s->hub_diff_file_tree);
        free(s->hub_diff_file_tree);
        s->hub_diff_file_tree = NULL;
    }
    s->diff_manifest_loaded = false;
    strncpy(s->diff_manifest.skill_name, skill_name, sizeof(s->diff_manifest.skill_name) - 1);

    const hub_skill_entry_t *entry = hub_find_skill(&s->hub_matrix, skill_name);
    if (!entry) return;

    const char *tool_skills_path = NULL;
    if (tool_idx >= 0 && tool_idx < entry->tool_count) {
        strncpy(s->diff_manifest.tool_name, entry->tool_copies[tool_idx].tool_name, sizeof(s->diff_manifest.tool_name) - 1);
        if (entry->tool_copies[tool_idx].exists) {
            tool_skills_path = entry->tool_copies[tool_idx].skill_path;
        }
    }

    if (skill_diff_compute(entry->in_hub ? entry->hub_path : NULL, tool_skills_path,
                           &s->diff_manifest) != 0) return;

    s->diff_manifest_loaded = true;

    /* 原选择消失时优先定位第一项有差异的文件。 */
    bool current_found = false;
    for (int i = 0; i < s->diff_manifest.count; i++) {
        if (strcmp(s->diff_manifest.items[i].rel_path, s->diff_file_rel) == 0) {
            current_found = true;
            break;
        }
    }
    if (!current_found) {
        if (s->diff_manifest.count > 0) {
            int pick = 0;
            for (int i = 0; i < s->diff_manifest.count; i++) {
                if (s->diff_manifest.items[i].status != DIFF_FILE_IN_SYNC) {
                    pick = i;
                    break;
                }
            }
            strncpy(s->diff_file_rel, s->diff_manifest.items[pick].rel_path, sizeof(s->diff_file_rel) - 1);
        } else {
            strncpy(s->diff_file_rel, "SKILL.md", sizeof(s->diff_file_rel) - 1);
        }
    }

    /* 用同一份差异清单构建文件树，保持徽标与预览一致。 */
    s->hub_diff_file_tree = build_tree_from_diff_manifest(&s->diff_manifest, entry->hub_path, tool_skills_path);
}

void load_hub_diff(app_state_t *s, const char *skill_name, int tool_idx, const char *rel_file) {
    if (!s || !skill_name) return;

    clear_diff_texts(s);
    if (skill_name != s->selected_hub_skill)
        snprintf(s->selected_hub_skill, sizeof(s->selected_hub_skill), "%s", skill_name);
    s->selected_hub_tool_idx = tool_idx;

    if (!rel_file || !rel_file[0])
        snprintf(s->diff_file_rel, sizeof(s->diff_file_rel), "SKILL.md");
    else if (rel_file != s->diff_file_rel)
        snprintf(s->diff_file_rel, sizeof(s->diff_file_rel), "%s", rel_file);
    s->diff_current_change = 0;

    const hub_skill_entry_t *entry = hub_find_skill(&s->hub_matrix, skill_name);
    if (!entry) return;

    /* 读取主仓预览，限制读取量以免大文件拖慢页面。 */
    if (entry->in_hub) {
        char m_path[1200];
        snprintf(m_path, sizeof(m_path), "%s/%s", entry->hub_path, s->diff_file_rel);
        int m_lines = 0;
        bool m_bin = false;
        s->diff_old_text = read_file_preview(m_path, 1024 * 1024, NULL, NULL, &m_lines, &m_bin);
    }
    if (!s->diff_old_text) {
        s->diff_old_text = strdup("");
    }

    /* 工具副本使用同样的预览上限。 */
    if (tool_idx >= 0 && tool_idx < entry->tool_count && entry->tool_copies[tool_idx].exists) {
        char t_path[1200];
        snprintf(t_path, sizeof(t_path), "%s/%s", entry->tool_copies[tool_idx].skill_path, s->diff_file_rel);
        int t_lines = 0;
        bool t_bin = false;
        s->diff_new_text = read_file_preview(t_path, 1024 * 1024, NULL, NULL, &t_lines, &t_bin);
    }
    if (!s->diff_new_text) {
        s->diff_new_text = strdup("");
    }
}

void open_hub_diff_view(app_state_t *s, const char *skill_name) {
    if (!s) return;
    s->prev_page = s->current_page;
    if (!s->hub_matrix_loaded && !s->hub_scan.is_scanning) {
        start_hub_scan_thread(s);
    }

    const hub_skill_entry_t *entry = hub_find_skill(&s->hub_matrix, skill_name);
    if (!entry) return;

    /* 打开差异页时优先定位有变更的工具副本。 */
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

    s->hub_subview = HUB_SUBVIEW_DIFF;
    s->diff_detail_tab = 0;
    build_skill_diff_manifest(s, skill_name, chosen_idx);
    load_hub_diff(s, skill_name, chosen_idx, s->diff_file_rel);
}
