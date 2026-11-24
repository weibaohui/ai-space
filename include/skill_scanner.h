#ifndef SKILL_SCANNER_H
#define SKILL_SCANNER_H

#include <stdbool.h>
#include <stddef.h>

/* Information about a single skill folder inside ~/.xxx/skills/ */
typedef struct {
    char name[128];             /* skill directory name, e.g. "cui", "lark-doc" */
    char path[1024];            /* full filesystem path to this skill folder */
    int file_count;             /* count of files in this skill */
    bool has_skill_md;          /* whether SKILL.md exists */
    char description[256];      /* summary description parsed from SKILL.md */
} skill_item_t;

/*
 * Scans a tool's skills directory directly from the filesystem (no database used).
 * Allocates *out_items with *out_count elements.
 * Returns 0 on success, negative on error. Caller must free(*out_items).
 */
int scan_tool_skills(const char *tool_skills_path, skill_item_t **out_items, int *out_count);

#endif /* SKILL_SCANNER_H */
