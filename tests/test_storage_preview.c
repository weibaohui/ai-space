#include "app_state.h"
#include "file_preview.h"
#include "file_tree.h"
#include "portability.h"
#include "storage_manager.h"
#include "db.h"

#include <assert.h>
#include <string.h>

static void write_fixture(const char *path, int lines) {
    FILE *file = fopen(path, "wb");
    assert(file);
    for (int i = 0; i < lines; i++) assert(fputs("line\n", file) >= 0);
    assert(fclose(file) == 0);
}

static void check_legacy_migration(const char *root) {
    char path[SYNC_PATH_CAP];
    assert(sync_join(path, sizeof(path), root, "legacy.db") == 0);
    sqlite3 *db = NULL;
    assert(sqlite3_open(path, &db) == SQLITE_OK);
    const char *sql = "CREATE TABLE local_tools (id INTEGER PRIMARY KEY, name TEXT, path TEXT,"
                      "skill_count INTEGER, is_deleted INTEGER);"
                      "INSERT INTO local_tools VALUES (7,'custom','/skills',3,0),(8,'removed','/old',0,1);"
                      "CREATE TABLE sync_baselines (scope TEXT NOT NULL, skill TEXT NOT NULL, hash TEXT NOT NULL,"
                      "PRIMARY KEY(scope,skill));"
                      "INSERT INTO sync_baselines VALUES ('old-scope','existing-skill','80ff3fcf57bfcbc8');";
    assert(sqlite3_exec(db, sql, NULL, NULL, NULL) == SQLITE_OK);
    db_close(db);
    for (int i = 0; i < 2; i++) {
        assert(db_init(path, &db) == 0);
        tool_record_t *tools = NULL;
        int count = 0;
        assert(db_query_tools(db, &tools, &count, NULL, 0, false) == 0);
        assert(count == 1 && tools[0].id == 7 && tools[0].skill_count == 3);
        assert(strcmp(tools[0].name, "custom") == 0 && strcmp(tools[0].path, "/skills") == 0);
        free(tools);
        sqlite3_stmt *baseline = NULL;
        assert(sqlite3_prepare_v2(db, "SELECT hash,git_revision FROM sync_baselines WHERE skill='existing-skill'",
                                  -1, &baseline, NULL) == SQLITE_OK);
        assert(sqlite3_step(baseline) == SQLITE_ROW);
        assert(strcmp((const char *)sqlite3_column_text(baseline, 0), "80ff3fcf57bfcbc8") == 0);
        assert(sqlite3_column_bytes(baseline, 1) == 0);
        sqlite3_finalize(baseline);
        db_close(db);
    }
}

static void check_directory_cache(const char *backup, const char *empty) {
    static app_state_t state;
    const app_storage_path_t slot = STORAGE_PATH_BACKUP;
    app_storage_begin_frame(&state);
    assert(app_storage_directory(&state, slot, backup)->subdirs == 3);

    char changed[SYNC_PATH_CAP];
    assert(sync_join(changed, sizeof(changed), backup, "new-snapshot") == 0);
    assert(AI_MKDIR(changed) == 0);
    app_storage_begin_frame(&state);
    assert(app_storage_directory(&state, slot, backup)->subdirs == 3);

    app_storage_invalidate(&state);
    assert(app_storage_directory(&state, slot, backup)->subdirs == 4);
    assert(app_storage_directory(&state, slot, empty)->subdirs == 0);
    assert(app_storage_directory(&state, slot, backup)->subdirs == 4);

    assert(sync_remove_tree(changed) == 0);
    app_storage_begin_frame(&state);
    app_storage_begin_frame(&state);
    assert(app_storage_directory(&state, slot, backup)->subdirs == 3);
}

int main(void) {
    char root[] = "/tmp/ai-space-storage-preview-XXXXXX";
    assert(mkdtemp(root));
    check_legacy_migration(root);
    char backup[SYNC_PATH_CAP], cache[SYNC_PATH_CAP], hub[SYNC_PATH_CAP];
    assert(sync_join(backup, sizeof(backup), root, "backup") == 0);
    assert(sync_join(cache, sizeof(cache), root, "cache") == 0);
    assert(sync_join(hub, sizeof(hub), root, "hub") == 0);
    assert(AI_MKDIR(backup) == 0 && AI_MKDIR(cache) == 0 && AI_MKDIR(hub) == 0);

    for (int i = 0; i < 132; i++) {
        char name[32], snapshot[SYNC_PATH_CAP];
        snprintf(name, sizeof(name), "snapshot-%03d", i);
        assert(sync_join(snapshot, sizeof(snapshot), backup, name) == 0);
        assert(AI_MKDIR(snapshot) == 0);
        assert(sync_set_mtime(snapshot, 1700000000 + i) == 0);
    }

    storage_stats_t stats;
    storage_collect(hub, backup, cache, &stats);
    assert(stats.snapshot_total == 132 && stats.snapshot_count == 128);
    assert(strcmp(stats.snapshots[0].dir_name, "snapshot-131") == 0);
    assert(storage_prune_snapshots(backup, 3) == 0);
    storage_collect(hub, backup, cache, &stats);
    assert(stats.snapshot_total == 3 && strcmp(stats.snapshots[2].dir_name, "snapshot-129") == 0);
    assert(storage_delete_snapshot(backup, "../hub") != 0);
    assert(AI_ACCESS(hub, F_OK) == 0);
    check_directory_cache(backup, cache);

    char file[SYNC_PATH_CAP], link[SYNC_PATH_CAP];
    assert(sync_join(file, sizeof(file), hub, "preview.txt") == 0);
    assert(sync_join(link, sizeof(link), cache, "external") == 0);
    write_fixture(file, 300000);
    assert(symlink(hub, link) == 0);
    assert(storage_clear_directory(link) != 0);
    assert(storage_clear_directory(cache) == 0);
    assert(AI_ACCESS(file, F_OK) == 0);

    size_t bytes_read = 0, file_size = 0;
    int lines = 0;
    bool binary = true;
    char *preview = read_file_preview(file, 20, &bytes_read, &file_size, &lines, &binary);
    assert(preview && bytes_read == 20 && file_size == 1500000 && lines == 5 && !binary);
    assert(strcmp(preview, "line\nline\nline\nline\n") == 0);
    free(preview);

    assert(sync_remove_tree(root) == 0);
    puts("PASS: snapshot enumeration and pruning, safe cleanup, directory cache, bounded preview");
    return 0;
}
