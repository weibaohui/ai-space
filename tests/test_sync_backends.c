#include "sync_directory.h"
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
    assert(argc == 5 || argc == 6);
    if (strcmp(argv[1], "shared") == 0) {
        cfg->type = SYNC_BACKEND_NFS;
        snprintf(cfg->nfs_mount_path, sizeof(cfg->nfs_mount_path), "%s", argv[2]);
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

/* Supply an isolated remote directory and two empty, disposable local roots. */
int main(int argc, char **argv) {
    if (argc < 5 || argc > 6) {
        fprintf(stderr, "Usage: %s shared REMOTE_ROOT LOCAL_A LOCAL_B\n", argv[0]);
        fprintf(stderr, "       %s webdav BASE_URL REMOTE_SUBDIR LOCAL_A LOCAL_B\n", argv[0]);
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

    check_drift(backend, a, 0, 0, 1, 0, 0);
    assert(backend->ops->push_skill(backend, name, a, progress, NULL) == 0);
    check_drift(backend, b, 0, 1, 0, 0, 0);
    assert(backend->ops->pull_all(backend, b, progress, NULL) == 0);
    check_drift(backend, b, 1, 0, 0, 0, 0);
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
    sync_backend_destroy(backend);
    puts("PASS: read/write, drift, single/batch transfer, nested names, deletion, convergence");
    return 0;
}
