#include "sync_local_files.h"
#include "file_tree.h"
#include "portability.h"
#include "skill_fingerprint.h"
#include "skill_metadata.h"

#include <errno.h>
#include <string.h>
#include <sys/stat.h>

bool sync_skill_matches(const char *path, const char *expected_hash) {
    struct stat st;
    if (!expected_hash[0]) return lstat(path, &st) != 0 && errno == ENOENT;
    char hash[32];
    return skill_is_directory(path) && hub_compute_skill_hash(path, hash, sizeof(hash), NULL) == 0 &&
           strcmp(hash, expected_hash) == 0;
}

int sync_publish_local_skill(const char *stage, const char *replacement, const char *target,
                             const sync_drift_item_t *expected, bool pull) {
    if (expected) {
        const char *source_hash = pull ? expected->remote_hash : expected->local_hash;
        const char *target_hash = pull ? expected->local_hash : expected->remote_hash;
        if (!sync_skill_matches(replacement, source_hash) || !sync_skill_matches(target, target_hash))
            return SYNC_TRANSFER_CHANGED;
    }
    return sync_replace_tree(stage, replacement, target);
}

int sync_copy_local_skill(const char *local, const char *remote, const char *name,
                          const sync_drift_item_t *expected, bool pull) {
    const char *source_root = pull ? remote : local;
    const char *target_root = pull ? local : remote;
    char source[SYNC_PATH_CAP], target[SYNC_PATH_CAP];
    if (!sync_valid_component(name) || skill_skip_name(name) ||
        sync_join(source, sizeof(source), source_root, name) != 0 || !skill_is_directory(source) ||
        sync_join(target, sizeof(target), target_root, name) != 0) return -1;

    if (pull) ai_mkdir_p(target_root);
    char stage[SYNC_PATH_CAP], replacement[SYNC_PATH_CAP];
    if (sync_make_stage(target_root, stage, sizeof(stage)) != 0) return -1;
    int rc = -1;
    if (sync_join(replacement, sizeof(replacement), stage, "new") == 0 &&
        file_tree_copy(source, replacement, false, skill_skip_name) == 0)
        rc = sync_publish_local_skill(stage, replacement, target, expected, pull);

    sync_cleanup_stage(stage, rc);
    return rc;
}
