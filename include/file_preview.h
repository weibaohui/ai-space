#ifndef FILE_PREVIEW_H
#define FILE_PREVIEW_H

#include <stdbool.h>
#include <stddef.h>

/* 返回以 NUL 结尾的缓冲区，由调用方释放。
 * out_read 表示已读字节数，out_size 表示磁盘文件大小，两者均可省略。 */
char *read_file_preview(const char *file_path, size_t max_bytes, size_t *out_read,
                        size_t *out_size, int *out_lines, bool *out_is_binary);
/* 为编辑器加载文本文件：分配至少 min_cap（且保留 headroom）的可写缓冲区。 */
char *read_file_for_edit(const char *file_path, size_t max_bytes, size_t headroom, size_t min_cap,
                         size_t *out_cap, size_t *out_size, int *out_lines,
                         bool *out_is_binary, bool *out_editable);
/* 原子写入文件（同目录临时文件 + rename）。成功返回 0，失败返回 -1。 */
int write_file_atomic(const char *file_path, const char *content, size_t len);
const char *detect_syntax_lang(const char *filename);

#endif
