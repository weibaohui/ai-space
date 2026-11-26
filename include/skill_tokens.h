#ifndef SKILL_TOKENS_H
#define SKILL_TOKENS_H

#include <stddef.h>

/* 单个技能的上下文 Token 开销估算（用于横向对比与预算预警）。 */
typedef struct {
    size_t prompt_chars;    /* name + "\n" + description 字符数 */
    int prompt_tokens;      /* 常驻注入估算 Token 数（name + description） */
    size_t full_bytes;      /* SKILL.md 字节数 */
    int full_tokens;        /* 完整激活估算 Token 数（SKILL.md 全文） */
    int total_tokens;       /* 技能目录内全部可读文本/脚本的总 Token 数 */
} skill_token_usage_t;

/* 多语言感知的 UTF-8 文本 Token 估算（区分英文词/符号与 CJK 字符）。 */
int skill_estimate_tokens(const char *text, size_t len);

/* 估算技能注入基线（name + "\n" + description）的字符数与 Token 数。 */
int skill_estimate_prompt_tokens(const char *name, const char *description, size_t *out_chars);

/* 估算单个可读文本文件的 Token 数（自动跳过含 NUL 字节的二进制文件）。 */
int skill_estimate_file_tokens(const char *file_path, size_t *out_bytes);

/* 统计技能目录的常驻注入、SKILL.md 全文及全目录文本 Token 开销。 */
int skill_measure_directory_tokens(const char *skill_dir, const char *skill_name,
                                   skill_token_usage_t *out);

/* 格式化 Token 数量为紧凑标签（如 "128"、"1.4k"）。 */
void skill_format_tokens(int tokens, char *out, size_t cap);

#endif /* SKILL_TOKENS_H */
