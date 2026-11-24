#ifndef SKILL_FILE_TREE_H
#define SKILL_FILE_TREE_H

#include <stdbool.h>
#include <stddef.h>

/* A node in the file tree of a skill */
typedef struct file_node_t {
    char name[128];             /* file or directory name */
    char rel_path[512];         /* path relative to the skill folder */
    char abs_path[1024];        /* full path on disk */
    bool is_dir;                /* true if directory, false if file */
    size_t size;                /* file size in bytes */
    bool is_open;               /* tree expansion state */
    struct file_node_t *children;
    int child_count;

    /* Diff metadata for Central Hub diff view */
    int diff_status;            /* 0: IN_SYNC, 1: MODIFIED, 2: ADDED, 3: DELETED */
    int added_lines;
    int removed_lines;
    bool has_diff_info;
} file_node_t;

/*
 * Recursively builds a file tree for a skill folder.
 * Returns the root file_node_t, or NULL if empty or error.
 */
file_node_t *build_skill_file_tree(const char *skill_dir_path, const char *rel_base, int depth, int max_depth);

/*
 * Frees memory allocated by build_skill_file_tree.
 */
void free_skill_file_tree(file_node_t *root);

#endif
