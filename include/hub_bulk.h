#ifndef HUB_BULK_H
#define HUB_BULK_H

#include "hub_manager.h"

int hub_broadcast_skill(const hub_matrix_t *matrix, const char *skill_name, int *out_synced_count);
int hub_distribute_skill_to_all_tools(const hub_matrix_t *matrix, const char *skill_name, int *out_synced_count);
int hub_distribute_all_to_all_tools(const hub_matrix_t *matrix, int *out_distributed_count,
                                    hub_progress_cb cb, void *user_data);
int hub_uninstall_skill_from_all_tools(const hub_matrix_t *matrix, const char *skill_name);
int hub_adopt_all_to_master(const hub_matrix_t *matrix, int *out_adopted_count);
int hub_adopt_all_to_master_ex(const hub_matrix_t *matrix, int *out_adopted_count,
                              hub_progress_cb cb, void *user_data);

#endif
