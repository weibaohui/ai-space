#ifndef FILE_PREVIEW_H
#define FILE_PREVIEW_H

#include <stdbool.h>
#include <stddef.h>

/* Returns an owned NUL-terminated buffer. out_read is the buffered byte count,
 * out_size is the full size on disk, and either may be omitted. */
char *read_file_preview(const char *file_path, size_t max_bytes, size_t *out_read,
                        size_t *out_size, int *out_lines, bool *out_is_binary);
const char *detect_syntax_lang(const char *filename);

#endif
