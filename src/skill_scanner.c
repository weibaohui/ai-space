#include "skill_scanner.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>

static int compare_skills(const void *a, const void *b) {
    const skill_item_t *sa = (const skill_item_t *)a;
    const skill_item_t *sb = (const skill_item_t *)b;
    return strcmp(sa->name, sb->name);
}

static int compare_file_nodes(const void *a, const void *b) {
    const file_node_t *na = (const file_node_t *)a;
    const file_node_t *nb = (const file_node_t *)b;
    /* SKILL.md always at the top */
    if (strcmp(na->name, "SKILL.md") == 0) return -1;
    if (strcmp(nb->name, "SKILL.md") == 0) return 1;
    if (na->is_dir != nb->is_dir) {
        return na->is_dir ? -1 : 1; /* directories first */
    }
    return strcmp(na->name, nb->name);
}

/* Extract a human-readable description from SKILL.md */
static void extract_skill_description(const char *skill_md_path, char *out_desc, size_t max_len) {
    out_desc[0] = '\0';
    FILE *f = fopen(skill_md_path, "r");
    if (!f) return;

    char line[512];
    bool in_frontmatter = false;
    char fallback_desc[256] = "";

    while (fgets(line, sizeof(line), f)) {
        /* Trim newline */
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }

        if (strcmp(line, "---") == 0) {
            in_frontmatter = !in_frontmatter;
            continue;
        }

        if (in_frontmatter) {
            if (strncmp(line, "description:", 12) == 0 ||
                strncmp(line, "description_zh:", 15) == 0) {
                const char *p = strchr(line, ':') + 1;
                while (*p == ' ' || *p == '\t' || *p == '"' || *p == '\'') p++;
                strncpy(out_desc, p, max_len - 1);
                out_desc[max_len - 1] = '\0';
                /* Remove trailing quotes */
                size_t dlen = strlen(out_desc);
                while (dlen > 0 && (out_desc[dlen - 1] == '"' || out_desc[dlen - 1] == '\'')) {
                    out_desc[--dlen] = '\0';
                }
                break;
            }
        } else {
            /* If no frontmatter description found yet, look for first non-header, non-empty paragraph */
            if (fallback_desc[0] == '\0' && line[0] != '#' && line[0] != '\0' && line[0] != '-') {
                const char *p = line;
                while (*p == ' ' || *p == '\t') p++;
                if (*p != '\0') {
                    strncpy(fallback_desc, p, sizeof(fallback_desc) - 1);
                    fallback_desc[sizeof(fallback_desc) - 1] = '\0';
                }
            }
        }
    }
    fclose(f);

    if (out_desc[0] == '\0' && fallback_desc[0] != '\0') {
        strncpy(out_desc, fallback_desc, max_len - 1);
        out_desc[max_len - 1] = '\0';
    }
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

            /* Check SKILL.md */
            char skill_md[1100];
            snprintf(skill_md, sizeof(skill_md), "%s/SKILL.md", skill_path);
            struct stat sm_st;
            if (stat(skill_md, &sm_st) == 0 && S_ISREG(sm_st.st_mode)) {
                item->has_skill_md = true;
                extract_skill_description(skill_md, item->description, sizeof(item->description));
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

file_node_t *build_skill_file_tree(const char *skill_dir_path, const char *rel_base, int depth, int max_depth) {
    if (!skill_dir_path || depth > max_depth) return NULL;

    DIR *dir = opendir(skill_dir_path);
    if (!dir) return NULL;

    file_node_t *root = (file_node_t *)calloc(1, sizeof(file_node_t));
    if (!root) {
        closedir(dir);
        return NULL;
    }

    /* Extract name of current folder */
    const char *last_slash = strrchr(skill_dir_path, '/');
    const char *dname = last_slash ? (last_slash + 1) : skill_dir_path;
    strncpy(root->name, dname, sizeof(root->name) - 1);
    strncpy(root->abs_path, skill_dir_path, sizeof(root->abs_path) - 1);
    strncpy(root->rel_path, rel_base ? rel_base : "", sizeof(root->rel_path) - 1);
    root->is_dir = true;
    root->is_open = (depth <= 1); /* open top-level folders by default */

    int cap = 16;
    int count = 0;
    file_node_t *children = (file_node_t *)malloc(sizeof(file_node_t) * cap);

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        if (strcmp(ent->d_name, "__pycache__") == 0) continue;

        char sub_abs[1024];
        snprintf(sub_abs, sizeof(sub_abs), "%s/%s", skill_dir_path, ent->d_name);

        char sub_rel[512];
        if (rel_base && rel_base[0] != '\0') {
            snprintf(sub_rel, sizeof(sub_rel), "%s/%s", rel_base, ent->d_name);
        } else {
            snprintf(sub_rel, sizeof(sub_rel), "%s", ent->d_name);
        }

        struct stat st;
        if (stat(sub_abs, &st) == 0) {
            if (count >= cap) {
                cap *= 2;
                file_node_t *re = (file_node_t *)realloc(children, sizeof(file_node_t) * cap);
                if (!re) break;
                children = re;
            }

            file_node_t *child = &children[count++];
            memset(child, 0, sizeof(file_node_t));
            strncpy(child->name, ent->d_name, sizeof(child->name) - 1);
            strncpy(child->abs_path, sub_abs, sizeof(child->abs_path) - 1);
            strncpy(child->rel_path, sub_rel, sizeof(child->rel_path) - 1);
            child->size = st.st_size;

            if (S_ISDIR(st.st_mode)) {
                child->is_dir = true;
                child->is_open = (depth < 2);
                /* Build sub-tree */
                file_node_t *sub_tree = build_skill_file_tree(sub_abs, sub_rel, depth + 1, max_depth);
                if (sub_tree) {
                    child->children = sub_tree->children;
                    child->child_count = sub_tree->child_count;
                    free(sub_tree);
                }
            } else {
                child->is_dir = false;
                child->is_open = false;
                child->children = NULL;
                child->child_count = 0;
            }
        }
    }
    closedir(dir);

    if (count > 0 && children) {
        qsort(children, count, sizeof(file_node_t), compare_file_nodes);
    }

    root->children = children;
    root->child_count = count;
    return root;
}

void free_skill_file_tree(file_node_t *root) {
    if (!root) return;
    if (root->children) {
        for (int i = 0; i < root->child_count; i++) {
            if (root->children[i].children) {
                free_skill_file_tree(&root->children[i]);
            }
        }
        free(root->children);
        root->children = NULL;
    }
    root->child_count = 0;
}

char *read_file_text_content(const char *file_path, size_t max_bytes,
                             size_t *out_size, int *out_lines, bool *out_is_binary) {
    if (!file_path) return NULL;
    if (out_size) *out_size = 0;
    if (out_lines) *out_lines = 0;
    if (out_is_binary) *out_is_binary = false;

    FILE *f = fopen(file_path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz < 0) {
        fclose(f);
        return NULL;
    }

    size_t to_read = (size_t)sz;
    if (to_read > max_bytes) {
        to_read = max_bytes;
    }

    char *buf = (char *)malloc(to_read + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }

    size_t n = fread(buf, 1, to_read, f);
    buf[n] = '\0';
    fclose(f);

    /* Check for binary characters in first 1024 bytes */
    bool is_bin = false;
    size_t check_len = n < 1024 ? n : 1024;
    for (size_t i = 0; i < check_len; i++) {
        if (buf[i] == '\0') {
            is_bin = true;
            break;
        }
    }

    int lines = 1;
    for (size_t i = 0; i < n; i++) {
        if (buf[i] == '\n') lines++;
    }

    if (out_size) *out_size = (size_t)sz;
    if (out_lines) *out_lines = lines;
    if (out_is_binary) *out_is_binary = is_bin;

    return buf;
}

const char *detect_syntax_lang(const char *filename) {
    if (!filename) return "text";
    const char *dot = strrchr(filename, '.');
    if (!dot) return "text";

    if (strcmp(dot, ".c") == 0 || strcmp(dot, ".h") == 0) return "c";
    if (strcmp(dot, ".cpp") == 0 || strcmp(dot, ".hpp") == 0 || strcmp(dot, ".cc") == 0) return "cpp";
    if (strcmp(dot, ".js") == 0 || strcmp(dot, ".jsx") == 0 || strcmp(dot, ".mjs") == 0) return "js";
    if (strcmp(dot, ".ts") == 0 || strcmp(dot, ".tsx") == 0) return "ts";
    if (strcmp(dot, ".py") == 0) return "python";
    if (strcmp(dot, ".sh") == 0 || strcmp(dot, ".bash") == 0 || strcmp(dot, ".zsh") == 0) return "shell";
    if (strcmp(dot, ".json") == 0) return "json";
    if (strcmp(dot, ".go") == 0) return "go";
    if (strcmp(dot, ".rs") == 0) return "rust";
    if (strcmp(dot, ".sql") == 0) return "sql";
    if (strcmp(dot, ".md") == 0 || strcmp(dot, ".markdown") == 0) return "markdown";
    if (strcmp(dot, ".yaml") == 0 || strcmp(dot, ".yml") == 0) return "shell";
    return "text";
}
