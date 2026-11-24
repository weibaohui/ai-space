#ifndef FILE_PREVIEW_H
#define FILE_PREVIEW_H

#include <stdbool.h>
#include <stddef.h>

/* 返回以 NUL 结尾的缓冲区，由调用方释放。
 * out_read 表示已读字节数，out_size 表示磁盘文件大小，两者均可省略。 */
char *read_file_preview(const char *file_path, size_t max_bytes, size_t *out_read,
                        size_t *out_size, int *out_lines, bool *out_is_binary);
const char *detect_syntax_lang(const char *filename);

#endif
