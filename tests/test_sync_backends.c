#include "sync_directory.h"
#include "sync_baseline.h"
#include "db.h"
#include "portability.h"

#include <assert.h>
#include <string.h>
#include <sys/stat.h>

static void path_join(char *out, const char *parent, const char *name) {
    assert(sync_join(out, SYNC_PATH_CAP, parent, name) == 0);
}

static void write_file(const char *path, const char *content, time_t modified) {
    FILE *out = fopen(path, "wb");
    assert(out);
    assert(fputs(content, out) >= 0);
    assert(fclose(out) == 0);
    assert(sync_set_mtime(path, modified) == 0);
}

static void config_from_args(sync_config_t *cfg, int argc, char **argv) {
    assert(argc >= 5 && argc <= 7);
    if (strcmp(argv[1], "shared") == 0) {
        cfg->type = SYNC_BACKEND_NFS;
        snprintf(cfg->nfs_mount_path, sizeof(cfg->nfs_mount_path), "%s", argv[2]);
    } else if (strcmp(argv[1], "nfs") == 0 || strcmp(argv[1], "smb") == 0) {
        assert(argc == 7);
        cfg->type = SYNC_BACKEND_NFS;
        cfg->share_mode = strcmp(argv[1], "nfs") == 0 ? SYNC_SHARE_NFS : SYNC_SHARE_SMB;
        snprintf(cfg->share_server, sizeof(cfg->share_server), "%s", argv[2]);
        snprintf(cfg->share_export, sizeof(cfg->share_export), "%s", argv[3]);
        snprintf(cfg->share_subdir, sizeof(cfg->share_subdir), "%s", argv[4]);
        const char *user = getenv("AI_SPACE_TEST_SMB_USER");
        const char *pass = getenv("AI_SPACE_TEST_SMB_PASSWORD");
        if (user) snprintf(cfg->smb_user, sizeof(cfg->smb_user), "%s", user);
        if (pass) snprintf(cfg->smb_pass, sizeof(cfg->smb_pass), "%s", pass);
    } else {
        assert(argc == 6 && strcmp(argv[1], "webdav") == 0);
        cfg->type = SYNC_BACKEND_WEBDAV;
        snprintf(cfg->webdav_url, sizeof(cfg->webdav_url), "%s", argv[2]);
        snprintf(cfg->webdav_remote_path, sizeof(cfg->webdav_remote_path), "%s", argv[3]);
        const char *user = getenv("AI_SPACE_TEST_WEBDAV_USER");
        const char *password = getenv("AI_SPACE_TEST_WEBDAV_PASSWORD");
        if (user) snprintf(cfg->webdav_user, sizeof(cfg->webdav_user), "%s", user);
        if (password) snprintf(cfg->webdav_pass, sizeof(cfg->webdav_pass), "%s", password);
    }
}

static void progress(float percent, const char *msg, void *user) {
    (void)percent;
    (void)user;
    puts(msg);
}

static void check_drift(sync_backend_t *backend, const char *local, int expected_in_sync,
                        int remote_only, int local_only, int remote_ahead, int local_ahead) {
    sync_drift_summary_t drift = {0};
    char error[256] = "";
    int rc = backend->ops->check_drift(backend, local, &drift, error, sizeof error);
    if (rc != 0) fprintf(stderr, "check_drift: %s\n", error);
    assert(rc == 0);
    assert(drift.in_sync_count == expected_in_sync && drift.remote_only_count == remote_only &&
           drift.local_only_count == local_only && drift.remote_ahead_count == remote_ahead &&
           drift.local_ahead_count == local_ahead && drift.conflict_count == 0);
    sync_free_drift_summary(&drift);
}

static void check_conflict_guards(sync_backend_t *backend, const char *a, const char *b,
                                  const char *name, time_t old) {
    sqlite3 *db = NULL;
    assert(db_init(":memory:", &db) == 0);
    char scope[SYNC_SCOPE_CAP];
    assert(sync_baseline_scope(&backend->cfg, a, scope, sizeof(scope)) == 0);
    sync_baseline_t baseline = {0};
    backend->baseline = &baseline;
    sync_drift_summary_t drift = {0};
    assert(backend->ops->check_drift(backend, a, &drift, NULL, 0) == 0);
    assert(drift.in_sync_count == 1);
    assert(sync_baseline_record(db, scope, &drift) == 0);
    sync_free_drift_summary(&drift);
    assert(sync_baseline_load(db, scope, &baseline) == 0 && baseline.count == 1);

    char skill[SYNC_PATH_CAP], a_doc[SYNC_PATH_CAP], b_doc[SYNC_PATH_CAP];
    path_join(skill, a, name);
    path_join(a_doc, skill, "SKILL.md");
    path_join(skill, b, name);
    path_join(b_doc, skill, "SKILL.md");
    write_file(a_doc, "independent A edit\n", old + 10);
    check_drift(backend, a, 0, 0, 0, 0, 1);
    write_file(b_doc, "independent B edit\n", old + 20);
    assert(backend->ops->push_skill(backend, name, b, NULL, NULL) == 0);
    assert(backend->ops->transfer_batch(backend, a, false, NULL, NULL, &drift) == SYNC_BATCH_UNCHANGED);
    assert(drift.conflict_count == 1 && drift.count == 1);
    assert(backend->ops->pull_all(backend, a, NULL, NULL) == 0);

    write_file(b_doc, "remote changed after conflict was shown\n", old + 30);
    assert(backend->ops->push_skill(backend, name, b, NULL, NULL) == 0);
    backend->expected_skill = &drift.items[0];
    assert(backend->ops->push_skill(backend, name, a, NULL, NULL) == SYNC_TRANSFER_CHANGED);
    backend->expected_skill = NULL;
    write_file(b_doc, "independent B edit\n", old + 20);
    assert(backend->ops->push_skill(backend, name, b, NULL, NULL) == 0);

    backend->expected_skill = &drift.items[0];
    write_file(a_doc, "changed after conflict was shown\n", old + 30);
    assert(backend->ops->pull_skill(backend, name, a, NULL, NULL) == SYNC_TRANSFER_CHANGED);
    write_file(a_doc, "independent A edit\n", old + 10);
    assert(backend->ops->pull_skill(backend, name, a, NULL, NULL) == 0);
    backend->expected_skill = NULL;
    sync_free_drift_summary(&drift);
    check_drift(backend, a, 1, 0, 0, 0, 0);
    backend->baseline = NULL;
    free(baseline.items);
    db_close(db);
}

/* Supply an isolated remote directory and two empty, disposable local roots. */
int main(int argc, char **argv) {
    if (argc < 5 || argc > 7) {
        fprintf(stderr, "Usage: %s shared REMOTE_ROOT LOCAL_A LOCAL_B\n", argv[0]);
        fprintf(stderr, "       %s webdav BASE_URL REMOTE_SUBDIR LOCAL_A LOCAL_B\n", argv[0]);
        fprintf(stderr, "       %s nfs|smb SERVER EXPORT_OR_SHARE REMOTE_SUBDIR LOCAL_A LOCAL_B\n", argv[0]);
        return 1;
    }
    sync_config_t cfg = {0};
    config_from_args(&cfg, argc, argv);
    const char *a = argv[argc - 2], *b = argv[argc - 1];
    char error[256] = "";
    int rc = sync_backend_check_available(&cfg, error, sizeof error);
    printf("availability: %s\n", error);
    if (rc != 0) return 2;

    sync_backend_t *backend = NULL;
    assert(sync_backend_create(&cfg, &backend) == 0);
    check_drift(backend, a, 0, 0, 0, 0, 0);
    check_drift(backend, b, 0, 0, 0, 0, 0);
    char skill_path[SYNC_PATH_CAP], doc[SYNC_PATH_CAP], refs[SYNC_PATH_CAP], extra[SYNC_PATH_CAP], empty[SYNC_PATH_CAP];
    const char *name = "example-skill";
    path_join(skill_path, a, name);
    assert(AI_MKDIR(skill_path) == 0);
    path_join(doc, skill_path, "SKILL.md");
    path_join(refs, skill_path, "references");
    assert(AI_MKDIR(refs) == 0);
    path_join(extra, refs, "a + b% & # 中文.txt");
    path_join(empty, refs, "empty");
    assert(AI_MKDIR(empty) == 0);
    time_t old = time(NULL) - 120;
    write_file(doc, "original\n", old);
    write_file(extra, "nested contents\n", old);
    assert(sync_set_mtime(skill_path, old) == 0);
    if (cfg.share_mode != SYNC_SHARE_MOUNTED) {
        char binary_path[SYNC_PATH_CAP];
        path_join(binary_path, refs, "binary.dat");
        FILE *binary = fopen(binary_path, "wb");
        assert(binary);
        for (unsigned i = 0; i < 170000; i++) assert(fputc(i % 251, binary) != EOF);
        assert(fclose(binary) == 0);
        assert(sync_set_mtime(binary_path, old) == 0);
    }
    assert(sync_set_mtime(empty, old) == 0);
    assert(sync_set_mtime(refs, old) == 0);

    check_drift(backend, a, 0, 0, 1, 0, 0);
    sync_drift_summary_t unchanged = {0};
    assert(backend->ops->transfer_batch(backend, a, true, NULL, NULL, &unchanged) == SYNC_BATCH_UNCHANGED);
    assert(unchanged.count == 1 && unchanged.local_only_count == 1 &&
           unchanged.items[0].status == SYNC_DRIFT_LOCAL_ONLY &&
           strcmp(unchanged.items[0].skill_name, name) == 0);
    sync_free_drift_summary(&unchanged);
    assert(backend->ops->push_skill(backend, name, a, progress, NULL) == 0);
    check_drift(backend, b, 0, 1, 0, 0, 0);
    assert(backend->ops->pull_all(backend, b, progress, NULL) == 0);
    check_drift(backend, b, 1, 0, 0, 0, 0);
    assert(backend->ops->push_all(backend, b, NULL, NULL) == 0);
    path_join(skill_path, b, name);
    path_join(refs, skill_path, "references");
    path_join(extra, refs, "a + b% & # 中文.txt");
    path_join(empty, refs, "empty");
    struct stat st;
    assert(stat(empty, &st) == 0 && S_ISDIR(st.st_mode));
    assert(remove(extra) == 0);
    path_join(doc, skill_path, "SKILL.md");
    write_file(doc, "updated on B\n", time(NULL) + 120);

    check_drift(backend, b, 0, 0, 0, 0, 1);
    assert(backend->ops->push_all(backend, b, progress, NULL) == 0);
    check_drift(backend, a, 0, 0, 0, 1, 0);
    assert(backend->ops->pull_skill(backend, name, a, progress, NULL) == 0);
    check_drift(backend, a, 1, 0, 0, 0, 0);
    check_drift(backend, b, 1, 0, 0, 0, 0);
    path_join(skill_path, a, name);
    path_join(refs, skill_path, "references");
    path_join(extra, refs, "a + b% & # 中文.txt");
    assert(stat(extra, &st) != 0);
    assert(backend->ops->pull_skill(backend, "missing-skill", a, NULL, NULL) != 0);
    assert(backend->ops->push_skill(backend, "missing-skill", a, NULL, NULL) != 0);
    check_conflict_guards(backend, a, b, name, old);
    sync_backend_destroy(backend);
    puts("PASS: read/write, drift, single/batch transfer, deletion, conflict guards, convergence");
    return 0;
}
