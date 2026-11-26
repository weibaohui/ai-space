#ifndef STORAGE_MANAGER_H
#define STORAGE_MANAGER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

typedef struct {
    char dir_name[128];
    char full_path[1024];
    char time_str[32];
    char tool_name[64];
    char skill_name[64];
    int64_t size_bytes;
    int file_count;
    time_t mtime;
} storage_snapshot_item_t;

/* Local Storage stats summary */
typedef struct {
    bool loaded;
    int snapshot_count;       /* Entries available in snapshots[] */
    size_t snapshot_total;    /* All snapshot directories */
    int64_t backup_total_bytes;
    int backup_file_count;
    storage_snapshot_item_t snapshots[128];

    int64_t cache_total_bytes;
    int cache_file_count;
    int cache_dir_count;

    int64_t hub_total_bytes;
    int hub_skill_count;

    char last_checked_at[32];
} storage_stats_t;


#define STORAGE_DEFAULT_BACKUP "~/.skillboard/backups"
#define STORAGE_DEFAULT_HUB "~/.skillboard/skills"
#define STORAGE_DEFAULT_CACHE "~/.skillboard/cache"

typedef struct {
    char expanded[1024];
    int subdirs;
    bool exists;
    bool writable;
} storage_directory_info_t;

void storage_probe_directory(const char *path, storage_directory_info_t *info);
void storage_collect(const char *hub_dir, const char *backup_dir, const char *cache_dir,
                     storage_stats_t *stats);
int storage_clear_directory(const char *path);
int storage_prune_snapshots(const char *backup_dir, int keep_count);
int storage_delete_snapshot(const char *backup_dir, const char *name);

#endif
