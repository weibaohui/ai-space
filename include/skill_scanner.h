#ifndef SKILL_SCANNER_H
#define SKILL_SCANNER_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    FILE_KIND_UNKNOWN = 0,
    FILE_KIND_MARKDOWN = 1,     /* .md, .markdown */
    FILE_KIND_SCRIPT = 2,       /* .sh, .py, .js, .ts, .bash, etc. */
    FILE_KIND_TEXT = 3,         /* .txt, .json, .yaml, .yml, .toml, configs */
    FILE_KIND_BINARY = 4,       /* image, binary */
} file_kind_t;

/* Information about a single skill folder inside ~/.xxx/skills/ */
typedef struct {
    char name[128];             /* skill directory name, e.g. "cui", "lark-doc" */
    char path[1024];            /* full filesystem path to this skill folder */
    int file_count;             /* count of files in this skill */
    bool has_skill_md;          /* whether SKILL.md exists */
    char description[256];      /* summary description parsed from SKILL.md */
} skill_item_t;

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
} file_node_t;

/*
 * Scans a tool's skills directory directly from the filesystem (no database used).
 * Allocates *out_items with *out_count elements.
 * Returns 0 on success, negative on error. Caller must free(*out_items).
 */
int scan_tool_skills(const char *tool_skills_path, skill_item_t **out_items, int *out_count);

/*
 * Recursively builds a file tree for a skill folder.
 * Returns the root file_node_t, or NULL if empty or error.
 */
file_node_t *build_skill_file_tree(const char *skill_dir_path, const char *rel_base, int depth, int max_depth);

/*
 * Frees memory allocated by build_skill_file_tree.
 */
void free_skill_file_tree(file_node_t *root);

/*
 * Reads a text file's contents into a newly allocated string buffer.
 * If file exceeds max_bytes, reads up to max_bytes.
 * Returns NULL on read error. Caller must free().
 */
char *read_file_text_content(const char *file_path, size_t max_bytes,
                             size_t *out_size, int *out_lines, bool *out_is_binary);

/*
 * Detects whether a file is markdown, script, text, or binary.
 */
file_kind_t detect_file_kind(const char *filename, bool is_binary);

/*
 * Returns a human-friendly label for the file kind.
 */
const char *get_file_kind_label(file_kind_t kind);

/*
 * Returns a syntax language string for CUI code block based on file extension.
 * e.g. "c", "python", "shell", "json", "markdown", "text".
 */
const char *detect_syntax_lang(const char *filename);

#endif /* SKILL_SCANNER_H */
