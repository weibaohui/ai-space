#include "db.h"
#include "sync_baseline.h"
#include "sync_directory.h"
#include "portability.h"
#include <assert.h>
#include <string.h>

static const char *skill = "conflict-demo";

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

static void write_file(peer_t *p, const char *name, const char *text) {
    char dir[SYNC_PATH_CAP], file[SYNC_PATH_CAP];
    path(dir, p->root, skill);
    ai_mkdir_p(dir);
    path(file, dir, name);
    FILE *f = fopen(file, "wb");
    assert(f && fputs(text, f) >= 0 && fclose(f) == 0);
}

static void verify_file(peer_t *p, const char *name, const char *text) {
    char dir[SYNC_PATH_CAP], file[SYNC_PATH_CAP], body[128];
    path(dir, p->root, skill);
    path(file, dir, name);
    FILE *f = fopen(file, "rb");
    assert(f && fgets(body, sizeof(body), f) && strcmp(body, text) == 0);
    fclose(f);
}

static sync_drift_item_t check(peer_t *p, sync_drift_status_t expected) {
    sync_drift_summary_t drift = {0};
    char error[256] = "";
    if (p->backend->ops->check_drift(p->backend, p->root, &drift, error, sizeof(error)) != 0) {
        fprintf(stderr, "check: %s\n", error);
        abort();
    }
    assert(drift.count == 1);
    sync_drift_item_t item = drift.items[0];
    if (item.status != expected) fprintf(stderr, "status=%d expected=%d (%s)\n", item.status, expected, item.status_desc);
    assert(item.status == expected);
    assert(sync_baseline_record(p->db, p->scope, &drift) == 0);
    sync_free_drift_summary(&drift);
    free(p->baseline.items);
    assert(sync_baseline_load(p->db, p->scope, &p->baseline) == 0);
    return item;
}

static int transfer(peer_t *p, const sync_drift_item_t *item, bool pull, sync_progress_cb cb) {
    p->backend->expected_skill = item;
    int rc = pull ? p->backend->ops->pull_skill(p->backend, skill, p->root, cb, NULL)
                  : p->backend->ops->push_skill(p->backend, skill, p->root, cb, NULL);
    p->backend->expected_skill = NULL;
    return rc;
}

static int prepare(peer_t *p, sync_drift_item_t *item, sync_git_draft_t *draft) {
    char error[256] = "";
    int rc = sync_git_merge_prepare(p->backend, p->root, item, draft, error, sizeof(error));
    if (rc != 0) fprintf(stderr, "prepare: %s\n", error);
    return rc;
}

static int publish(peer_t *p, sync_git_draft_t *draft, const char *rejection) {
    char error[256] = "";
    sync_baseline_item_t confirmed;
    int rc = sync_git_merge_publish(p->backend, p->root, skill, draft, &confirmed, error, sizeof(error));
    if (rejection) assert(rc != 0 && strstr(error, rejection));
    else if (rc != 0) fprintf(stderr, "publish: %s\n", error);
    if (rc == 0) assert(!strcmp(confirmed.skill, skill) && confirmed.revision[0] && confirmed.hash[0]);
    return rc;
}

static void push(peer_t *p) {
    sync_drift_item_t item = check(p, SYNC_DRIFT_LOCAL_AHEAD);
    assert(transfer(p, &item, false, NULL) == 0);
    check(p, SYNC_DRIFT_IN_SYNC);
}

static void pull(peer_t *p) {
    sync_drift_item_t item = check(p, SYNC_DRIFT_REMOTE_AHEAD);
    assert(transfer(p, &item, true, NULL) == 0);
    check(p, SYNC_DRIFT_IN_SYNC);
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
    write_file(&a, "SKILL.md", "original skill\n");
    write_file(&a, "a.txt", "a0\n");
    write_file(&a, "b.txt", "b0\n");
    sync_drift_item_t item = check(&a, SYNC_DRIFT_LOCAL_ONLY);
    assert(transfer(&a, &item, false, NULL) == 0);
    check(&a, SYNC_DRIFT_IN_SYNC);
    item = check(&b, SYNC_DRIFT_REMOTE_ONLY);
    assert(transfer(&b, &item, true, NULL) == 0);
    check(&b, SYNC_DRIFT_IN_SYNC);
    assert(b.baseline.count == 1 && strlen(b.baseline.items[0].revision) == 40);

    write_file(&a, "a.txt", "a1\n");
    write_file(&b, "b.txt", "b1\n");
    push(&a);
    item = check(&b, SYNC_DRIFT_CONFLICT);
    sync_git_draft_t draft = {0};
    assert(prepare(&b, &item, &draft) == 0 && draft.workspace[0] && !draft.conflicts[0]);
    check(&b, SYNC_DRIFT_CONFLICT);
    verify_file(&b, "a.txt", "a0\n");
    assert(sync_git_draft_save(b.db, b.scope, skill, "", &draft) == 0);
    char workspace[SYNC_PATH_CAP];
    snprintf(workspace, sizeof(workspace), "%s", draft.workspace);
    sync_git_draft_t restored = {0};
    assert(sync_git_draft_load(b.db, b.scope, skill, &restored) == 0);
    assert(!strcmp(workspace, restored.workspace) && !strcmp(restored.local_oid, draft.local_oid));
    write_file(&b, "b.txt", "new work\n");
    assert(publish(&b, &restored, "本地主仓有新改动") != 0);
    verify_file(&b, "b.txt", "new work\n");
    write_file(&b, "b.txt", "b1\n");
    assert(publish(&b, &restored, NULL) == 0 && !restored.workspace[0]);
    assert(sync_git_draft_save(b.db, b.scope, skill, workspace, &restored) == 0);
    assert(AI_ACCESS(workspace, F_OK) != 0);
    check(&b, SYNC_DRIFT_IN_SYNC);
    pull(&a);
    verify_file(&a, "a.txt", "a1\n");
    verify_file(&a, "b.txt", "b1\n");
    puts("PASS: review, persistent draft, stale local protection, fast-forward merge preserving both edits");

    write_file(&a, "SKILL.md", "A same line\n");
    write_file(&b, "SKILL.md", "B same line\n");
    push(&a);
    item = check(&b, SYNC_DRIFT_CONFLICT);
    b.baseline.items[0].revision[0] = '\0';
    draft = (sync_git_draft_t){0};
    assert(prepare(&b, &item, &draft) == 0 && strstr(draft.conflicts, "SKILL.md"));
    assert(publish(&b, &draft, "未暂存为已解决") != 0);
    char stage_error[256] = "";
    assert(sync_git_merge_stage(b.backend, skill, &draft, stage_error, sizeof(stage_error)) != 0);
    assert(strstr(stage_error, "冲突标记"));
    char d[SYNC_PATH_CAP], doc[SYNC_PATH_CAP];
    path(d, draft.workspace, skill);
    path(doc, d, "SKILL.md");
    FILE *f = fopen(doc, "wb");
    assert(f && fputs("A and B resolution\n", f) >= 0 && fclose(f) == 0);
    assert(sync_git_merge_stage(b.backend, skill, &draft, stage_error, sizeof(stage_error)) == 0);
    assert(!draft.conflicts[0]);
    assert(publish(&b, &draft, NULL) == 0);
    check(&b, SYNC_DRIFT_IN_SYNC);
    pull(&a);
    verify_file(&a, "SKILL.md", "A and B resolution\n");
    puts("PASS: historical baseline recovery, conflict list, markers rejected, manual stage and merge");

    write_file(&a, "a.txt", "a2\n");
    write_file(&b, "b.txt", "b2\n");
    push(&a);
    item = check(&b, SYNC_DRIFT_CONFLICT);
    assert(prepare(&b, &item, &draft) == 0);
    write_file(&a, "a.txt", "a3\n");
    push(&a);
    assert(publish(&b, &draft, "远端已更新") != 0 && draft.workspace[0]);
    verify_file(&b, "a.txt", "a1\n");
    verify_file(&b, "b.txt", "b2\n");
    puts("PASS: changed remote refuses publication; draft and hub remain available");

    sync_backend_destroy(a.backend);
    sync_backend_destroy(b.backend);
    free(a.baseline.items);
    free(b.baseline.items);
    db_close(a.db);
    db_close(b.db);
}
