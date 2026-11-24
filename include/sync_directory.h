#ifndef AI_SYNC_DIRECTORY_H
#define AI_SYNC_DIRECTORY_H

#include "sync_backend.h"

#include "file_tree.h"

int sync_scan_directory(const char *root, bool allow_missing, sync_remote_manifest_t *out,
                        char *error, size_t cap);
int sync_compare_directory(const sync_remote_manifest_t *remote, const char *local,
                           const sync_baseline_t *baseline,
                           sync_drift_summary_t *out, char *error, size_t cap);
bool sync_drift_is_candidate(sync_drift_status_t status, bool pull);
bool sync_skill_matches(const char *path, const char *expected_hash);
int sync_transfer_batch(sync_backend_t *backend, const char *local, bool pull,
                        sync_progress_cb cb, void *user, sync_drift_summary_t *unchanged_drift);

#endif
