#include "sync_git_merge.h"
#include "sync_git_process.h"
#include "sync_git_repository.h"
#include "sync_directory.h"
#include "sync_local_files.h"
#include "skill_fingerprint.h"
#include "skill_metadata.h"
#include "portability.h"

#include <string.h>

#define GIT(dir, ...) sync_git_run(dir, (const char *const[]){__VA_ARGS__, NULL}, NULL, 0)
#define READ(dir, out, ...) sync_git_run(dir, (const char *const[]){__VA_ARGS__, NULL}, out, sizeof(out))
#define HISTORY_LIMIT 64

static int fail(char *error, size_t cap, const char *message) {
    if (error) snprintf(error, cap, "%s", message);
    return -1;
}

static bool valid_oid(const char *oid) {
    size_t len = strlen(oid);
    return (len == 40 || len == 64) && strspn(oid, "0123456789abcdef") == len;
}

static bool valid_skill(const char *name) {
    return sync_valid_component(name) && !skill_skip_name(name) &&
           strlen(name) < sizeof(((sync_drift_item_t *)0)->skill_name);
}

static int merge_root(const git_priv_t *repo, char path[SYNC_PATH_CAP]) {
    char metadata[SYNC_PATH_CAP];
    if (sync_join(metadata, sizeof(metadata), repo->git_cache_dir, ".git") != 0) return -1;
#ifdef _WIN32
    char *absolute = _fullpath(NULL, metadata, SYNC_PATH_CAP);
#else
    char *absolute = realpath(metadata, NULL);
#endif
    if (!absolute) return -1;
#ifdef _WIN32
    for (char *p = absolute; *p; p++) if (*p == '\\') *p = '/';
#endif
    int rc = sync_join(path, SYNC_PATH_CAP, absolute, "ai-space-merges");
    free(absolute);
    return rc;
}

/* 检查草稿归属。只能操作当前仓库专用目录中创建的直属工作区。 */
static bool draft_stage(const git_priv_t *repo, const sync_git_draft_t *draft, char stage[SYNC_PATH_CAP]) {
    char root[SYNC_PATH_CAP];
    if (merge_root(repo, root) != 0) return false;
    size_t length = strlen(root);
    if (strncmp(draft->workspace, root, length) != 0 || draft->workspace[length] != '/') return false;
    const char *name = draft->workspace + length + 1;
    const char *leaf = strchr(name, '/');
    const char *prefix = ".ai-space-stage-";
    if (!leaf || strcmp(leaf, "/work") != 0 || strncmp(name, prefix, strlen(prefix)) != 0) return false;
    for (const char *p = name + strlen(prefix); p < leaf; p++)
        if ((*p < '0' || *p > '9') && *p != '-') return false;
    size_t n = (size_t)(leaf - draft->workspace);
    memcpy(stage, draft->workspace, n);
    stage[n] = '\0';
    return true;
}

static bool owns_worktree(const git_priv_t *repo, const sync_git_draft_t *draft) {
    char root[SYNC_PATH_CAP], common[SYNC_PATH_CAP], top[SYNC_PATH_CAP];
    if (merge_root(repo, root) != 0 ||
        READ(draft->workspace, common, "rev-parse", "--git-common-dir") != 0 ||
        READ(draft->workspace, top, "rev-parse", "--show-toplevel") != 0)
        return false;
    char expected[SYNC_PATH_CAP];
    if (sync_join(expected, sizeof(expected), common, "ai-space-merges") != 0) return false;
#ifdef _WIN32
    return _stricmp(root, expected) == 0 && _stricmp(top, draft->workspace) == 0;
#else
    return strcmp(root, expected) == 0 && strcmp(top, draft->workspace) == 0;
#endif
}

static bool cleanup_draft(const git_priv_t *repo, sync_git_draft_t *draft) {
    char stage[SYNC_PATH_CAP];
    if (!draft_stage(repo, draft, stage)) return false;
    if (GIT(repo->git_cache_dir, "worktree", "remove", "--force", draft->workspace) != 0) return false;
    sync_remove_tree(stage);
    *draft = (sync_git_draft_t){0};
    return true;
}

static int create_draft(const git_priv_t *repo, sync_git_draft_t *draft) {
    char root[SYNC_PATH_CAP], stage[SYNC_PATH_CAP];
    if (merge_root(repo, root) != 0) return -1;
    ai_mkdir_p(root);
    if (sync_make_stage(root, stage, sizeof(stage)) != 0) return -1;
    if (sync_join(draft->workspace, sizeof(draft->workspace), stage, "work") == 0 &&
        GIT(repo->git_cache_dir, "worktree", "add", "--detach", draft->workspace, "HEAD") == 0) return 0;
    sync_remove_tree(stage);
    draft->workspace[0] = '\0';
    return -1;
}

static bool checkout_base(const char *work, const char *skill, const sync_baseline_item_t *base,
                          const char *remote_oid) {
    char path[SYNC_PATH_CAP];
    if (sync_join(path, sizeof(path), work, skill) != 0) return false;
    if (valid_oid(base->revision) &&
        GIT(work, "merge-base", "--is-ancestor", base->revision, remote_oid) == 0 &&
        GIT(work, "checkout", "--detach", base->revision) == 0 && sync_skill_matches(path, base->hash))
        return true;

    /* 旧库只有内容指纹，沿远端一侧的历史查找可验证的共同版本。 */
    char start[SYNC_GIT_OID_CAP + 2];
    snprintf(start, sizeof(start), "%s", remote_oid);
    for (int i = 0; i < HISTORY_LIMIT; i++) {
        char oid[SYNC_GIT_OID_CAP];
        if (READ(work, oid, "--literal-pathspecs", "log", "--first-parent", "-1", "--format=%H",
                 start, "--", skill) != 0 || !valid_oid(oid)) break;
        if (GIT(work, "checkout", "--detach", oid) == 0 && sync_skill_matches(path, base->hash))
            return true;
        snprintf(start, sizeof(start), "%s^", oid);
    }
    return false;
}

static int read_conflicts(sync_git_draft_t *draft) {
    int rc = sync_git_read_text(draft->workspace,
                               (const char *const[]){"diff", "--name-only", "--diff-filter=U", NULL},
                               draft->conflicts, sizeof(draft->conflicts));
    size_t n = strlen(draft->conflicts);
    if (n > 0 && draft->conflicts[n - 1] == '\n') draft->conflicts[n - 1] = '\0';
    return rc;
}

static bool merge_head_matches(const sync_git_draft_t *draft) {
    char head[SYNC_GIT_OID_CAP];
    return READ(draft->workspace, head, "rev-parse", "--verify", "MERGE_HEAD") == 0 &&
           strcmp(head, draft->local_oid) == 0;
}

static int stage_skill(const char *workspace, const char *skill) {
    char include[160];
    snprintf(include, sizeof(include), ":(top,literal)%s", skill);
    /* 同步清单忽略隐藏文件和 Python 缓存，合并时也保留 Git 基线中的这些路径。 */
    return GIT(workspace, "add", "-A", "--", include,
               ":(exclude,glob)**/.*", ":(exclude,glob)**/.*/**",
               ":(exclude,glob)**/__pycache__", ":(exclude,glob)**/__pycache__/**");
}

int sync_git_merge_prepare(sync_backend_t *backend, const char *root, const sync_drift_item_t *expected,
                           sync_git_draft_t *draft, char *error, size_t cap) {
    if (backend->cfg.type != SYNC_BACKEND_GIT || !valid_skill(expected->skill_name) ||
        expected->status != SYNC_DRIFT_CONFLICT || !expected->local_hash[0] ||
        !expected->remote_hash[0] || draft->workspace[0])
        return fail(error, cap, "当前没有可创建的 Git 合并");
    const char *name = expected->skill_name;
    const sync_baseline_item_t *base = sync_find_baseline(backend->baseline, name);
    if (!base) return fail(error, cap, "这个技能没有已确认的共同版本，请先备份，再选择保留一侧");
    git_priv_t *repo = backend->priv_data;
    if (git_repo_refresh(repo, error, cap) != 0) {
        if (error && !error[0]) fail(error, cap, "无法读取 Git 同步分支");
        return -1;
    }
    char local[SYNC_PATH_CAP], remote[SYNC_PATH_CAP], remote_oid[SYNC_GIT_OID_CAP];
    if (sync_join(local, sizeof(local), root, name) != 0 ||
        sync_join(remote, sizeof(remote), repo->git_cache_dir, name) != 0 ||
        !sync_skill_matches(local, expected->local_hash) || !sync_skill_matches(remote, expected->remote_hash))
        return fail(error, cap, "技能内容已变化，请重新检查远端");
    if (git_repo_head(repo->git_cache_dir, remote_oid) != 0 || !valid_oid(remote_oid) ||
        create_draft(repo, draft) != 0) return fail(error, cap, "无法创建 Git 合并工作区");

    const char *message = "找不到已验证的共同版本";
    if (checkout_base(draft->workspace, name, base, remote_oid)) {
        message = "无法将本地技能写入合并工作区";
        sync_drift_item_t changes = *expected;
        snprintf(changes.remote_hash, sizeof(changes.remote_hash), "%s", base->hash);
        if (sync_copy_local_skill(root, draft->workspace, name, &changes, false) == 0 &&
            stage_skill(draft->workspace, name) == 0 &&
            git_repo_commit_message(repo, draft->workspace, "sync: preserve local skill for merge") == 0 &&
            git_repo_head(draft->workspace, draft->local_oid) == 0 &&
            GIT(draft->workspace, "checkout", "--force", "--detach", remote_oid) == 0) {
            message = "Git 无法生成合并草稿";
            /* 先保留本地改动的提交，再以远端提交为合并结果的第一父提交。 */
            char author[sizeof(repo->user_name) + 16], email[sizeof(repo->user_email) + 16];
            snprintf(author, sizeof(author), "user.name=%s", repo->user_name);
            snprintf(email, sizeof(email), "user.email=%s", repo->user_email);
            GIT(draft->workspace, "-c", author, "-c", email,
                "merge", "--no-ff", "--no-commit", "--no-edit", draft->local_oid);
            if (merge_head_matches(draft) && read_conflicts(draft) == 0) {
                snprintf(draft->remote_oid, sizeof(draft->remote_oid), "%s", remote_oid);
                snprintf(draft->local_hash, sizeof(draft->local_hash), "%s", expected->local_hash);
                return 0;
            }
        }
    }
    if (!cleanup_draft(repo, draft)) *draft = (sync_git_draft_t){0};
    return fail(error, cap, message);
}

static bool result_parents_match(const sync_git_draft_t *draft) {
    char parents[2 * SYNC_GIT_OID_CAP + 2], expected[sizeof(parents)];
    snprintf(expected, sizeof(expected), "%s %s", draft->remote_oid, draft->local_oid);
    return READ(draft->workspace, parents, "show", "-s", "--format=%P", draft->result_oid) == 0 &&
           strcmp(parents, expected) == 0;
}

static bool selected_skill_clean(const sync_git_draft_t *draft, const char *name) {
    char extras[128];
    int diff = draft->result_oid[0]
        ? GIT(draft->workspace, "--literal-pathspecs", "diff", "--quiet", draft->result_oid, "--", name)
        : GIT(draft->workspace, "--literal-pathspecs", "diff", "--quiet", "--", name);
    return diff == 0 &&
           READ(draft->workspace, extras, "--literal-pathspecs", "ls-files", "--others", "--", name) == 0 &&
           !extras[0];
}

static bool merge_has_no_markers(const sync_git_draft_t *draft, const char *name) {
    return GIT(draft->workspace, "-c", "core.whitespace=-blank-at-eol,-blank-at-eof,-space-before-tab",
               "--literal-pathspecs", "diff", "--cached", "--check", draft->remote_oid, "--", name) == 0;
}

int sync_git_merge_stage(sync_backend_t *backend, const char *skill, sync_git_draft_t *draft,
                         char *error, size_t cap) {
    if (backend->cfg.type != SYNC_BACKEND_GIT || !valid_skill(skill) || draft->result_oid[0] ||
        !valid_oid(draft->remote_oid) || !valid_oid(draft->local_oid))
        return fail(error, cap, "合并草稿不可暂存");
    git_priv_t *repo = backend->priv_data;
    char stage[SYNC_PATH_CAP], head[SYNC_GIT_OID_CAP];
    if (git_repo_open_cache(repo, error, cap) != 0) return -1;
    if (!draft_stage(repo, draft, stage) || !owns_worktree(repo, draft) ||
        git_repo_head(draft->workspace, head) != 0 || strcmp(head, draft->remote_oid) != 0 ||
        !merge_head_matches(draft))
        return fail(error, cap, "合并工作区的 Git 状态已变化");
    if (stage_skill(draft->workspace, skill) != 0)
        return fail(error, cap, "无法暂存该技能的修改");
    if (!merge_has_no_markers(draft, skill))
        return fail(error, cap, "暂存的内容仍有冲突标记，请编辑后重新暂存");
    if (read_conflicts(draft) != 0 || draft->conflicts[0])
        return fail(error, cap, "还有未解决的文件冲突");
    return 0;
}

static bool changes_only_skill(const sync_git_draft_t *draft, const char *skill, bool staged) {
    char exclude[160];
    snprintf(exclude, sizeof(exclude), ":(top,exclude,literal)%s", skill);
    if (staged)
        return GIT(draft->workspace, "diff", "--cached", "--quiet",
                   draft->remote_oid, "--", ".", exclude) == 0;
    return GIT(draft->workspace, "diff", "--quiet", draft->remote_oid, draft->result_oid,
               "--", ".", exclude) == 0;
}

static int create_merge_commit(const git_priv_t *repo, const char *skill, sync_git_draft_t *draft,
                               char *error, size_t cap) {
    char head[SYNC_GIT_OID_CAP], path[SYNC_PATH_CAP];
    if (git_repo_head(draft->workspace, head) != 0 || strcmp(head, draft->remote_oid) != 0 ||
        !merge_head_matches(draft) || read_conflicts(draft) != 0)
        return fail(error, cap, "合并工作区的 Git 状态已变化，请重新创建草稿");
    if (draft->conflicts[0]) return fail(error, cap, "还有冲突文件未暂存为已解决");
    if (!changes_only_skill(draft, skill, true))
        return fail(error, cap, "合并工作区暂存了该技能以外的改动");
    if (!selected_skill_clean(draft, skill))
        return fail(error, cap, "该技能还有未暂存的改动");
    if (!merge_has_no_markers(draft, skill))
        return fail(error, cap, "合并结果仍有冲突标记，请检查文件内容");
    if (sync_join(path, sizeof(path), draft->workspace, skill) != 0 || !skill_is_directory(path))
        return fail(error, cap, "合并结果需要保留有效的 SKILL.md");
    char message[192];
    snprintf(message, sizeof(message), "sync: merge skill %s", skill);
    if (git_repo_commit_message(repo, draft->workspace, message) != 0 ||
        git_repo_head(draft->workspace, draft->result_oid) != 0 || !result_parents_match(draft))
        return fail(error, cap, "无法创建 Git 合并提交");
    return 0;
}

int sync_git_merge_publish(sync_backend_t *backend, const char *root, const char *skill,
                           sync_git_draft_t *draft, sync_baseline_item_t *confirmed, char *error, size_t cap) {
    if (confirmed) *confirmed = (sync_baseline_item_t){0};
    if (backend->cfg.type != SYNC_BACKEND_GIT || !valid_skill(skill) ||
        !valid_oid(draft->remote_oid) || !valid_oid(draft->local_oid))
        return fail(error, cap, "合并草稿不可用");
    git_priv_t *repo = backend->priv_data;
    int rc = git_repo_refresh(repo, error, cap);
    if (rc != 0) {
        if (error && !error[0]) fail(error, cap, "无法获取 Git 同步分支");
        return -1;
    }
    char stage[SYNC_PATH_CAP], local[SYNC_PATH_CAP], remote[SYNC_GIT_OID_CAP];
    if (!draft_stage(repo, draft, stage) || !owns_worktree(repo, draft) ||
        sync_join(local, sizeof(local), root, skill) != 0 ||
        git_repo_head(repo->git_cache_dir, remote) != 0)
        return fail(error, cap, "合并工作区或远端提交不可用");
    bool published = draft->result_oid[0] && strcmp(remote, draft->result_oid) == 0;
    if (!published && strcmp(remote, draft->remote_oid) != 0)
        return fail(error, cap, "远端已更新，请保留工作区中的修改并重新合并");
    if (!sync_skill_matches(local, draft->local_hash))
        return fail(error, cap, published ? "合并已发布，本地主仓有新改动，原内容已保留"
                                         : "本地主仓有新改动，合并尚未发布");

    if (!draft->result_oid[0] && create_merge_commit(repo, skill, draft, error, cap) != 0) return -1;
    char head[SYNC_GIT_OID_CAP];
    if (!valid_oid(draft->result_oid) || !result_parents_match(draft) || !changes_only_skill(draft, skill, false) ||
        git_repo_head(draft->workspace, head) != 0 || strcmp(head, draft->result_oid) != 0 ||
        !selected_skill_clean(draft, skill)) return fail(error, cap, "合并提交或工作区已变化");
    sync_drift_item_t expected = {0};
    char merged[SYNC_PATH_CAP], replacement[SYNC_PATH_CAP], local_stage[SYNC_PATH_CAP];
    if (sync_join(merged, sizeof(merged), draft->workspace, skill) != 0 ||
        !skill_is_directory(merged) || sync_make_stage(root, local_stage, sizeof(local_stage)) != 0)
        return fail(error, cap, "无法验证合并结果");
    snprintf(expected.local_hash, sizeof(expected.local_hash), "%s", draft->local_hash);
    const char *message = "无法暂存合并结果，请检查本地路径和文件类型";
    rc = -1;
    if (sync_join(replacement, sizeof(replacement), local_stage, "new") == 0 &&
        file_tree_copy(merged, replacement, false, skill_skip_name) == 0 &&
        hub_compute_skill_hash(replacement, expected.remote_hash, sizeof(expected.remote_hash), NULL) == 0) {
        if (!sync_skill_matches(local, expected.local_hash)) message = "本地主仓有新改动";
        else if (!selected_skill_clean(draft, skill)) message = "合并工作区有新改动，结果尚未发布";
        else if (!published && git_repo_push_from(repo, draft->workspace) != 0)
            message = "远端拒绝推送，请检查分支权限和远端更新";
        else {
            message = "合并已发布，本地主仓未更新，合并草稿仍保留";
            rc = sync_publish_local_skill(local_stage, replacement, local, &expected, true);
        }
    }
    sync_cleanup_stage(local_stage, rc);
    if (rc != 0) return fail(error, cap, message);
    if (confirmed) {
        snprintf(confirmed->skill, sizeof(confirmed->skill), "%s", skill);
        snprintf(confirmed->hash, sizeof(confirmed->hash), "%s", expected.remote_hash);
        snprintf(confirmed->revision, sizeof(confirmed->revision), "%s", draft->result_oid);
    }
    if (!cleanup_draft(repo, draft)) {
        /* 文件和 Git 分支均已更新，清理失败不应让用户再次发布。 */
        *draft = (sync_git_draft_t){0};
    }
    return 0;
}
