#include "hub_files.h"
#include "file_tree.h"
#include "portability.h"
#include "scanner.h"

#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void join_path(char *out, size_t cap, const char *parent, const char *relative) {
    assert(sync_join(out, cap, parent, relative) == 0);
}

static void write_fixture(const char *path, const char *content) {
    FILE *file = fopen(path, "wb");
    assert(file);
    assert(fputs(content, file) >= 0);
    assert(fclose(file) == 0);
}

static void make_dirs(const char *parent, const char *relative, char *out, size_t cap) {
    join_path(out, cap, parent, relative);
    ai_mkdir_p(out);
}

/* 在技能目录下建一个只含 SKILL.md 的技能。 */
static void make_skill(const char *skills, const char *name) {
    char dir[SYNC_PATH_CAP], document[SYNC_PATH_CAP], content[160];
    make_dirs(skills, name, dir, sizeof(dir));
    join_path(document, sizeof(document), dir, "SKILL.md");
    snprintf(content, sizeof(content), "%s\n", name);
    write_fixture(document, content);
}

static void file_matches(const char *path, const char *content) {
    FILE *file = fopen(path, "rb");
    assert(file);
    char buffer[64] = "";
    assert(fread(buffer, 1, sizeof(buffer) - 1, file) == strlen(content));
    assert(fclose(file) == 0);
    assert(strcmp(buffer, content) == 0);
}

static bool is_link_to(const char *path, const char *target) {
    char actual[512];
    ssize_t len = readlink(path, actual, sizeof(actual) - 1);
    if (len < 0) return false;
    actual[len] = '\0';
    return strcmp(actual, target) == 0;
}

/* 统计目录条目，part 非空时只统计名称中含有它的条目。 */
static int count_entries(const char *path, const char *part) {
    DIR *dir = opendir(path);
    assert(dir);
    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        if (part && !strstr(entry->d_name, part)) continue;
        count++;
    }
    closedir(dir);
    return count;
}

/* 取出目录中唯一的条目名，用于定位刚生成的快照。 */
static void only_entry(const char *path, char *out, size_t cap) {
    DIR *dir = opendir(path);
    assert(dir);
    struct dirent *entry;
    int count = 0;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        assert(++count == 1);
        snprintf(out, cap, "%s", entry->d_name);
    }
    closedir(dir);
    assert(count == 1);
}

/* 名称含有 part 的每一份快照里，file 的内容都应与 content 一致。 */
static void snapshots_contain(const char *backup, const char *part, const char *file, const char *content) {
    DIR *dir = opendir(backup);
    assert(dir);
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (!strstr(entry->d_name, part)) continue;
        char snapshot[SYNC_PATH_CAP], path[SYNC_PATH_CAP];
        join_path(snapshot, sizeof(snapshot), backup, entry->d_name);
        join_path(path, sizeof(path), snapshot, file);
        file_matches(path, content);
    }
    closedir(dir);
}

int main(void) {
    char root[] = "/tmp/skillboard-tool-clear-XXXXXX";
    assert(mkdtemp(root));
    /* 备份目录和工具扫描都落在临时 HOME 内，不触碰真实的 ~/.skillboard。 */
    assert(ai_setenv("HOME", root) == 0);

    char skills[SYNC_PATH_CAP], backup[SYNC_PATH_CAP], guard[SYNC_PATH_CAP], alias[SYNC_PATH_CAP];
    char readonly[SYNC_PATH_CAP], path[SYNC_PATH_CAP], nested[SYNC_PATH_CAP];
    join_path(skills, sizeof(skills), root, ".demo/skills");
    join_path(backup, sizeof(backup), root, ".skillboard/backups");
    join_path(guard, sizeof(guard), root, ".guard/skills");
    join_path(alias, sizeof(alias), root, ".alias/skills");
    join_path(readonly, sizeof(readonly), root, ".ro/skills");

    /* 普通技能、含内部符号链接的技能、指向外部的技能链接、悬空链接和隐藏目录各一个。 */
    make_skill(skills, "alpha");
    make_skill(skills, "beta");
    make_dirs(skills, "beta/node_modules/tool", path, sizeof(path));
    join_path(nested, sizeof(nested), path, "cli.js");
    write_fixture(nested, "cli\n");
    make_dirs(skills, "beta/node_modules/.bin", path, sizeof(path));
    join_path(nested, sizeof(nested), path, "tool");
    assert(symlink("../tool/cli.js", nested) == 0);
    make_skill(root, "shared/real");
    join_path(path, sizeof(path), skills, "linked");
    assert(symlink("../../shared/real", path) == 0);
    join_path(path, sizeof(path), skills, "dangling");
    assert(symlink("../../missing", path) == 0);
    make_skill(skills, ".hidden");

    int cleared = -1;
    assert(hub_clear_tool_skills(skills, "demo", &cleared) == 0 && cleared == 5);
    assert(count_entries(skills, NULL) == 0);

    /* 整个目录的内容原样出现在唯一一份快照中，链接仍是链接。 */
    char name[256], snapshot[SYNC_PATH_CAP];
    only_entry(backup, name, sizeof(name));
    assert(strlen(name) >= 31 && strcmp(name + strlen(name) - 16, "_demo_ALL-SKILLS") == 0);
    join_path(snapshot, sizeof(snapshot), backup, name);
    assert(count_entries(snapshot, NULL) == 5);
    join_path(path, sizeof(path), snapshot, "alpha/SKILL.md");
    file_matches(path, "alpha\n");
    join_path(path, sizeof(path), snapshot, ".hidden/SKILL.md");
    file_matches(path, ".hidden\n");
    join_path(path, sizeof(path), snapshot, "beta/node_modules/.bin/tool");
    assert(is_link_to(path, "../tool/cli.js"));
    join_path(path, sizeof(path), snapshot, "linked");
    assert(is_link_to(path, "../../shared/real"));
    join_path(path, sizeof(path), snapshot, "dangling");
    assert(is_link_to(path, "../../missing"));

    /* 链接指向的外部技能不能被波及。 */
    join_path(path, sizeof(path), root, "shared/real/SKILL.md");
    file_matches(path, "shared/real\n");

    /* 清空后目录本身保留，工具仍被识别，技能数归零。 */
    struct stat st;
    assert(stat(skills, &st) == 0 && S_ISDIR(st.st_mode));
    discovered_tool_t *tools = NULL;
    int tool_count = 0;
    assert(scanner_scan(&tools, &tool_count) == 0);
    int found = 0;
    for (int i = 0; i < tool_count; i++) {
        if (strcmp(tools[i].name, "demo") == 0) {
            found++;
            assert(strcmp(tools[i].path, skills) == 0 && tools[i].skill_count == 0);
        }
    }
    assert(found == 1);
    free(tools);

    /* 非技能目录、符号链接形式的技能目录、非法工具名和不存在的目录一律拒绝。 */
    make_skill(guard, "kept");
    make_dirs(root, ".alias", path, sizeof(path));
    assert(symlink("../.guard/skills", alias) == 0);
    join_path(path, sizeof(path), root, ".guard");
    char tool_dir[SYNC_PATH_CAP];
    snprintf(tool_dir, sizeof(tool_dir), "%s", path);
    join_path(path, sizeof(path), root, ".none/skills");
    char missing[SYNC_PATH_CAP];
    snprintf(missing, sizeof(missing), "%s", path);
    assert(hub_clear_tool_skills(tool_dir, "guard", &cleared) != 0 && cleared == 0);
    assert(hub_clear_tool_skills(alias, "alias", &cleared) != 0 && cleared == 0);
    assert(hub_clear_tool_skills(guard, "gu/ard", &cleared) != 0 && cleared == 0);
    assert(hub_clear_tool_skills(missing, "none", &cleared) != 0 && cleared == 0);
    assert(count_entries(guard, NULL) == 1);
    only_entry(backup, name, sizeof(name));
    assert(strcmp(name + strlen(name) - 16, "_demo_ALL-SKILLS") == 0);

    /* 目录不可写时一条都不移出，也不留下空的快照目录。 */
    if (geteuid() != 0) {
        make_skill(readonly, "stuck");
        assert(chmod(readonly, 0500) == 0);
        cleared = -1;
        assert(hub_clear_tool_skills(readonly, "ro", &cleared) != 0 && cleared == 0);
        assert(count_entries(readonly, NULL) == 1);
        assert(chmod(readonly, 0755) == 0);

        /* 备份目录不可写、快照建不出来时同样必须报告失败。 */
        assert(chmod(backup, 0500) == 0);
        cleared = -1;
        assert(hub_clear_tool_skills(guard, "guard", &cleared) != 0 && cleared == 0);
        assert(count_entries(guard, NULL) == 1);
        assert(chmod(backup, 0755) == 0);
    }

    /* 结尾带斜杠的路径同样可用；目录已空时直接成功，不生成空快照。 */
    char slashed[SYNC_PATH_CAP];
    snprintf(slashed, sizeof(slashed), "%s/", guard);
    assert(hub_clear_tool_skills(slashed, "guard", &cleared) == 0 && cleared == 1);
    assert(hub_clear_tool_skills(guard, "guard", &cleared) == 0 && cleared == 0);
    assert(count_entries(backup, NULL) == 2);

    /* 同一秒内再次清空时快照名称不会冲突。 */
    make_skill(guard, "again");
    assert(hub_clear_tool_skills(guard, "guard", &cleared) == 0 && cleared == 1);
    assert(count_entries(backup, NULL) == 3);

    /* 按技能的备份与清空共用快照命名：覆盖和卸载前各留一份旧版本。 */
    char hub[SYNC_PATH_CAP], pkg[SYNC_PATH_CAP];
    join_path(hub, sizeof(hub), root, ".skillboard/skills");
    join_path(pkg, sizeof(pkg), root, ".pkg/skills");
    make_skill(hub, "pkg");
    assert(hub_sync_master_to_tool(pkg, "pkg", "pkg") == 0);
    assert(hub_sync_master_to_tool(pkg, "pkg", "pkg") == 0);
    assert(hub_uninstall_skill_from_tool(pkg, "pkg", "pkg") == 0);
    assert(count_entries(pkg, NULL) == 0);
    assert(count_entries(backup, "_pkg_pkg") == 2);
    assert(count_entries(backup, NULL) == 5);
    snapshots_contain(backup, "_pkg_pkg", "SKILL.md", "pkg\n");

    assert(sync_remove_tree(root) == 0);
    puts("PASS: tool clear moves every entry into one snapshot, keeps links, refuses unsafe targets");
    return 0;
}
