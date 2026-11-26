#ifndef AI_SYNC_GIT_MERGE_H
#define AI_SYNC_GIT_MERGE_H

#include "file_tree.h"
#include "sync_backend.h"

typedef struct {
    char workspace[SYNC_PATH_CAP];
    char local_hash[32];
    char remote_oid[SYNC_GIT_OID_CAP];
    char local_oid[SYNC_GIT_OID_CAP];
    char result_oid[SYNC_GIT_OID_CAP];
    char conflicts[640];
} sync_git_draft_t;

/* 准备阶段只创建草稿，发布阶段才更新远端与本地主仓。 */
int sync_git_merge_prepare(sync_backend_t *backend, const char *root, const sync_drift_item_t *expected,
                           sync_git_draft_t *draft, char *error, size_t cap);
int sync_git_merge_stage(sync_backend_t *backend, const char *skill, sync_git_draft_t *draft,
                         char *error, size_t cap);
int sync_git_merge_publish(sync_backend_t *backend, const char *root, const char *skill,
                           sync_git_draft_t *draft, sync_baseline_item_t *confirmed, char *error, size_t cap);

#endif
