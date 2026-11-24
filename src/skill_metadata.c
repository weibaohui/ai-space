#include "skill_metadata.h"
#include "file_tree.h"
#include "portability.h"

#include <string.h>
#include <sys/stat.h>

bool skill_skip_name(const char *name) {
    return name[0] == '.' || strcmp(name, "__pycache__") == 0;
}

bool skill_has_document(const char *directory, bool follow_links) {
    char document[SYNC_PATH_CAP + sizeof(SKILL_DOCUMENT)];
    struct stat st;
    if (sync_join(document, sizeof(document), directory, SKILL_DOCUMENT) != 0) return false;
    int rc = follow_links ? stat(document, &st) : lstat(document, &st);
    return rc == 0 && S_ISREG(st.st_mode);
}

bool skill_is_directory(const char *path) {
    struct stat st;
    return lstat(path, &st) == 0 && S_ISDIR(st.st_mode) && skill_has_document(path, false);
}

void skill_read_description(const char *skill_md_path, char *out_desc, size_t max_len, bool allow_body) {
    if (!max_len) return;
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
        } else if (allow_body) {
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
