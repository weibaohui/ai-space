#include "skill_file_tree.h"
#include "skill_metadata.h"
#include "portability.h"

#include <dirent.h>
#include <string.h>
#include <sys/stat.h>

static int compare_file_nodes(const void *a, const void *b) {
    const file_node_t *na = (const file_node_t *)a;
    const file_node_t *nb = (const file_node_t *)b;
    /* 将 SKILL.md 固定在列表顶部。 */
    bool a_document = strcmp(na->name, SKILL_DOCUMENT) == 0;
    bool b_document = strcmp(nb->name, SKILL_DOCUMENT) == 0;
    if (a_document != b_document) return a_document ? -1 : 1;
    if (na->is_dir != nb->is_dir) {
        return na->is_dir ? -1 : 1; /* directories first */
    }
    return strcmp(na->name, nb->name);
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

    /* 从路径末段取得文件树节点的显示名称。 */
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

    if (!children) {
        free(root);
        closedir(dir);
        return NULL;
    }

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (skill_skip_name(ent->d_name)) continue;

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
                /* 目录节点继续读取下一级，文件节点直接收录。 */
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
