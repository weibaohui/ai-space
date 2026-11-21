#include "scanner.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>

static int compare_tools(const void *a, const void *b) {
    const discovered_tool_t *ta = (const discovered_tool_t *)a;
    const discovered_tool_t *tb = (const discovered_tool_t *)b;
    return strcmp(ta->name, tb->name);
}

static int count_skills_in_dir(const char *skills_path) {
    DIR *dir = opendir(skills_path);
    if (!dir) return 0;

    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        char item_path[1200];
        snprintf(item_path, sizeof(item_path), "%s/%s", skills_path, entry->d_name);

        struct stat st;
        if (stat(item_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            count++;
        }
    }
    closedir(dir);
    return count;
}

int scanner_scan(discovered_tool_t **out_tools, int *out_count) {
    if (!out_tools || !out_count) return -1;
    *out_tools = NULL;
    *out_count = 0;

    const char *home = getenv("HOME");
    if (!home || home[0] == '\0') {
        struct passwd *pw = getpwuid(getuid());
        if (pw) home = pw->pw_dir;
    }
    if (!home || home[0] == '\0') {
        fprintf(stderr, "[scanner] Unable to determine user HOME directory.\n");
        return -1;
    }

    DIR *home_dir = opendir(home);
    if (!home_dir) {
        perror("[scanner] Failed to open HOME directory");
        return -1;
    }

    int capacity = 32;
    int count = 0;
    discovered_tool_t *tools = (discovered_tool_t *)malloc(sizeof(discovered_tool_t) * capacity);
    if (!tools) {
        closedir(home_dir);
        return -1;
    }

    struct dirent *entry;
    while ((entry = readdir(home_dir)) != NULL) {
        /* We are looking for ~/.xxx/skills directory: entry name starts with '.' and is not '.' or '..' */
        if (entry->d_name[0] != '.') continue;
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        /* Tool name 'xxx' is the name minus the leading '.' */
        const char *tool_name = entry->d_name + 1;
        if (tool_name[0] == '\0') continue;

        char skills_path[1024];
        snprintf(skills_path, sizeof(skills_path), "%s/%s/skills", home, entry->d_name);

        struct stat st;
        if (stat(skills_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            if (count >= capacity) {
                capacity *= 2;
                discovered_tool_t *resized = (discovered_tool_t *)realloc(tools, sizeof(discovered_tool_t) * capacity);
                if (!resized) {
                    free(tools);
                    closedir(home_dir);
                    return -1;
                }
                tools = resized;
            }

            discovered_tool_t *t = &tools[count++];
            strncpy(t->name, tool_name, sizeof(t->name) - 1);
            t->name[sizeof(t->name) - 1] = '\0';

            strncpy(t->path, skills_path, sizeof(t->path) - 1);
            t->path[sizeof(t->path) - 1] = '\0';

            t->skill_count = count_skills_in_dir(skills_path);
        }
    }
    closedir(home_dir);

    if (count > 0) {
        qsort(tools, count, sizeof(discovered_tool_t), compare_tools);
    }

    *out_tools = tools;
    *out_count = count;
    return 0;
}
