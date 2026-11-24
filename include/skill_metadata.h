#ifndef SKILL_METADATA_H
#define SKILL_METADATA_H

#include <stdbool.h>
#include <stddef.h>

#define SKILL_DOCUMENT "SKILL.md"

bool skill_skip_name(const char *name);
bool skill_has_document(const char *directory, bool follow_links);
bool skill_is_directory(const char *path);
void skill_read_description(const char *skill_md_path, char *out_desc, size_t max_len, bool allow_body);

#endif
