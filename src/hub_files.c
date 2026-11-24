#include "hub_files.h"
#include "hub_paths.h"
#include "skill_metadata.h"
#include "file_tree.h"
#include "portability.h"

#include <errno.h>
#include <string.h>
#include <sys/stat.h>

/* 快照先在备份目录内完整生成，同秒重名时使用序号。 */
static int backup_skill(const char *tool, const char *skill, const char *source) {
    char root[512];
    if (hub_init_dirs(NULL, 0, root, sizeof(root)) != 0) return -1;
    time_t now = time(NULL);
    struct tm tm_info;
    if (!ai_localtime(&now, &tm_info)) return -1;

    char time_str[32];
    if (strftime(time_str, sizeof(time_str), "%Y%m%d_%H%M%S", &tm_info) == 0) return -1;
    if (!sync_valid_component(tool) || !sync_valid_component(skill)) return -1;

    char stage[SYNC_PATH_CAP], copy[SYNC_PATH_CAP], destination[SYNC_PATH_CAP];
    if (sync_make_stage(root, stage, sizeof(stage)) != 0) return -1;
    int rc = -1;
    if (sync_join(copy, sizeof(copy), stage, "snapshot") == 0 &&
        file_tree_copy(source, copy, false, NULL) == 0) {
        for (int attempt = 0; attempt < 128; attempt++) {
            int n = attempt == 0
                ? snprintf(destination, sizeof(destination), "%s/%s_%s_%s", root, time_str, tool, skill)
                : snprintf(destination, sizeof(destination), "%s/%s_%s_%s-%d", root, time_str, tool, skill, attempt);
            if (n < 0 || (size_t)n >= sizeof(destination)) break;
            struct stat st;
            if (lstat(destination, &st) == 0) continue;
            if (errno != ENOENT) break;
            rc = rename(copy, destination);
            break;
        }
    }
    sync_remove_tree(stage);
    return rc;
}

static int replace_skill(const char *source_root, const char *target_root,
                         const char *skill, const char *target_label) {
    if (!source_root || !source_root[0] || !target_root || !target_root[0] ||
        !sync_valid_component(skill) || skill_skip_name(skill)) return -1;
    char source[SYNC_PATH_CAP], target[SYNC_PATH_CAP];
    if (sync_join(source, sizeof(source), source_root, skill) != 0 ||
        sync_join(target, sizeof(target), target_root, skill) != 0 || !skill_is_directory(source)) return -1;

    char stage[SYNC_PATH_CAP], replacement[SYNC_PATH_CAP];
    if (sync_make_stage(target_root, stage, sizeof(stage)) != 0) return -1;
    int rc = -1;
    if (sync_join(replacement, sizeof(replacement), stage, "new") != 0 ||
        file_tree_copy(source, replacement, false, skill_skip_name) != 0) goto done;

    /* 覆盖目标前先备份旧版本，备份失败就保留原状。 */
    struct stat current;
    if (lstat(target, &current) == 0) {
        if (!S_ISDIR(current.st_mode) || backup_skill(target_label, skill, target) != 0) goto done;
    } else if (errno != ENOENT) {
        goto done;
    }
    rc = sync_replace_tree(stage, replacement, target);
done:
    sync_cleanup_stage(stage, rc);
    return rc;
}

static int delete_skill(const char *root, const char *skill, const char *label, bool allow_missing) {
    if (!root || !root[0] || !sync_valid_component(skill) || skill_skip_name(skill)) return -1;
    char target[SYNC_PATH_CAP];
    if (sync_join(target, sizeof(target), root, skill) != 0) return -1;
    struct stat st;
    if (lstat(target, &st) != 0) return allow_missing && errno == ENOENT ? 0 : -1;
    if (!S_ISDIR(st.st_mode) || backup_skill(label, skill, target) != 0) return -1;

    char stage[SYNC_PATH_CAP], removed[SYNC_PATH_CAP];
    if (sync_make_stage(root, stage, sizeof(stage)) != 0) return -1;
    int rc = sync_join(removed, sizeof(removed), stage, "previous");
    if (rc == 0) rc = rename(target, removed);
    if (sync_remove_tree(stage) != 0) return -1;
    return rc;
}

int hub_promote_to_master(const char *tool_skills_path, const char *skill_name) {
    char hub[512];
    if (hub_init_dirs(hub, sizeof(hub), NULL, 0) != 0) return -1;
    return replace_skill(tool_skills_path, hub, skill_name, "master");
}

int hub_sync_master_to_tool(const char *tool_skills_path, const char *skill_name, const char *tool_name) {
    char hub[512];
    if (hub_init_dirs(hub, sizeof(hub), NULL, 0) != 0) return -1;
    if (tool_skills_path && tool_skills_path[0]) ai_mkdir_p(tool_skills_path);
    return replace_skill(hub, tool_skills_path, skill_name, tool_name ? tool_name : "tool");
}

int hub_uninstall_skill_from_tool(const char *tool_skills_path, const char *skill_name, const char *tool_name) {
    return delete_skill(tool_skills_path, skill_name, tool_name ? tool_name : "tool", true);
}

int hub_delete_master_skill(const char *skill_name) {
    char hub[512];
    if (hub_init_dirs(hub, sizeof(hub), NULL, 0) != 0) return -1;
    return delete_skill(hub, skill_name, "master", false);
}
