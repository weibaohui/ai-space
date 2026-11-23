#ifndef AI_SYNC_DIRECTORY_H
#define AI_SYNC_DIRECTORY_H

#include "sync_backend.h"

#define SYNC_PATH_CAP 1024
#define SYNC_TREE_DEPTH 64

bool sync_valid_component(const char *name);
bool sync_skip_name(const char *name);
int sync_join(char *out, size_t cap, const char *parent, const char *name);
int sync_expand(const char *path, char *out, size_t cap);
bool sync_is_skill(const char *path);
int sync_make_stage(const char *root, char *out, size_t cap);
int sync_remove_tree(const char *path);
int sync_set_mtime(const char *path, time_t mtime);
int sync_copy_tree(const char *source, const char *target, int depth);
int sync_replace_tree(const char *stage, const char *replacement, const char *target);
int sync_scan_directory(const char *root, bool allow_missing, sync_remote_manifest_t *out,
                        char *error, size_t cap);
int sync_compare_directory(const sync_remote_manifest_t *remote, const char *local,
                           sync_drift_summary_t *out, char *error, size_t cap);
int sync_transfer_batch(sync_backend_t *backend, const char *local, bool pull,
                        sync_progress_cb cb, void *user);

#endif
