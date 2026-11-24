#include "app_storage.h"
#include "app_state.h"

#include <stdio.h>
#include <string.h>

void app_storage_begin_frame(app_state_t *s) {
    s->storage_cache.frame++;
}

void app_storage_invalidate(app_state_t *s) {
    for (int i = 0; i < STORAGE_PATH_COUNT; i++) s->storage_cache.dirs[i].valid = false;
    s->storage_stats.loaded = false;
}

const storage_directory_info_t *app_storage_directory(app_state_t *s, app_storage_path_t slot,
                                                      const char *path) {
    app_storage_dir_cache_t *cached = &s->storage_cache.dirs[slot];
    unsigned long frame = s->storage_cache.frame;
    time_t now = time(NULL);
    /* 重新进入页面和路径变化会立即刷新。页面常驻时也定期探测外部修改。 */
    if (!cached->valid || frame - cached->frame > 1 || strcmp(path, cached->path) != 0 ||
        now < cached->refreshed_at || now - cached->refreshed_at >= 3) {
        storage_probe_directory(path, &cached->info);
        snprintf(cached->path, sizeof(cached->path), "%s", path);
        cached->refreshed_at = now;
        cached->valid = true;
    }
    cached->frame = frame;
    return &cached->info;
}
