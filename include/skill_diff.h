#ifndef SKILL_DIFF_H
#define SKILL_DIFF_H

#include <stdbool.h>
#include <stddef.h>

/* Status for individual files within a skill */
typedef enum {
    DIFF_FILE_IN_SYNC = 0,    /* Identical in both master and tool */
    DIFF_FILE_MODIFIED = 1,   /* Exists in both but content has drifted */
    DIFF_FILE_ADDED = 2,      /* Exists only in tool (added locally) */
    DIFF_FILE_DELETED = 3,    /* Exists only in master (missing in tool copy) */
} diff_file_status_t;

typedef struct {
    char rel_path[256];
    char file_name[128];
    diff_file_status_t status;
    size_t master_size;
    size_t tool_size;
    bool is_binary;
    int added_lines;            /* Added lines in tool vs master */
    int removed_lines;          /* Removed lines in tool vs master */
} hub_diff_file_item_t;

typedef struct {
    char skill_name[128];
    char tool_name[128];
    hub_diff_file_item_t *items;
    int count;
    int modified_count;
    int added_count;
    int deleted_count;
    int in_sync_count;
    int total_added_lines;
    int total_removed_lines;
} hub_skill_diff_manifest_t;

/* The caller supplies an empty manifest, optionally with display names set. */
int skill_diff_compute(const char *master, const char *tool_path, hub_skill_diff_manifest_t *manifest);
void free_skill_diff_manifest(hub_skill_diff_manifest_t *manifest);

#endif
