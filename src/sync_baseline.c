#include "sync_baseline.h"
#include "file_tree.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *scope_url(const char *url, char *clean, size_t cap) {
    const char *scheme = strstr(url, "://");
    if (!scheme) return url;
    const char *authority = scheme + 3;
    const char *at = strchr(authority, '@');
    if (!at || (size_t)(at - authority) >= strcspn(authority, "/?#")) return url;
    snprintf(clean, cap, "%.*s%s", (int)(authority - url), url, at + 1);
    return clean;
}

int sync_baseline_scope(const sync_config_t *cfg, const char *local, char *out, size_t cap) {
    char root[SYNC_PATH_CAP], endpoint[SYNC_PATH_CAP];
    if (sync_expand(local, root, sizeof(root)) != 0) return -1;

    /* 只使用目标身份字段确定基线范围，单独配置的 Token 和密码不参与。 */
    const char *parts[5] = {root};
    size_t count = 1;
    switch (cfg->type) {
    case SYNC_BACKEND_GIT:
        parts[count++] = scope_url(cfg->git_repo_url, endpoint, sizeof(endpoint));
        parts[count++] = cfg->git_branch[0] ? cfg->git_branch : "main";
        break;
    case SYNC_BACKEND_WEBDAV:
        parts[count++] = scope_url(cfg->webdav_url, endpoint, sizeof(endpoint));
        parts[count++] = cfg->webdav_remote_path;
        parts[count++] = cfg->webdav_user;
        break;
    case SYNC_BACKEND_NFS:
        if (cfg->share_mode == SYNC_SHARE_MOUNTED) {
            parts[count++] = cfg->nfs_mount_path;
        } else {
            parts[count++] = cfg->share_server;
            parts[count++] = cfg->share_export;
            parts[count++] = cfg->share_subdir;
            parts[count++] = cfg->share_mode == SYNC_SHARE_SMB ? cfg->smb_user : "";
        }
        break;
    default:
        return -1;
    }

    int n = snprintf(out, cap, "v1:%d:%d:", cfg->type,
                     cfg->type == SYNC_BACKEND_NFS ? cfg->share_mode : 0);
    if (n < 0 || (size_t)n >= cap) return -1;
    size_t used = (size_t)n;
    for (size_t i = 0; i < count; i++) {
        /* 为每段写入长度，避免不同字段组合拼出相同范围。 */
        n = snprintf(out + used, cap - used, "%zu:%s", strlen(parts[i]), parts[i]);
        if (n < 0 || (size_t)n >= cap - used) return -1;
        used += (size_t)n;
    }
    return 0;
}

int sync_baseline_load(sqlite3 *db, const char *scope, sync_baseline_t *baseline) {
    const char *sql = "SELECT skill,hash,git_revision FROM sync_baselines WHERE scope=? ORDER BY skill COLLATE BINARY";
    *baseline = (sync_baseline_t){0};
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    int rc = sqlite3_bind_text(stmt, 1, scope, -1, SQLITE_STATIC);
    size_t capacity = 0;
    if (rc == SQLITE_OK) {
        while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
            const char *skill = (const char *)sqlite3_column_text(stmt, 0);
            const char *hash = (const char *)sqlite3_column_text(stmt, 1);
            const char *revision = (const char *)sqlite3_column_text(stmt, 2);
            if (!skill || !hash || strlen(skill) >= sizeof(baseline->items[0].skill) ||
                strlen(hash) != 16 || strspn(hash, "0123456789abcdef") != 16 ||
                !revision || strlen(revision) >= sizeof(baseline->items[0].revision)) {
                rc = SQLITE_CORRUPT;
                break;
            }
            if (baseline->count == capacity) {
                capacity = capacity ? capacity * 2 : 32;
                void *items = realloc(baseline->items, capacity * sizeof(*baseline->items));
                if (!items) { rc = SQLITE_NOMEM; break; }
                baseline->items = items;
            }
            sync_baseline_item_t *item = &baseline->items[baseline->count++];
            snprintf(item->skill, sizeof(item->skill), "%s", skill);
            snprintf(item->hash, sizeof(item->hash), "%s", hash);
            snprintf(item->revision, sizeof(item->revision), "%s", revision);
        }
    }
    sqlite3_finalize(stmt);
    if (rc == SQLITE_DONE) return 0;
    free(baseline->items);
    *baseline = (sync_baseline_t){0};
    return -1;
}

int sync_baseline_record(sqlite3 *db, const char *scope, const sync_drift_summary_t *drift) {
    /* 只为已验证一致的技能推进基线，冲突保留上一次确认的版本。 */
    if (!drift->in_sync_count) return 0;
    const char *sql = "INSERT INTO sync_baselines(scope,skill,hash,git_revision) VALUES(?,?,?,?)"
                      " ON CONFLICT(scope,skill) DO UPDATE SET hash=excluded.hash,git_revision=excluded.git_revision"
                      " WHERE sync_baselines.hash<>excluded.hash OR sync_baselines.git_revision<>excluded.git_revision";
    if (sqlite3_exec(db, "BEGIN IMMEDIATE", NULL, NULL, NULL) != SQLITE_OK) return -1;
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    for (int i = 0; rc == SQLITE_OK && i < drift->count; i++) {
        const sync_drift_item_t *item = &drift->items[i];
        if (item->status != SYNC_DRIFT_IN_SYNC) continue;
        rc = sqlite3_bind_text(stmt, 1, scope, -1, SQLITE_STATIC);
        if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 2, item->skill_name, -1, SQLITE_STATIC);
        if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 3, item->local_hash, -1, SQLITE_STATIC);
        if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 4, drift->remote_revision, -1, SQLITE_STATIC);
        if (rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_DONE) rc = sqlite3_reset(stmt);
        else if (rc == SQLITE_OK) rc = SQLITE_ERROR;
    }
    sqlite3_finalize(stmt);
    if (rc == SQLITE_OK && sqlite3_exec(db, "COMMIT", NULL, NULL, NULL) == SQLITE_OK) return 0;
    sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
    return -1;
}

static bool column_text(sqlite3_stmt *stmt, int column, char *out, size_t cap) {
    const char *value = (const char *)sqlite3_column_text(stmt, column);
    if (!value || strlen(value) >= cap) return false;
    snprintf(out, cap, "%s", value);
    return true;
}

int sync_git_draft_load(sqlite3 *db, const char *scope, const char *skill, sync_git_draft_t *draft) {
    const char *sql = "SELECT workspace,local_hash,remote_oid,local_oid,result_oid,conflicts"
                      " FROM sync_git_drafts WHERE scope=? AND skill=?";
    sqlite3_stmt *stmt = NULL;
    *draft = (sync_git_draft_t){0};
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    int rc = sqlite3_bind_text(stmt, 1, scope, -1, SQLITE_STATIC);
    if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 2, skill, -1, SQLITE_STATIC);
    if (rc == SQLITE_OK) rc = sqlite3_step(stmt);
    bool ok = rc == SQLITE_DONE ||
        (rc == SQLITE_ROW &&
         column_text(stmt, 0, draft->workspace, sizeof(draft->workspace)) &&
         column_text(stmt, 1, draft->local_hash, sizeof(draft->local_hash)) &&
         column_text(stmt, 2, draft->remote_oid, sizeof(draft->remote_oid)) &&
         column_text(stmt, 3, draft->local_oid, sizeof(draft->local_oid)) &&
         column_text(stmt, 4, draft->result_oid, sizeof(draft->result_oid)) &&
         column_text(stmt, 5, draft->conflicts, sizeof(draft->conflicts)));
    sqlite3_finalize(stmt);
    if (!ok) *draft = (sync_git_draft_t){0};
    return ok ? 0 : -1;
}

int sync_git_draft_first(sqlite3 *db, const char *scope, char skill[128]) {
    sqlite3_stmt *stmt = NULL;
    skill[0] = '\0';
    if (sqlite3_prepare_v2(db, "SELECT skill FROM sync_git_drafts WHERE scope=? ORDER BY skill LIMIT 1",
                           -1, &stmt, NULL) != SQLITE_OK) return -1;
    int rc = sqlite3_bind_text(stmt, 1, scope, -1, SQLITE_STATIC);
    if (rc == SQLITE_OK) rc = sqlite3_step(stmt);
    bool ok = rc == SQLITE_DONE || (rc == SQLITE_ROW && column_text(stmt, 0, skill, 128));
    sqlite3_finalize(stmt);
    return ok ? 0 : -1;
}

int sync_git_draft_save(sqlite3 *db, const char *scope, const char *skill, const char *previous,
                        const sync_git_draft_t *draft) {
    if (!previous[0] && !draft->workspace[0]) return 0;
    const char *sql = draft->workspace[0]
        ? "INSERT INTO sync_git_drafts(scope,skill,workspace,local_hash,remote_oid,local_oid,result_oid,conflicts)"
          " VALUES(?,?,?,?,?,?,?,?) ON CONFLICT(scope,skill) DO UPDATE SET"
          " result_oid=excluded.result_oid,conflicts=excluded.conflicts WHERE workspace=?"
        : "DELETE FROM sync_git_drafts WHERE scope=? AND skill=? AND workspace=?";
    const char *values[] = {scope, skill, draft->workspace, draft->local_hash, draft->remote_oid,
                           draft->local_oid, draft->result_oid, draft->conflicts, previous};
    int count = draft->workspace[0] ? 9 : 3;
    if (count == 3) values[2] = previous;
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    int rc = SQLITE_OK;
    for (int i = 0; rc == SQLITE_OK && i < count; i++)
        rc = sqlite3_bind_text(stmt, i + 1, values[i], -1, SQLITE_STATIC);
    if (rc == SQLITE_OK) rc = sqlite3_step(stmt);
    bool ok = rc == SQLITE_DONE && sqlite3_changes(db) == 1;
    sqlite3_finalize(stmt);
    return ok ? 0 : -1;
}
