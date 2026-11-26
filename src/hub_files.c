#include "hub_files.h"
#include "hub_paths.h"
#include "skill_metadata.h"
#include "file_tree.h"
#include "portability.h"

#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>

/* 工具的技能目录名，以及整目录快照在备份目录中使用的名称后缀。 */
#define TOOL_SKILLS_DIR "skills"
#define TOOL_SKILLS_SNAPSHOT "ALL-SKILLS"

/* 在备份目录内选一个未被占用的时间戳快照路径，同秒重名时使用序号。 */
static int snapshot_destination(const char *root, const char *tool, const char *skill,
                                char *out, size_t cap) {
    time_t now = time(NULL);
    struct tm tm_info;
    char time_str[32];
    if (!ai_localtime(&now, &tm_info) ||
        strftime(time_str, sizeof(time_str), "%Y%m%d_%H%M%S", &tm_info) == 0) return -1;

    for (int attempt = 0; attempt < 128; attempt++) {
        int n = attempt == 0
            ? snprintf(out, cap, "%s/%s_%s_%s", root, time_str, tool, skill)
            : snprintf(out, cap, "%s/%s_%s_%s-%d", root, time_str, tool, skill, attempt);
        if (n < 0 || (size_t)n >= cap) break;
        struct stat st;
        if (lstat(out, &st) != 0) return errno == ENOENT ? 0 : -1;
    }
    return -1;
}

/* 快照先在备份目录内完整生成，再发布到时间戳目录。 */
static int backup_skill(const char *tool, const char *skill, const char *source) {
    char root[512];
    if (hub_init_dirs(NULL, 0, root, sizeof(root)) != 0) return -1;
    if (!sync_valid_component(tool) || !sync_valid_component(skill)) return -1;

    char stage[SYNC_PATH_CAP], copy[SYNC_PATH_CAP], destination[SYNC_PATH_CAP];
    if (sync_make_stage(root, stage, sizeof(stage)) != 0) return -1;
    int rc = -1;
    if (sync_join(copy, sizeof(copy), stage, "snapshot") == 0 &&
        file_tree_copy(source, copy, false, NULL) == 0 &&
        snapshot_destination(root, tool, skill, destination, sizeof(destination)) == 0) {
        rc = rename(copy, destination);
    }
    sync_remove_tree(stage);
    return rc;
}

/* 只接受以 skills 结尾的目录，避免误清空工具配置目录或主目录。 */
static bool is_skills_path(const char *path) {
    size_t len = strlen(path);
    while (len > 1 && path[len - 1] == '/') len--;
    return len > sizeof(TOOL_SKILLS_DIR) && path[len - sizeof(TOOL_SKILLS_DIR)] == '/' &&
           strncmp(path + len - (sizeof(TOOL_SKILLS_DIR) - 1), TOOL_SKILLS_DIR, sizeof(TOOL_SKILLS_DIR) - 1) == 0;
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

int hub_clear_tool_skills(const char *tool_skills_path, const char *tool_name, int *out_cleared) {
    if (out_cleared) *out_cleared = 0;

    /* 技能目录本身是符号链接时拒绝，避免清空它指向的其他目录。 */
    struct stat st;
    if (!tool_skills_path || !is_skills_path(tool_skills_path) || !sync_valid_component(tool_name) ||
        lstat(tool_skills_path, &st) != 0 || !S_ISDIR(st.st_mode)) return -1;

    DIR *dir = opendir(tool_skills_path);
    if (!dir) return -1;

    /* 先记下全部条目再移动，避免边遍历边改动目录；名称无法安全拼接时整体放弃。 */
    char (*names)[256] = NULL;
    int count = 0, capacity = 0, rc = -1;
    for (;;) {
        errno = 0;
        struct dirent *entry = readdir(dir);
        if (!entry) {
            rc = errno ? -1 : 0;
            break;
        }
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        if (!sync_valid_component(entry->d_name) || strlen(entry->d_name) >= sizeof(*names)) break;
        if (count == capacity) {
            int grown = capacity ? capacity * 2 : 32;
            void *resized = realloc(names, (size_t)grown * sizeof(*names));
            if (!resized) break;
            names = resized;
            capacity = grown;
        }
        snprintf(names[count++], sizeof(*names), "%s", entry->d_name);
    }
    closedir(dir);

    char backup[512], snapshot[SYNC_PATH_CAP];
    int moved = 0, rename_error = 0;
    if (rc == 0 && count > 0) {
        rc = -1;
        if (hub_init_dirs(NULL, 0, backup, sizeof(backup)) == 0 &&
            snapshot_destination(backup, tool_name, TOOL_SKILLS_SNAPSHOT, snapshot, sizeof(snapshot)) == 0 &&
            AI_MKDIR(snapshot) == 0) {
            /* 条目用 rename 移入快照：同盘瞬时完成，符号链接保持为链接，不会波及链接目标。 */
            for (int i = 0; i < count; i++) {
                char source[SYNC_PATH_CAP], target[SYNC_PATH_CAP];
                if (sync_join(source, sizeof(source), tool_skills_path, names[i]) != 0 ||
                    sync_join(target, sizeof(target), snapshot, names[i]) != 0) continue;
                if (rename(source, target) == 0) moved++;
                else if (!rename_error) rename_error = errno;
            }
            /* 一条都没移出时不保留空的快照目录。 */
            if (moved == 0) sync_remove_tree(snapshot);
            rc = moved == count ? 0 : moved == 0 && rename_error == EXDEV ? HUB_CLEAR_CROSS_DEVICE : -1;
        }
    }
    free(names);
    if (out_cleared) *out_cleared = moved;
    return rc;
}
