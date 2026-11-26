#include "skill_tokens.h"
#include "file_tree.h"
#include "portability.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void write_file(const char *path, const char *data, size_t len) {
    FILE *f = fopen(path, "wb");
    assert(f);
    assert(fwrite(data, 1, len, f) == len);
    assert(fclose(f) == 0);
}

int main(void) {
    /* 1. 空输入与基础中英文混合估算 */
    assert(skill_estimate_tokens(NULL, 0) == 0);
    assert(skill_estimate_tokens("", 0) == 0);

    const char *en = "Manage local agent skills and synchronize across hosts.";
    int en_tok = skill_estimate_tokens(en, strlen(en));
    assert(en_tok >= 8 && en_tok <= 20);

    const char *zh = "管理本地智能体技能并同步到多台主机";
    int zh_tok = skill_estimate_tokens(zh, strlen(zh));
    /* 中文每字约 1.6 token，17 个汉字应显著高于同字符数的纯英文。 */
    assert(zh_tok >= 20 && zh_tok <= 35);

    /* 2. 常驻注入（name + description）估算 */
    size_t chars = 0;
    int prompt_tok = skill_estimate_prompt_tokens("skillboard-sync", en, &chars);
    assert(chars == strlen("skillboard-sync") + 1 + strlen(en));
    assert(prompt_tok > en_tok);

    /* 3. 格式化标签 */
    char fmt[16];
    skill_format_tokens(42, fmt, sizeof(fmt));
    assert(strcmp(fmt, "42") == 0);
    skill_format_tokens(1450, fmt, sizeof(fmt));
    assert(strcmp(fmt, "1.5k") == 0 || strcmp(fmt, "1.4k") == 0);

    /* 4. 目录级 Token 统计：未截断的长 description、多文件累计、忽略二进制与隐藏目录 */
    char root[] = "/tmp/skillboard-tokens-XXXXXX";
    assert(mkdtemp(root));

    char skill_dir[SYNC_PATH_CAP], hidden_dir[SYNC_PATH_CAP], cache_dir[SYNC_PATH_CAP];
    char skill_md[SYNC_PATH_CAP], helper_py[SYNC_PATH_CAP], bin_file[SYNC_PATH_CAP], hidden_md[SYNC_PATH_CAP];
    assert(sync_join(skill_dir, sizeof(skill_dir), root, "demo-skill") == 0);
    ai_mkdir_p(skill_dir);
    assert(sync_join(hidden_dir, sizeof(hidden_dir), skill_dir, ".git") == 0);
    ai_mkdir_p(hidden_dir);
    assert(sync_join(cache_dir, sizeof(cache_dir), skill_dir, "__pycache__") == 0);
    ai_mkdir_p(cache_dir);

    char long_desc[420];
    memset(long_desc, 'a', 360);
    long_desc[360] = '\0';
    char md_body[1024];
    snprintf(md_body, sizeof(md_body),
             "---\nname: demo-skill\ndescription: %s\n---\n\n# Demo Skill\n\nDetailed instructions for the agent.\n",
             long_desc);

    assert(sync_join(skill_md, sizeof(skill_md), skill_dir, "SKILL.md") == 0);
    write_file(skill_md, md_body, strlen(md_body));

    const char *py_body = "def run():\n    print('hello skillboard')\n";
    assert(sync_join(helper_py, sizeof(helper_py), skill_dir, "helper.py") == 0);
    write_file(helper_py, py_body, strlen(py_body));

    const char bin_body[] = {'\x7f', 'E', 'L', 'F', '\0', '\x01', '\x02', '\x03'};
    assert(sync_join(bin_file, sizeof(bin_file), skill_dir, "blob.bin") == 0);
    write_file(bin_file, bin_body, sizeof(bin_body));

    const char *ignored_text = "this should not be counted in skill tokens";
    assert(sync_join(hidden_md, sizeof(hidden_md), hidden_dir, "config.txt") == 0);
    write_file(hidden_md, ignored_text, strlen(ignored_text));
    assert(sync_join(hidden_md, sizeof(hidden_md), cache_dir, "cached.txt") == 0);
    write_file(hidden_md, ignored_text, strlen(ignored_text));

    skill_token_usage_t usage = {0};
    assert(skill_measure_directory_tokens(skill_dir, "demo-skill", &usage) == 0);
    assert(usage.prompt_chars == strlen("demo-skill") + 1 + 360);
    assert(usage.prompt_tokens > 0);
    assert(usage.full_bytes == strlen(md_body));
    assert(usage.full_tokens >= usage.prompt_tokens);
    assert(usage.total_tokens > usage.full_tokens);

    int expected_total = usage.full_tokens + skill_estimate_tokens(py_body, strlen(py_body));
    assert(usage.total_tokens == expected_total);

    sync_remove_tree(root);
    puts("PASS: skill token estimation, prompt/full/directory totals, binary & hidden dir filtering");
    return 0;
}
