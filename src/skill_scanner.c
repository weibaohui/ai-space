#include "skill_scanner.h"
#include "skill_metadata.h"
#include "file_tree.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <ctype.h>
#include "portability.h"

static int compare_skills(const void *a, const void *b) {
    const skill_item_t *sa = (const skill_item_t *)a;
    const skill_item_t *sb = (const skill_item_t *)b;
    return strcmp(sa->name, sb->name);
}

static int count_files_recursive(const char *dir_path, int depth) {
    if (depth > 8) return 0;
    DIR *d = opendir(dir_path);
    if (!d) return 0;

    int count = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        char sub[1200];
        snprintf(sub, sizeof(sub), "%s/%s", dir_path, ent->d_name);
        struct stat st;
        if (stat(sub, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                count += count_files_recursive(sub, depth + 1);
            } else {
                count++;
            }
        }
    }
    closedir(d);
    return count;
}

int scan_tool_skills(const char *tool_skills_path, skill_item_t **out_items, int *out_count) {
    if (!tool_skills_path || !out_items || !out_count) return -1;
    *out_items = NULL;
    *out_count = 0;

    DIR *dir = opendir(tool_skills_path);
    if (!dir) return -1;

    int cap = 32;
    int count = 0;
    skill_item_t *items = (skill_item_t *)malloc(sizeof(skill_item_t) * cap);
    if (!items) {
        closedir(dir);
        return -1;
    }

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') continue;

        char skill_path[1024];
        snprintf(skill_path, sizeof(skill_path), "%s/%s", tool_skills_path, ent->d_name);

        struct stat st;
        if (stat(skill_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            if (count >= cap) {
                cap *= 2;
                skill_item_t *re = (skill_item_t *)realloc(items, sizeof(skill_item_t) * cap);
                if (!re) break;
                items = re;
            }

            skill_item_t *item = &items[count++];
            strncpy(item->name, ent->d_name, sizeof(item->name) - 1);
            item->name[sizeof(item->name) - 1] = '\0';

            strncpy(item->path, skill_path, sizeof(item->path) - 1);
            item->path[sizeof(item->path) - 1] = '\0';

            /* 仅将含有技能入口文档的目录收录到技能扫描结果。 */
            char skill_md[1100];
            if (sync_join(skill_md, sizeof(skill_md), skill_path, SKILL_DOCUMENT) == 0 &&
                skill_has_document(skill_path, true)) {
                item->has_skill_md = true;
                skill_read_description(skill_md, item->description, sizeof(item->description), true);
            } else {
                item->has_skill_md = false;
                item->description[0] = '\0';
            }

            item->file_count = count_files_recursive(skill_path, 0);
        }
    }
    closedir(dir);

    if (count > 0) {
        qsort(items, count, sizeof(skill_item_t), compare_skills);
    }

    *out_items = items;
    *out_count = count;
    return 0;
}
