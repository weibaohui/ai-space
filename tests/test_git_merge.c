#include "db.h"
#include "sync_baseline.h"
#include "sync_directory.h"
#include "portability.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char root[SYNC_PATH_CAP];
    char scope[SYNC_SCOPE_CAP];
    sqlite3 *db;
    sync_baseline_t baseline;
    sync_backend_t *backend;
} peer_t;

static void path(char *out, const char *parent, const char *name) {
    assert(sync_join(out, SYNC_PATH_CAP, parent, name) == 0);
}

static void init_peer(peer_t *p, sync_config_t cfg, const char *fixture, const char *name) {
    path(p->root, fixture, name);
    ai_mkdir_p(p->root);
    char db_file[SYNC_PATH_CAP];
    path(db_file, p->root, ".state.db");
    assert(db_init(db_file, &p->db) == 0);
    assert(sync_join(cfg.git_cache_dir, sizeof(cfg.git_cache_dir), p->root, ".git-cache") == 0);
    assert(sync_baseline_scope(&cfg, p->root, p->scope, sizeof(p->scope)) == 0);
    assert(sync_backend_create(&cfg, &p->backend) == 0);
    p->backend->baseline = &p->baseline;
}

static void destroy_peer(peer_t *p) {
    sync_backend_destroy(p->backend);
    free(p->baseline.items);
    db_close(p->db);
}

static void write_text(const char *file, const char *text) {
    FILE *f = fopen(file, "wb");
    assert(f && fputs(text, f) >= 0 && fclose(f) == 0);
}

static void write_skill_file(peer_t *p, const char *skill_name, const char *name, const char *text) {
    char dir[SYNC_PATH_CAP], file[SYNC_PATH_CAP];
    path(dir, p->root, skill_name);
    ai_mkdir_p(dir);
    path(file, dir, name);
    write_text(file, text);
}

static void remove_skill_file(peer_t *p, const char *skill_name, const char *name) {
    char dir[SYNC_PATH_CAP], file[SYNC_PATH_CAP];
    path(dir, p->root, skill_name);
    path(file, dir, name);
    assert(remove(file) == 0);
}

static void verify_skill_file(peer_t *p, const char *skill_name, const char *name, const char *text) {
    char dir[SYNC_PATH_CAP], file[SYNC_PATH_CAP], body[256];
    path(dir, p->root, skill_name);
    path(file, dir, name);
    FILE *f = fopen(file, "rb");
    assert(f && fgets(body, sizeof(body), f) && strcmp(body, text) == 0);
    fclose(f);
}

static bool skill_file_exists(peer_t *p, const char *skill_name, const char *name) {
    char dir[SYNC_PATH_CAP], file[SYNC_PATH_CAP];
    path(dir, p->root, skill_name);
    path(file, dir, name);
    return AI_ACCESS(file, F_OK) == 0;
}

static sync_drift_item_t check_skill(peer_t *p, const char *skill_name, sync_drift_status_t expected) {
    sync_drift_summary_t drift = {0};
    char error[256] = "";
    if (p->backend->ops->check_drift(p->backend, p->root, &drift, error, sizeof(error)) != 0) {
        fprintf(stderr, "check: %s\n", error);
        abort();
    }
    sync_drift_item_t item = {0};
    bool found = false;
    for (int i = 0; i < drift.count; i++) {
        if (strcmp(drift.items[i].skill_name, skill_name) == 0) {
            item = drift.items[i];
            found = true;
            break;
        }
    }
    assert(found);
    if (item.status != expected) {
        fprintf(stderr, "[%s] status=%d expected=%d (%s)\n", skill_name, item.status, expected, item.status_desc);
    }
    assert(item.status == expected);
    assert(sync_baseline_record(p->db, p->scope, &drift) == 0);
    sync_free_drift_summary(&drift);
    free(p->baseline.items);
    assert(sync_baseline_load(p->db, p->scope, &p->baseline) == 0);
    return item;
}

static int transfer_skill(peer_t *p, const char *skill_name, const sync_drift_item_t *item, bool pull, sync_progress_cb cb) {
    p->backend->expected_skill = item;
    int rc = pull ? p->backend->ops->pull_skill(p->backend, skill_name, p->root, cb, NULL)
                  : p->backend->ops->push_skill(p->backend, skill_name, p->root, cb, NULL);
    p->backend->expected_skill = NULL;
    return rc;
}

static int prepare(peer_t *p, sync_drift_item_t *item, sync_git_draft_t *draft, const char *rejection) {
    char error[256] = "";
    int rc = sync_git_merge_prepare(p->backend, p->root, item, draft, error, sizeof(error));
    if (rejection) assert(rc != 0 && strstr(error, rejection));
    else if (rc != 0) fprintf(stderr, "prepare: %s\n", error);
    return rc;
}

static int stage(peer_t *p, const char *skill_name, sync_git_draft_t *draft, const char *rejection) {
    char error[256] = "";
    int rc = sync_git_merge_stage(p->backend, skill_name, draft, error, sizeof(error));
    if (rejection) assert(rc != 0 && strstr(error, rejection));
    else if (rc != 0) fprintf(stderr, "stage: %s\n", error);
    return rc;
}

static int publish(peer_t *p, const char *skill_name, sync_git_draft_t *draft, const char *rejection) {
    char error[256] = "";
    sync_baseline_item_t confirmed;
    int rc = sync_git_merge_publish(p->backend, p->root, skill_name, draft, &confirmed, error, sizeof(error));
    if (rejection) assert(rc != 0 && strstr(error, rejection));
    else if (rc != 0) fprintf(stderr, "publish: %s\n", error);
    if (rc == 0) assert(!strcmp(confirmed.skill, skill_name) && confirmed.revision[0] && confirmed.hash[0]);
    return rc;
}

static void push_skill(peer_t *p, const char *skill_name) {
    sync_drift_item_t item = check_skill(p, skill_name, SYNC_DRIFT_LOCAL_AHEAD);
    assert(transfer_skill(p, skill_name, &item, false, NULL) == 0);
    check_skill(p, skill_name, SYNC_DRIFT_IN_SYNC);
}

static void pull_skill(peer_t *p, const char *skill_name) {
    sync_drift_item_t item = check_skill(p, skill_name, SYNC_DRIFT_REMOTE_AHEAD);
    assert(transfer_skill(p, skill_name, &item, true, NULL) == 0);
    check_skill(p, skill_name, SYNC_DRIFT_IN_SYNC);
}

static void seed_skill(peer_t *a, peer_t *b, const char *skill_name) {
    sync_drift_item_t item = check_skill(a, skill_name, SYNC_DRIFT_LOCAL_ONLY);
    assert(transfer_skill(a, skill_name, &item, false, NULL) == 0);
    check_skill(a, skill_name, SYNC_DRIFT_IN_SYNC);
    item = check_skill(b, skill_name, SYNC_DRIFT_REMOTE_ONLY);
    assert(transfer_skill(b, skill_name, &item, true, NULL) == 0);
    check_skill(b, skill_name, SYNC_DRIFT_IN_SYNC);
}

static void test_non_git_and_validation(peer_t *b, const char *fixture) {
    sync_config_t share_cfg = {.type = SYNC_BACKEND_NFS, .share_mode = SYNC_SHARE_MOUNTED};
    path(share_cfg.nfs_mount_path, fixture, "share-remote");
    ai_mkdir_p(share_cfg.nfs_mount_path);
    sync_backend_t *share_backend = NULL;
    assert(sync_backend_create(&share_cfg, &share_backend) == 0);

    sync_drift_item_t item = {.status = SYNC_DRIFT_CONFLICT};
    snprintf(item.skill_name, sizeof(item.skill_name), "%s", "conflict-demo");
    snprintf(item.local_hash, sizeof(item.local_hash), "%s", "1111");
    snprintf(item.remote_hash, sizeof(item.remote_hash), "%s", "2222");
    sync_git_draft_t draft = {0};
    char error[256] = "";
    assert(sync_git_merge_prepare(share_backend, b->root, &item, &draft, error, sizeof(error)) != 0);
    assert(strstr(error, "当前没有可创建的 Git 合并"));
    assert(sync_git_merge_stage(share_backend, "conflict-demo", &draft, error, sizeof(error)) != 0);
    assert(strstr(error, "合并草稿不可暂存"));
    assert(sync_git_merge_publish(share_backend, b->root, "conflict-demo", &draft, NULL, error, sizeof(error)) != 0);
    assert(strstr(error, "合并草稿不可用"));
    sync_backend_destroy(share_backend);

    /* 非冲突状态或非法技能名应被拒绝 */
    item.status = SYNC_DRIFT_IN_SYNC;
    assert(prepare(b, &item, &draft, "当前没有可创建的 Git 合并") != 0);
    item.status = SYNC_DRIFT_CONFLICT;
    snprintf(item.skill_name, sizeof(item.skill_name), "%s", "../escape");
    assert(prepare(b, &item, &draft, "当前没有可创建的 Git 合并") != 0);

    /* 伪造草稿工作区路径应被拒绝 */
    sync_git_draft_t tampered = {0};
    snprintf(tampered.workspace, sizeof(tampered.workspace), "%s", "/tmp/evil-workspace");
    snprintf(tampered.remote_oid, sizeof(tampered.remote_oid), "%040d", 1);
    snprintf(tampered.local_oid, sizeof(tampered.local_oid), "%040d", 2);
    assert(stage(b, "conflict-demo", &tampered, "合并工作区") != 0);
    assert(publish(b, "conflict-demo", &tampered, "合并工作区") != 0);
    puts("PASS: non-Git backend, invalid inputs, and tampered draft workspace rejected");
}

static void test_draft_db_lifecycle(peer_t *b) {
    sync_git_draft_t d1 = {
        .workspace = "/tmp/ws-z",
        .local_hash = "hash-z",
        .remote_oid = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
        .local_oid = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
        .conflicts = "SKILL.md",
    };
    sync_git_draft_t d2 = {
        .workspace = "/tmp/ws-a",
        .local_hash = "hash-a",
        .remote_oid = "cccccccccccccccccccccccccccccccccccccccc",
        .local_oid = "dddddddddddddddddddddddddddddddddddddddd",
        .conflicts = "a.txt\nb.txt",
    };
    assert(sync_git_draft_save(b->db, b->scope, "z-skill", "", &d1) == 0);
    assert(sync_git_draft_save(b->db, b->scope, "a-skill", "", &d2) == 0);

    char first[128] = "";
    assert(sync_git_draft_first(b->db, b->scope, first) == 0);
    assert(strcmp(first, "a-skill") == 0);

    snprintf(d2.result_oid, sizeof(d2.result_oid), "%s", "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee");
    d2.conflicts[0] = '\0';
    assert(sync_git_draft_save(b->db, b->scope, "a-skill", "/tmp/ws-a", &d2) == 0);

    sync_git_draft_t loaded = {0};
    assert(sync_git_draft_load(b->db, b->scope, "a-skill", &loaded) == 0);
    assert(strcmp(loaded.result_oid, d2.result_oid) == 0 && !loaded.conflicts[0]);

    sync_git_draft_t empty = {0};
    assert(sync_git_draft_save(b->db, b->scope, "a-skill", "/tmp/ws-a", &empty) == 0);
    assert(sync_git_draft_load(b->db, b->scope, "a-skill", &loaded) == 0 && !loaded.workspace[0]);
    assert(sync_git_draft_first(b->db, b->scope, first) == 0 && strcmp(first, "z-skill") == 0);

    assert(sync_git_draft_save(b->db, b->scope, "z-skill", "/tmp/ws-z", &empty) == 0);
    assert(sync_git_draft_first(b->db, b->scope, first) == 0 && !first[0]);
    puts("PASS: draft DB save, update, alphabetical first, and delete");
}

/* 使用可丢弃的 Git 远端和测试目录，测试会向 main 写入合并提交。 */
int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s GIT_URL FIXTURE_DIRECTORY\n", argv[0]);
        return 2;
    }
    sync_config_t cfg = {.type = SYNC_BACKEND_GIT};
    snprintf(cfg.git_repo_url, sizeof(cfg.git_repo_url), "%s", argv[1]);
    peer_t a = {0}, b = {0};
    init_peer(&a, cfg, argv[2], "peer-a");
    init_peer(&b, cfg, argv[2], "peer-b");

    const char *skill = "conflict-demo";
    write_skill_file(&a, skill, "SKILL.md", "original skill\n");
    write_skill_file(&a, skill, "a.txt", "a0\n");
    write_skill_file(&a, skill, "b.txt", "b0\n");
    seed_skill(&a, &b, skill);
    assert(b.baseline.count == 1 && strlen(b.baseline.items[0].revision) == 40);

    /* 1. 无重叠修改：自动合并、草稿持久化、本地防覆盖保护、快速发布 */
    write_skill_file(&a, skill, "a.txt", "a1\n");
    write_skill_file(&b, skill, "b.txt", "b1\n");
    push_skill(&a, skill);
    sync_drift_item_t item = check_skill(&b, skill, SYNC_DRIFT_CONFLICT);
    sync_git_draft_t draft = {0};
    assert(prepare(&b, &item, &draft, NULL) == 0 && draft.workspace[0] && !draft.conflicts[0]);
    check_skill(&b, skill, SYNC_DRIFT_CONFLICT);
    verify_skill_file(&b, skill, "a.txt", "a0\n");
    assert(sync_git_draft_save(b.db, b.scope, skill, "", &draft) == 0);
    char workspace[SYNC_PATH_CAP];
    snprintf(workspace, sizeof(workspace), "%s", draft.workspace);
    sync_git_draft_t restored = {0};
    assert(sync_git_draft_load(b.db, b.scope, skill, &restored) == 0);
    assert(!strcmp(workspace, restored.workspace) && !strcmp(restored.local_oid, draft.local_oid));
    write_skill_file(&b, skill, "b.txt", "new work\n");
    assert(publish(&b, skill, &restored, "本地主仓有新改动") != 0);
    verify_skill_file(&b, skill, "b.txt", "new work\n");
    write_skill_file(&b, skill, "b.txt", "b1\n");
    assert(publish(&b, skill, &restored, NULL) == 0 && !restored.workspace[0]);
    assert(sync_git_draft_save(b.db, b.scope, skill, workspace, &restored) == 0);
    assert(AI_ACCESS(workspace, F_OK) != 0);
    check_skill(&b, skill, SYNC_DRIFT_IN_SYNC);
    pull_skill(&a, skill);
    verify_skill_file(&a, skill, "a.txt", "a1\n");
    verify_skill_file(&a, skill, "b.txt", "b1\n");
    puts("PASS: review, persistent draft, stale local protection, fast-forward merge preserving both edits");

    /* 2. 同行冲突 + 历史基线回溯 + 冲突标记拦截 + 手动暂存发布 */
    write_skill_file(&a, skill, "SKILL.md", "A same line\n");
    write_skill_file(&b, skill, "SKILL.md", "B same line\n");
    push_skill(&a, skill);
    item = check_skill(&b, skill, SYNC_DRIFT_CONFLICT);
    b.baseline.items[0].revision[0] = '\0';
    draft = (sync_git_draft_t){0};
    assert(prepare(&b, &item, &draft, NULL) == 0 && strstr(draft.conflicts, "SKILL.md"));
    assert(publish(&b, skill, &draft, "未暂存为已解决") != 0);
    assert(stage(&b, skill, &draft, "冲突标记") != 0);
    char d[SYNC_PATH_CAP], doc[SYNC_PATH_CAP];
    path(d, draft.workspace, skill);
    path(doc, d, "SKILL.md");
    write_text(doc, "A and B resolution\n");
    assert(stage(&b, skill, &draft, NULL) == 0);
    assert(!draft.conflicts[0]);
    assert(publish(&b, skill, &draft, NULL) == 0);
    check_skill(&b, skill, SYNC_DRIFT_IN_SYNC);
    pull_skill(&a, skill);
    verify_skill_file(&a, skill, "SKILL.md", "A and B resolution\n");
    puts("PASS: historical baseline recovery, conflict list, markers rejected, manual stage and merge");

    /* 3. 远端推进后拒绝发布草稿 */
    write_skill_file(&a, skill, "a.txt", "a2\n");
    write_skill_file(&b, skill, "b.txt", "b2\n");
    push_skill(&a, skill);
    item = check_skill(&b, skill, SYNC_DRIFT_CONFLICT);
    draft = (sync_git_draft_t){0};
    assert(prepare(&b, &item, &draft, NULL) == 0);
    write_skill_file(&a, skill, "a.txt", "a3\n");
    push_skill(&a, skill);
    assert(publish(&b, skill, &draft, "远端已更新") != 0 && draft.workspace[0]);
    verify_skill_file(&b, skill, "a.txt", "a1\n");
    verify_skill_file(&b, skill, "b.txt", "b2\n");
    puts("PASS: changed remote refuses publication; draft and hub remain available");

    /* 4. 校验非 Git 后端、非法参数、草稿数据库生命周期 */
    test_non_git_and_validation(&b, argv[2]);
    test_draft_db_lifecycle(&b);

    /* 5. 缺少共同基线时拒绝合并 */
    const char *no_base = "no-base-skill";
    write_skill_file(&a, no_base, "SKILL.md", "A independent\n");
    write_skill_file(&b, no_base, "SKILL.md", "B independent\n");
    item = check_skill(&a, no_base, SYNC_DRIFT_LOCAL_ONLY);
    assert(transfer_skill(&a, no_base, &item, false, NULL) == 0);
    item = check_skill(&b, no_base, SYNC_DRIFT_CONFLICT);
    draft = (sync_git_draft_t){0};
    assert(prepare(&b, &item, &draft, "没有已确认的共同版本") != 0 && !draft.workspace[0]);
    puts("PASS: missing baseline refuses three-way merge");

    /* 6. prepare 前本地内容发生变化应拒绝准备 */
    const char *stale_skill = "stale-check";
    write_skill_file(&a, stale_skill, "SKILL.md", "stale v0\n");
    seed_skill(&a, &b, stale_skill);
    write_skill_file(&a, stale_skill, "SKILL.md", "stale remote\n");
    write_skill_file(&b, stale_skill, "SKILL.md", "stale local\n");
    push_skill(&a, stale_skill);
    item = check_skill(&b, stale_skill, SYNC_DRIFT_CONFLICT);
    write_skill_file(&b, stale_skill, "SKILL.md", "stale local edited again\n");
    draft = (sync_git_draft_t){0};
    assert(prepare(&b, &item, &draft, "技能内容已变化") != 0 && !draft.workspace[0]);
    puts("PASS: stale drift snapshot before prepare rejected");

    /* 7. 多文件冲突与部分解决校验 */
    const char *multi = "multi-conflict";
    write_skill_file(&a, multi, "SKILL.md", "multi skill 0\n");
    write_skill_file(&a, multi, "extra.txt", "extra 0\n");
    seed_skill(&a, &b, multi);
    write_skill_file(&a, multi, "SKILL.md", "multi skill A\n");
    write_skill_file(&a, multi, "extra.txt", "extra A\n");
    write_skill_file(&b, multi, "SKILL.md", "multi skill B\n");
    write_skill_file(&b, multi, "extra.txt", "extra B\n");
    push_skill(&a, multi);
    item = check_skill(&b, multi, SYNC_DRIFT_CONFLICT);
    draft = (sync_git_draft_t){0};
    assert(prepare(&b, &item, &draft, NULL) == 0);
    assert(strstr(draft.conflicts, "SKILL.md") && strstr(draft.conflicts, "extra.txt"));
    path(d, draft.workspace, multi);
    path(doc, d, "SKILL.md");
    write_text(doc, "multi skill AB\n");
    /* extra.txt 仍含冲突标记，暂存必须失败 */
    assert(stage(&b, multi, &draft, "冲突标记") != 0);
    char extra_doc[SYNC_PATH_CAP];
    path(extra_doc, d, "extra.txt");
    write_text(extra_doc, "extra AB\n");
    assert(stage(&b, multi, &draft, NULL) == 0 && !draft.conflicts[0]);
    assert(publish(&b, multi, &draft, NULL) == 0);
    check_skill(&b, multi, SYNC_DRIFT_IN_SYNC);
    pull_skill(&a, multi);
    verify_skill_file(&a, multi, "SKILL.md", "multi skill AB\n");
    verify_skill_file(&a, multi, "extra.txt", "extra AB\n");
    puts("PASS: multi-file conflict partial resolution rejected and full resolution merged");

    /* 8. 合并结果完整性检查：删除 SKILL.md、未暂存/未跟踪文件拦截 */
    const char *integrity = "integrity-skill";
    write_skill_file(&a, integrity, "SKILL.md", "integrity 0\n");
    seed_skill(&a, &b, integrity);
    write_skill_file(&a, integrity, "SKILL.md", "integrity A\n");
    write_skill_file(&b, integrity, "SKILL.md", "integrity B\n");
    push_skill(&a, integrity);
    item = check_skill(&b, integrity, SYNC_DRIFT_CONFLICT);
    draft = (sync_git_draft_t){0};
    assert(prepare(&b, &item, &draft, NULL) == 0);
    path(d, draft.workspace, integrity);
    path(doc, d, "SKILL.md");
    assert(remove(doc) == 0);
    assert(stage(&b, integrity, &draft, NULL) == 0);
    assert(publish(&b, integrity, &draft, "保留有效的 SKILL.md") != 0);
    write_text(doc, "integrity resolved\n");
    assert(stage(&b, integrity, &draft, NULL) == 0);
    char stray[SYNC_PATH_CAP];
    path(stray, d, "stray.txt");
    write_text(stray, "untracked\n");
    assert(publish(&b, integrity, &draft, "未暂存的改动") != 0);
    assert(remove(stray) == 0);
    assert(publish(&b, integrity, &draft, NULL) == 0);
    check_skill(&b, integrity, SYNC_DRIFT_IN_SYNC);
    pull_skill(&a, integrity);
    verify_skill_file(&a, integrity, "SKILL.md", "integrity resolved\n");
    puts("PASS: missing SKILL.md and unstaged files in merge result rejected");

    /* 9. 一侧删文件、另一侧改其他文件的无冲突合并 */
    const char *del_skill = "delete-merge";
    write_skill_file(&a, del_skill, "SKILL.md", "delete demo\n");
    write_skill_file(&a, del_skill, "keep.txt", "keep 0\n");
    write_skill_file(&a, del_skill, "drop.txt", "drop 0\n");
    seed_skill(&a, &b, del_skill);
    remove_skill_file(&a, del_skill, "drop.txt");
    write_skill_file(&b, del_skill, "keep.txt", "keep B\n");
    push_skill(&a, del_skill);
    item = check_skill(&b, del_skill, SYNC_DRIFT_CONFLICT);
    draft = (sync_git_draft_t){0};
    assert(prepare(&b, &item, &draft, NULL) == 0 && !draft.conflicts[0]);
    assert(publish(&b, del_skill, &draft, NULL) == 0);
    check_skill(&b, del_skill, SYNC_DRIFT_IN_SYNC);
    assert(!skill_file_exists(&b, del_skill, "drop.txt"));
    verify_skill_file(&b, del_skill, "keep.txt", "keep B\n");
    pull_skill(&a, del_skill);
    assert(!skill_file_exists(&a, del_skill, "drop.txt"));
    verify_skill_file(&a, del_skill, "keep.txt", "keep B\n");
    puts("PASS: remote file deletion + local edit merged cleanly");

    destroy_peer(&a);
    destroy_peer(&b);
    return 0;
}
