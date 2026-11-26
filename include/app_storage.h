#ifndef APP_STORAGE_H
#define APP_STORAGE_H

#include "storage_manager.h"

struct app_state_t;

typedef enum {
    STORAGE_PATH_HUB,
    STORAGE_PATH_BACKUP,
    STORAGE_PATH_DASHBOARD_BACKUP,
    STORAGE_PATH_TOOL_SKILLS,
    STORAGE_PATH_COUNT,
} app_storage_path_t;

typedef struct {
    bool valid;
    unsigned long frame;
    time_t refreshed_at;
    char path[512];
    storage_directory_info_t info;
} app_storage_dir_cache_t;

typedef struct {
    unsigned long frame;
    app_storage_dir_cache_t dirs[STORAGE_PATH_COUNT];
} app_storage_cache_t;

void app_storage_begin_frame(struct app_state_t *s);
void app_storage_invalidate(struct app_state_t *s);
const storage_directory_info_t *app_storage_directory(struct app_state_t *s, app_storage_path_t slot,
                                                      const char *path);

#endif
