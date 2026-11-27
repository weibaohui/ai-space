#include "file_preview.h"
#include "file_tree.h"
#include "portability.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    char root[] = "/tmp/skillboard-file-edit-XXXXXX";
    assert(mkdtemp(root));

    char text_path[SYNC_PATH_CAP], bin_path[SYNC_PATH_CAP], large_path[SYNC_PATH_CAP], missing_dir[SYNC_PATH_CAP];
    assert(sync_join(text_path, sizeof(text_path), root, "SKILL.md") == 0);
    assert(sync_join(bin_path, sizeof(bin_path), root, "blob.bin") == 0);
    assert(sync_join(large_path, sizeof(large_path), root, "large.txt") == 0);
    assert(sync_join(missing_dir, sizeof(missing_dir), root, "no-such-dir/file.txt") == 0);

    /* 1. 原子写入与可编辑加载（含缓冲区余量） */
    const char *v1 = "# Skill\nline 2\n";
    assert(write_file_atomic(text_path, v1, strlen(v1)) == 0);

    size_t cap = 0, sz = 0;
    int lines = 0;
    bool is_bin = false, editable = false;
    char *buf = read_file_for_edit(text_path, 1024, 256, 512, &cap, &sz, &lines, &is_bin, &editable);
    assert(buf && editable && !is_bin);
    assert(cap >= 512 && sz == strlen(v1) && lines == 3);
    assert(strcmp(buf, v1) == 0);

    /* 2. 原地编辑并原子覆盖保存 */
    const char *v2 = "# Skill Updated\nline 2\nline 3";
    snprintf(buf, cap, "%s", v2);
    assert(write_file_atomic(text_path, buf, strlen(buf)) == 0);
    free(buf);

    buf = read_file_for_edit(text_path, 1024, 256, 512, &cap, &sz, &lines, &is_bin, &editable);
    assert(buf && editable && !is_bin && sz == strlen(v2) && lines == 3);
    assert(strcmp(buf, v2) == 0);
    free(buf);

    /* 3. 空文件可写入、二进制文件不可编辑 */
    assert(write_file_atomic(text_path, "", 0) == 0);
    buf = read_file_for_edit(text_path, 1024, 256, 512, &cap, &sz, &lines, &is_bin, &editable);
    assert(buf && editable && !is_bin && sz == 0 && buf[0] == '\0');
    free(buf);

    const char bin_data[] = {'\x89', 'P', 'N', 'G', '\0', 1, 2, 3};
    assert(write_file_atomic(bin_path, bin_data, sizeof(bin_data)) == 0);
    buf = read_file_for_edit(bin_path, 1024, 256, 512, &cap, &sz, &lines, &is_bin, &editable);
    assert(buf && is_bin && !editable);
    free(buf);

    /* 4. 超出上限的文件保持只读 */
    char big[1500];
    memset(big, 'x', sizeof(big));
    assert(write_file_atomic(large_path, big, sizeof(big)) == 0);
    buf = read_file_for_edit(large_path, 1024, 256, 512, &cap, &sz, &lines, &is_bin, &editable);
    assert(buf && !is_bin && !editable && sz == sizeof(big));
    free(buf);

    /* 5. 目标目录不存在时安全失败且不残留临时文件 */
    assert(write_file_atomic(missing_dir, "hello", 5) != 0);

    sync_remove_tree(root);
    puts("PASS: editable buffer allocation, atomic overwrite, empty/binary/oversize guards");
    return 0;
}
