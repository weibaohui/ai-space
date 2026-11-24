#ifndef FILE_TREE_H
#define FILE_TREE_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#define SYNC_PATH_CAP 1024
#define SYNC_TREE_DEPTH 64
enum { SYNC_REPLACE_RESTORE_FAILED = -2 };

bool sync_valid_component(const char *name);
int sync_join(char *out, size_t cap, const char *parent, const char *name);
int sync_expand(const char *path, char *out, size_t cap);
int sync_make_stage(const char *root, char *out, size_t cap);
int sync_make_temp_stage(char *out, size_t cap);
/* 回滚失败时暂存目录可能保留唯一的旧副本，交由调用方提示用户恢复。 */
void sync_cleanup_stage(const char *stage, int result);
int sync_remove_tree(const char *path);
int sync_set_mtime(const char *path, time_t mtime);
typedef bool (*file_tree_skip_fn)(const char *name);
int file_tree_copy(const char *source, const char *target, bool merge, file_tree_skip_fn skip);
int sync_replace_tree(const char *stage, const char *replacement, const char *target);

#endif
