#include "hub_bulk.h"
#include "hub_files.h"

#include <stdio.h>
#include <string.h>

int hub_broadcast_skill(const hub_matrix_t *matrix, const char *skill_name, int *out_synced_count) {
    if (!matrix || !skill_name) return -1;

    int synced = 0;
    int result = 0;
    for (int i = 0; i < matrix->skill_count; i++) {
        if (strcmp(matrix->skills[i].skill_name, skill_name) == 0) {
            hub_skill_entry_t *entry = &matrix->skills[i];
            if (!entry->in_hub) return -1;

            for (int t = 0; t < entry->tool_count; t++) {
                if (entry->tool_copies[t].exists) {
                    if (hub_sync_master_to_tool(entry->tool_copies[t].tool_skills_path,
                                                skill_name, entry->tool_copies[t].tool_name) == 0) {
                        synced++;
                    } else {
                        result = -1;
                    }
                }
            }
            break;
        }
    }

    if (out_synced_count) *out_synced_count = synced;
    return result;
}

int hub_distribute_skill_to_all_tools(const hub_matrix_t *matrix, const char *skill_name, int *out_synced_count) {
    if (!matrix || !skill_name) return -1;

    int synced = 0;
    int result = 0;
    for (int t = 0; t < matrix->tool_count; t++) {
        const char *t_name = matrix->tools[t];
        const char *t_path = matrix->tool_paths[t];
        if (!t_path || t_path[0] == '\0') continue;

        if (hub_sync_master_to_tool(t_path, skill_name, t_name) == 0) {
            synced++;
        } else {
            result = -1;
        }
    }

    if (out_synced_count) *out_synced_count = synced;
    return result;
}

int hub_distribute_all_to_all_tools(const hub_matrix_t *matrix, int *out_distributed_count,
                                    hub_progress_cb cb, void *user_data) {
    if (!matrix) return -1;

    int total_actions = 0;
    int result = 0;
    int master_count = 0;
    for (int i = 0; i < matrix->skill_count; i++) {
        if (matrix->skills[i].in_hub) master_count++;
    }

    int current = 0;
    for (int i = 0; i < matrix->skill_count; i++) {
        hub_skill_entry_t *entry = &matrix->skills[i];
        if (!entry->in_hub) continue;

        current++;
        if (cb) {
            float p = (float)current / (float)(master_count ? master_count : 1);
            char p_msg[128];
            snprintf(p_msg, sizeof(p_msg), "正在全量分发主仓技能 (%d/%d): %s", current, master_count, entry->skill_name);
            cb(p, p_msg, user_data);
        }

        int synced = 0;
        if (hub_distribute_skill_to_all_tools(matrix, entry->skill_name, &synced) != 0)
            result = -1;
        total_actions += synced;
    }

    if (out_distributed_count) *out_distributed_count = total_actions;
    return result;
}

int hub_uninstall_skill_from_all_tools(const hub_matrix_t *matrix, const char *skill_name) {
    if (!matrix || !skill_name || skill_name[0] == '\0') return -1;
    int uninstalled = 0;
    bool failed = false;
    for (int i = 0; i < matrix->skill_count; i++) {
        const hub_skill_entry_t *entry = &matrix->skills[i];
        if (strcmp(entry->skill_name, skill_name) == 0) {
            for (int t = 0; t < entry->tool_count; t++) {
                if (entry->tool_copies[t].exists) {
                    if (hub_uninstall_skill_from_tool(entry->tool_copies[t].tool_skills_path,
                                                      skill_name,
                                                      entry->tool_copies[t].tool_name) == 0) {
                        uninstalled++;
                    } else {
                        failed = true;
                    }
                }
            }
            break;
        }
    }
    return failed ? -1 : uninstalled;
}

int hub_adopt_all_to_master_ex(const hub_matrix_t *matrix, int *out_adopted_count,
                              hub_progress_cb cb, void *user_data) {
    if (!matrix) return -1;

    int adopted = 0;
    int result = 0;
    int total = matrix->skill_count;
    for (int i = 0; i < total; i++) {
        hub_skill_entry_t *entry = &matrix->skills[i];
        if (cb && (i % 3 == 0 || i == total - 1)) {
            float p = (float)(i + 1) / (float)(total ? total : 1);
            char p_msg[128];
            snprintf(p_msg, sizeof(p_msg), "正在收录主库 (%d/%d): %s", i + 1, total, entry->skill_name);
            cb(p, p_msg, user_data);
        }
        if (!entry->in_hub && entry->installed_count > 0) {
            /* Find first existing tool copy to promote */
            for (int t = 0; t < entry->tool_count; t++) {
                if (entry->tool_copies[t].exists) {
                    if (hub_promote_to_master(entry->tool_copies[t].tool_skills_path,
                                              entry->skill_name) == 0) {
                        adopted++;
                    } else {
                        result = -1;
                    }
                    break;
                }
            }
        }
    }

    if (cb) cb(1.0f, "全量收录主库完成", user_data);
    if (out_adopted_count) *out_adopted_count = adopted;
    return result;
}

int hub_adopt_all_to_master(const hub_matrix_t *matrix, int *out_adopted_count) {
    return hub_adopt_all_to_master_ex(matrix, out_adopted_count, NULL, NULL);
}
