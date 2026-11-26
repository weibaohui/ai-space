#include "db.h"
#include "scanner.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "portability.h"

static void ensure_parent_dir(const char *path) {
    char tmp[512];
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    char *dir = ai_dirname(tmp);
    if (dir && strcmp(dir, ".") != 0 && strcmp(dir, "/") != 0 && strcmp(dir, "\\") != 0) {
        AI_MKDIR(dir);
    }
}

#define TOOLS_COLUMNS "(id INTEGER PRIMARY KEY AUTOINCREMENT," \
    "name TEXT NOT NULL UNIQUE, path TEXT NOT NULL, skill_count INTEGER NOT NULL DEFAULT 0," \
    "created_at TEXT NOT NULL DEFAULT (datetime('now', 'localtime'))," \
    "updated_at TEXT NOT NULL DEFAULT (datetime('now', 'localtime')))"

static int ensure_git_baseline_revision(sqlite3 *db) {
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, "SELECT git_revision FROM sync_baselines LIMIT 0", -1, &stmt, NULL);
    sqlite3_finalize(stmt);
    if (rc == SQLITE_OK) return 0;
    return sqlite3_exec(db, "ALTER TABLE sync_baselines ADD COLUMN git_revision TEXT NOT NULL DEFAULT ''",
                        NULL, NULL, NULL) == SQLITE_OK ? 0 : -1;
}

static int migrate_legacy_tools(sqlite3 *db) {
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, "PRAGMA table_info(local_tools)", -1, &stmt, NULL) != SQLITE_OK) return -1;
    bool legacy = false, count = false, created = false, updated = false;
    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char *name = (const char *)sqlite3_column_text(stmt, 1);
        if (!name) continue;
        if (strcmp(name, "is_deleted") == 0) legacy = true;
        else if (strcmp(name, "skill_count") == 0) count = true;
        else if (strcmp(name, "created_at") == 0) created = true;
        else if (strcmp(name, "updated_at") == 0) updated = true;
    }
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) return -1;
    if (!legacy) return 0;

    /* 旧版本可能缺少统计和时间字段，迁移时用当前表定义的默认值补齐。 */
    char migration[1400];
    snprintf(migration, sizeof(migration),
             "CREATE TABLE local_tools_migration " TOOLS_COLUMNS ";"
             "INSERT INTO local_tools_migration (id,name,path,skill_count,created_at,updated_at)"
             " SELECT id,name,path,%s,%s,%s FROM local_tools WHERE coalesce(is_deleted,0)=0;"
             "DROP TABLE local_tools;"
             "ALTER TABLE local_tools_migration RENAME TO local_tools;",
             count ? "coalesce(skill_count,0)" : "0",
             created ? "coalesce(created_at,datetime('now','localtime'))" : "datetime('now','localtime')",
             updated ? "coalesce(updated_at,datetime('now','localtime'))" : "datetime('now','localtime')");
    return sqlite3_exec(db, migration, NULL, NULL, NULL) == SQLITE_OK ? 0 : -1;
}

int db_init(const char *db_path, sqlite3 **out_db) {
    if (!db_path || !out_db) return -1;
    *out_db = NULL;

    ensure_parent_dir(db_path);

    sqlite3 *db = NULL;
    int rc = sqlite3_open(db_path, &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[db] Cannot open database %s: %s\n", db_path, sqlite3_errmsg(db));
        if (db) sqlite3_close(db);
        return -1;
    }

    sqlite3_busy_timeout(db, 2000);
    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);
    sqlite3_exec(db, "PRAGMA synchronous=NORMAL;", NULL, NULL, NULL);

    const char *schema =
        "CREATE TABLE IF NOT EXISTS local_tools " TOOLS_COLUMNS ";"
        "CREATE INDEX IF NOT EXISTS idx_tools_name ON local_tools(name);"
        "CREATE TABLE IF NOT EXISTS app_settings (key TEXT PRIMARY KEY, value TEXT NOT NULL);"
        "CREATE TABLE IF NOT EXISTS sync_baselines ("
        " scope TEXT NOT NULL, skill TEXT NOT NULL, hash TEXT NOT NULL,"
        " git_revision TEXT NOT NULL DEFAULT '', PRIMARY KEY(scope,skill));"
        "CREATE TABLE IF NOT EXISTS sync_git_drafts ("
        " scope TEXT NOT NULL, skill TEXT NOT NULL, workspace TEXT NOT NULL, local_hash TEXT NOT NULL,"
        " remote_oid TEXT NOT NULL, local_oid TEXT NOT NULL, result_oid TEXT NOT NULL, conflicts TEXT NOT NULL,"
        " PRIMARY KEY(scope,skill));";
    /* 迁移、建表及索引在同一事务完成，任一步失败都保留原表。 */
    if (sqlite3_exec(db, "BEGIN IMMEDIATE", NULL, NULL, NULL) != SQLITE_OK ||
        migrate_legacy_tools(db) != 0 ||
        sqlite3_exec(db, schema, NULL, NULL, NULL) != SQLITE_OK ||
        ensure_git_baseline_revision(db) != 0 ||
        sqlite3_exec(db, "COMMIT", NULL, NULL, NULL) != SQLITE_OK) {
        fprintf(stderr, "[db] Failed to initialize schema: %s\n", sqlite3_errmsg(db));
        sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
        sqlite3_close(db);
        return -1;
    }

    *out_db = db;
    return 0;
}

void db_close(sqlite3 *db) {
    if (db) {
        sqlite3_close(db);
    }
}

typedef struct {
    int id;
    char name[128];
    bool seen;
} db_tool_entry_t;

int db_scan_and_sync(sqlite3 *db, int *out_inserted, int *out_updated, int *out_deleted) {
    if (!db) return -1;

    int inserted = 0;
    int updated = 0;
    int deleted = 0;

    /* 先完成磁盘发现，再开始写入数据库。 */
    discovered_tool_t *disc_tools = NULL;
    int disc_count = 0;
    if (scanner_scan(&disc_tools, &disc_count) != 0) {
        return -1;
    }

    /* 同一轮发现结果用一个事务落库。 */
    char *errmsg = NULL;
    if (sqlite3_exec(db, "BEGIN IMMEDIATE TRANSACTION;", NULL, NULL, &errmsg) != SQLITE_OK) {
        fprintf(stderr, "[db] Transaction begin failed: %s\n", errmsg ? errmsg : "");
        sqlite3_free(errmsg);
        free(disc_tools);
        return -1;
    }

    /* 加载旧记录以区分更新、插入和移除。 */
    sqlite3_stmt *stmt = NULL;
    const char *sel_sql = "SELECT id, name FROM local_tools;";
    if (sqlite3_prepare_v2(db, sel_sql, -1, &stmt, NULL) != SQLITE_OK) {
        sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
        free(disc_tools);
        return -1;
    }

    int db_cap = 64;
    int db_count = 0;
    db_tool_entry_t *db_entries = (db_tool_entry_t *)malloc(sizeof(db_tool_entry_t) * db_cap);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (db_count >= db_cap) {
            db_cap *= 2;
            db_tool_entry_t *re = (db_tool_entry_t *)realloc(db_entries, sizeof(db_tool_entry_t) * db_cap);
            if (!re) break;
            db_entries = re;
        }
        db_entries[db_count].id = sqlite3_column_int(stmt, 0);
        const unsigned char *name_txt = sqlite3_column_text(stmt, 1);
        strncpy(db_entries[db_count].name, name_txt ? (const char *)name_txt : "", sizeof(db_entries[db_count].name) - 1);
        db_entries[db_count].name[sizeof(db_entries[db_count].name) - 1] = '\0';
        db_entries[db_count].seen = false;
        db_count++;
    }
    sqlite3_finalize(stmt);

    /* 循环复用写入语句，避免重复编译 SQL。 */
    sqlite3_stmt *stmt_update = NULL;
    const char *upd_sql =
        "UPDATE local_tools SET "
        "  path = ?, "
        "  skill_count = ?, "
        "  updated_at = datetime('now', 'localtime') "
        "WHERE id = ?;";
    sqlite3_prepare_v2(db, upd_sql, -1, &stmt_update, NULL);

    sqlite3_stmt *stmt_insert = NULL;
    const char *ins_sql =
        "INSERT INTO local_tools (name, path, skill_count, created_at, updated_at) "
        "VALUES (?, ?, ?, datetime('now', 'localtime'), datetime('now', 'localtime'));";
    sqlite3_prepare_v2(db, ins_sql, -1, &stmt_insert, NULL);

    for (int i = 0; i < disc_count; i++) {
        discovered_tool_t *dt = &disc_tools[i];
        int found_idx = -1;
        for (int j = 0; j < db_count; j++) {
            if (strcmp(db_entries[j].name, dt->name) == 0) {
                found_idx = j;
                break;
            }
        }

        if (found_idx >= 0) {
            db_entries[found_idx].seen = true;
            if (stmt_update) {
                sqlite3_reset(stmt_update);
                sqlite3_bind_text(stmt_update, 1, dt->path, -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(stmt_update, 2, dt->skill_count);
                sqlite3_bind_int(stmt_update, 3, db_entries[found_idx].id);
                if (sqlite3_step(stmt_update) == SQLITE_DONE) {
                    updated++;
                }
            }
        } else {
            if (stmt_insert) {
                sqlite3_reset(stmt_insert);
                sqlite3_bind_text(stmt_insert, 1, dt->name, -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt_insert, 2, dt->path, -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(stmt_insert, 3, dt->skill_count);
                if (sqlite3_step(stmt_insert) == SQLITE_DONE) {
                    inserted++;
                }
            }
        }
    }

    if (stmt_update) sqlite3_finalize(stmt_update);
    if (stmt_insert) sqlite3_finalize(stmt_insert);

    /* 移除本轮未发现的旧记录。 */
    sqlite3_stmt *stmt_del = NULL;
    const char *del_sql = "DELETE FROM local_tools WHERE id = ?;";
    sqlite3_prepare_v2(db, del_sql, -1, &stmt_del, NULL);

    for (int j = 0; j < db_count; j++) {
        if (!db_entries[j].seen) {
            if (stmt_del) {
                sqlite3_reset(stmt_del);
                sqlite3_bind_int(stmt_del, 1, db_entries[j].id);
                if (sqlite3_step(stmt_del) == SQLITE_DONE) {
                    deleted++;
                }
            }
        }
    }

    if (stmt_del) sqlite3_finalize(stmt_del);

    /* 提交整轮变更。 */
    sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);

    free(db_entries);
    free(disc_tools);

    if (out_inserted) *out_inserted = inserted;
    if (out_updated) *out_updated = updated;
    if (out_deleted) *out_deleted = deleted;

    return 0;
}

int db_query_tools(sqlite3 *db, tool_record_t **out_records, int *out_count,
                   const char *search_kw, int sort_col, bool sort_desc) {
    if (!db || !out_records || !out_count) return -1;
    *out_records = NULL;
    *out_count = 0;

    char query[1024];
    char where_clause[512] = "";

    bool has_kw = (search_kw && search_kw[0] != '\0');
    if (has_kw) {
        strcat(where_clause, "WHERE (name LIKE ? OR path LIKE ?)");
    }

    const char *col_name = "name";
    switch (sort_col) {
        case 0: col_name = "id"; break;
        case 1: col_name = "name"; break;
        case 2: col_name = "skill_count"; break;
        case 3: col_name = "path"; break;
        case 4: col_name = "created_at"; break;
        case 5: col_name = "updated_at"; break;
        default: col_name = "name"; break;
    }

    const char *dir = sort_desc ? "DESC" : "ASC";
    snprintf(query, sizeof(query),
             "SELECT id, name, path, skill_count, created_at, updated_at "
             "FROM local_tools %s ORDER BY %s %s;",
             where_clause, col_name, dir);

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, query, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "[db] Query prepare failed: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    if (has_kw) {
        char pattern[256];
        snprintf(pattern, sizeof(pattern), "%%%s%%", search_kw);
        sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, pattern, -1, SQLITE_TRANSIENT);
    }

    int cap = 32;
    int count = 0;
    tool_record_t *records = (tool_record_t *)malloc(sizeof(tool_record_t) * cap);
    if (!records) {
        sqlite3_finalize(stmt);
        return -1;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (count >= cap) {
            cap *= 2;
            tool_record_t *re = (tool_record_t *)realloc(records, sizeof(tool_record_t) * cap);
            if (!re) {
                free(records);
                sqlite3_finalize(stmt);
                return -1;
            }
            records = re;
        }

        tool_record_t *r = &records[count++];
        r->id = sqlite3_column_int(stmt, 0);

        const unsigned char *name = sqlite3_column_text(stmt, 1);
        strncpy(r->name, name ? (const char *)name : "", sizeof(r->name) - 1);
        r->name[sizeof(r->name) - 1] = '\0';

        const unsigned char *path = sqlite3_column_text(stmt, 2);
        strncpy(r->path, path ? (const char *)path : "", sizeof(r->path) - 1);
        r->path[sizeof(r->path) - 1] = '\0';

        r->skill_count = sqlite3_column_int(stmt, 3);

        const unsigned char *c_at = sqlite3_column_text(stmt, 4);
        strncpy(r->created_at, c_at ? (const char *)c_at : "", sizeof(r->created_at) - 1);
        r->created_at[sizeof(r->created_at) - 1] = '\0';

        const unsigned char *u_at = sqlite3_column_text(stmt, 5);
        strncpy(r->updated_at, u_at ? (const char *)u_at : "", sizeof(r->updated_at) - 1);
        r->updated_at[sizeof(r->updated_at) - 1] = '\0';
    }

    sqlite3_finalize(stmt);

    *out_records = records;
    *out_count = count;
    return 0;
}

int db_query_stats(sqlite3 *db, tool_stats_t *out_stats) {
    if (!db || !out_stats) return -1;
    memset(out_stats, 0, sizeof(tool_stats_t));

    const char *sql =
        "SELECT "
        "  COUNT(*), "
        "  COALESCE(SUM(skill_count), 0), "
        "  COALESCE(MAX(updated_at), '') "
        "FROM local_tools;";

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out_stats->total_tools = sqlite3_column_int(stmt, 0);
        out_stats->total_skills = sqlite3_column_int(stmt, 1);
        const unsigned char *last_s = sqlite3_column_text(stmt, 2);
        strncpy(out_stats->last_scanned_at, last_s ? (const char *)last_s : "", sizeof(out_stats->last_scanned_at) - 1);
        out_stats->last_scanned_at[sizeof(out_stats->last_scanned_at) - 1] = '\0';
    }

    sqlite3_finalize(stmt);
    return 0;
}

int db_get_setting(sqlite3 *db, const char *key, char *out_val, size_t cap, const char *default_val) {
    if (!out_val || cap == 0) return -1;
    if (default_val) {
        strncpy(out_val, default_val, cap - 1);
        out_val[cap - 1] = '\0';
    } else {
        out_val[0] = '\0';
    }
    if (!db || !key) return -1;

    const char *sql = "SELECT value FROM app_settings WHERE key = ? LIMIT 1;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;

    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);
    int res = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char *val = sqlite3_column_text(stmt, 0);
        if (val) {
            strncpy(out_val, (const char *)val, cap - 1);
            out_val[cap - 1] = '\0';
            res = 0;
        }
    }
    sqlite3_finalize(stmt);
    return res;
}

int db_set_setting(sqlite3 *db, const char *key, const char *value) {
    if (!db || !key) return -1;
    const char *sql = "INSERT INTO app_settings (key, value) VALUES (?, ?) "
                      "ON CONFLICT(key) DO UPDATE SET value = excluded.value;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;

    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, value ? value : "", -1, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 0 : -1;
}
