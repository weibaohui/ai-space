#ifndef HUB_PATHS_H
#define HUB_PATHS_H

#include <stddef.h>

/* Initialize central hub directories (~/.skillboard/skills, ~/.skillboard/backups) */
int hub_init_dirs(char *out_hub_dir, size_t hub_cap, char *out_backup_dir, size_t backup_cap);

/* Custom directory configuration (can be loaded from settings) */
void hub_set_custom_dirs(const char *hub_dir, const char *backup_dir);
void hub_get_custom_dirs(char *out_hub_dir, size_t hub_cap, char *out_backup_dir, size_t backup_cap);

#endif
