#include "sync_git_repository.h"
#include "sync_git_process.h"
#include "portability.h"
#include "file_tree.h"
#include "hub_paths.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>

#ifndef _WIN32
#include <sys/file.h>
#endif

int git_repo_configure(git_priv_t *priv, const sync_config_t *cfg) {
    strncpy(priv->repo_url, cfg->git_repo_url, sizeof(priv->repo_url) - 1);
    strncpy(priv->branch, cfg->git_branch[0] ? cfg->git_branch : "main", sizeof(priv->branch) - 1);
    strncpy(priv->auth_token, cfg->git_auth_token, sizeof(priv->auth_token) - 1);
    strncpy(priv->user_name, cfg->git_user_name[0] ? cfg->git_user_name : "SkillBoard User", sizeof(priv->user_name) - 1);
    strncpy(priv->user_email, cfg->git_user_email[0] ? cfg->git_user_email : "user@example.com", sizeof(priv->user_email) - 1);

    char raw_cache[512] = "";
    if (cfg->git_cache_dir[0] != '\0') {
        strncpy(raw_cache, cfg->git_cache_dir, sizeof(raw_cache) - 1);
    } else {
        const char *env_cache = getenv("SKILLBOARD_GIT_CACHE");
        if (env_cache && env_cache[0] != '\0') {
            strncpy(raw_cache, env_cache, sizeof(raw_cache) - 1);
        } else {
            /* 自定义主仓的缓存放在主仓的同级 cache 目录。 */
            char custom_hub[512] = "";
            hub_get_custom_dirs(custom_hub, sizeof(custom_hub), NULL, 0);
            if (custom_hub[0] != '\0' &&
                strstr(custom_hub, ".skillboard/skills") == NULL &&
                strstr(custom_hub, ".ai-space/skills") == NULL) {
                char tmp[512];
                strncpy(tmp, custom_hub, sizeof(tmp) - 1);
                tmp[sizeof(tmp) - 1] = '\0';
                char *slash = strrchr(tmp, '/');
                if (slash) *slash = '\0';
                snprintf(raw_cache, sizeof(raw_cache), "%s/cache/git_remote", tmp);
            } else {
                strncpy(raw_cache, "~/.skillboard/cache/git_remote", sizeof(raw_cache) - 1);
            }
        }
    }
    return sync_expand(raw_cache, priv->git_cache_dir, sizeof(priv->git_cache_dir));
}

bool sync_git_uses_ssh(const char *url) {
    return strncmp(url, "ssh://", 6) == 0 || strncmp(url, "git@", 4) == 0;
}

bool sync_git_uses_token(const char *url) {
    return strncmp(url, "https://", 8) == 0;
}

static void git_build_auth_url(const git_priv_t *priv, char *out_url, size_t cap) {
    if (priv->auth_token[0] != '\0' && sync_git_uses_token(priv->repo_url)) {
        snprintf(out_url, cap, "https://oauth2:%s@%s", priv->auth_token, priv->repo_url + 8);
    } else {
        strncpy(out_url, priv->repo_url, cap - 1);
        out_url[cap - 1] = '\0';
    }
}

#define GIT(cwd, ...) sync_git_run(cwd, (const char *const[]){__VA_ARGS__, NULL}, NULL, 0)
#define GIT_READ(cwd, out, ...) sync_git_run(cwd, (const char *const[]){__VA_ARGS__, NULL}, out, sizeof(out))

static bool git_valid_url(const char *url) {
    return strncmp(url, "http://", 7) == 0 || sync_git_uses_token(url) || sync_git_uses_ssh(url);
}

int git_repo_check_available(const git_priv_t *repo, char *error, size_t cap) {
    if (!git_valid_url(repo->repo_url)) {
        if (error) snprintf(error, cap, "请输入有效的 HTTP、HTTPS 或 SSH 仓库地址");
        return -1;
    }
    char url[1024];
    git_build_auth_url(repo, url, sizeof(url));
    int rc = GIT(NULL, "ls-remote", "--", url);
    if (error) snprintf(error, cap, "%s", rc == 0 ? "Git 远端版本库连接正常"
                                    : "Git 连接失败，请检查仓库地址、网络和访问权限");
    return rc == 0 ? 0 : -1;
}

static const char *without_http_credentials(const char *url) {
    const char *host;
    if (strncmp(url, "https://", 8) == 0) host = url + 8;
    else if (strncmp(url, "http://", 7) == 0) host = url + 7;
    else return url;
    const char *at = strchr(host, '@');
    const char *path = strchr(host, '/');
    return at && (!path || at < path) ? at + 1 : host;
}

static bool same_origin(const char *a, const char *b) {
    const char *aa = without_http_credentials(a), *bb = without_http_credentials(b);
    if (aa == a || bb == b) return strcmp(a, b) == 0;
    return strncmp(a, b, a[4] == 's' ? 8 : 7) == 0 && strcmp(aa, bb) == 0;
}

static bool cache_empty(const char *path) {
    DIR *dir = opendir(path);
    if (!dir) return false;
    bool empty = true;
    for (;;) {
        errno = 0;
        struct dirent *entry = readdir(dir);
        if (!entry) { empty = errno == 0; break; }
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            empty = false;
            break;
        }
    }
    closedir(dir);
    return empty;
}

static FILE *lock_cache(const char *metadata) {
    char path[SYNC_PATH_CAP];
    if (sync_join(path, sizeof(path), metadata, "ai-space.lock") != 0) return NULL;
    FILE *file = fopen(path, "a+b");
    if (!file) return NULL;
#ifdef _WIN32
    OVERLAPPED lock = {0};
    bool acquired = LockFileEx((HANDLE)_get_osfhandle(_fileno(file)),
                               LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
                               0, 1, 0, &lock);
#else
    int fd = fileno(file);
    bool acquired = fcntl(fd, F_SETFD, FD_CLOEXEC) == 0 && flock(fd, LOCK_EX | LOCK_NB) == 0;
#endif
    if (acquired) return file;
    fclose(file);
    return NULL;
}

void git_repo_release(git_priv_t *repo) {
    if (repo->cache_lock) fclose(repo->cache_lock);
    repo->cache_lock = NULL;
}

static int prepare_cache(git_priv_t *repo, const char *auth_url) {
    char metadata[SYNC_PATH_CAP];
    if (sync_join(metadata, sizeof(metadata), repo->git_cache_dir, ".git") != 0) return -1;
    struct stat st;
    if (lstat(metadata, &st) != 0) {
        if (errno != ENOENT) return -1;
        ai_mkdir_p(repo->git_cache_dir);
        if (!cache_empty(repo->git_cache_dir)) return -1;
        if (GIT(repo->git_cache_dir, "init", "--initial-branch", repo->branch) != 0 ||
            GIT(repo->git_cache_dir, "remote", "add", "origin", auth_url) != 0) return -1;
    } else if (!S_ISDIR(st.st_mode)) {
        return -1;
    }
    char origin[1024];
    if (GIT_READ(repo->git_cache_dir, origin, "config", "--local", "--get", "remote.origin.url") != 0)
        return -1;
    if (!same_origin(origin, auth_url)) return 1;
    repo->cache_lock = lock_cache(metadata);
    if (!repo->cache_lock) return -1;
    if (strcmp(origin, auth_url) != 0 &&
        GIT(repo->git_cache_dir, "remote", "set-url", "origin", auth_url) != 0) {
        git_repo_release(repo);
        return -1;
    }
    return 0;
}

int git_repo_open_cache(git_priv_t *repo, char *error, size_t cap) {
    /* 同步任务持有缓存锁直到销毁后端，避免同时操作同一工作区。 */
    if (repo->cache_lock) return 0;
    char ref[sizeof(repo->branch) + sizeof("refs/heads/")];
    snprintf(ref, sizeof(ref), "refs/heads/%s", repo->branch);
    if (!git_valid_url(repo->repo_url) || repo->branch[0] == '-' ||
        GIT(NULL, "check-ref-format", ref) != 0) {
        if (error) snprintf(error, cap, "Git 仓库地址或分支格式无效");
        return -1;
    }
    char auth_url[1024];
    git_build_auth_url(repo, auth_url, sizeof(auth_url));
    int rc = prepare_cache(repo, auth_url);
    if (rc == 1) {
        /* 切换远端时另建缓存，保留旧仓库内容。 */
        uint64_t key = UINT64_C(14695981039346656037);
        for (const unsigned char *p = (const unsigned char *)repo->repo_url; *p; p++)
            key = (key ^ *p) * UINT64_C(1099511628211);
        char path[sizeof(repo->git_cache_dir)];
        int n = snprintf(path, sizeof(path), "%s-%016llx", repo->git_cache_dir, (unsigned long long)key);
        if (n > 0 && (size_t)n < sizeof(path)) {
            memcpy(repo->git_cache_dir, path, (size_t)n + 1);
            rc = prepare_cache(repo, auth_url);
        } else rc = -1;
    }
    if (rc != 0) {
        if (error) snprintf(error, cap, "无法初始化或验证 Git 缓存目录");
        return -1;
    }
    return 0;
}

int git_repo_refresh(git_priv_t *repo, char *error, size_t cap) {
    if (git_repo_open_cache(repo, error, cap) != 0) return -1;
    char ref[sizeof(repo->branch) + sizeof("refs/heads/")];
    snprintf(ref, sizeof(ref), "refs/heads/%s", repo->branch);
    const char *dir = repo->git_cache_dir;
    char status[256];
    if (GIT_READ(dir, status, "status", "--porcelain", "--untracked-files=all") != 0 || status[0]) {
        if (error) snprintf(error, cap, "Git 缓存中有未保存内容，请先检查缓存目录");
        return -1;
    }
    /* 只有成功获取选定的分支后才切换缓存内容。 */
    if (GIT(dir, "fetch", "--no-tags", "origin", ref) == 0) {
        if (GIT(dir, "checkout", "--force", "-B", repo->branch, "FETCH_HEAD") == 0) return 0;
    } else {
        if (GIT(dir, "ls-remote", "--exit-code", "--heads", "origin") == 2 &&
            GIT(dir, "rev-parse", "--verify", "HEAD") != 0 &&
            GIT(dir, "symbolic-ref", "HEAD", ref) == 0) return GIT_REPO_EMPTY;
    }
    if (error) snprintf(error, cap, "无法获取 Git 远端分支 %.63s", repo->branch);
    return -1;
}

int git_repo_stage(const git_priv_t *repo, const char *skill_name) {
    return GIT(repo->git_cache_dir, "--literal-pathspecs", "add", "--", skill_name) == 0 ? 0 : -1;
}

int git_repo_head(const char *dir, char out[SYNC_GIT_OID_CAP]) {
    return sync_git_run(dir, (const char *const[]){"rev-parse", "--verify", "HEAD", NULL},
                        out, SYNC_GIT_OID_CAP);
}

int git_repo_commit_message(const git_priv_t *repo, const char *dir, const char *message) {
    char name[sizeof(repo->user_name) + 16], email[sizeof(repo->user_email) + 16];
    snprintf(name, sizeof(name), "user.name=%s", repo->user_name);
    snprintf(email, sizeof(email), "user.email=%s", repo->user_email);
    return GIT(dir, "-c", name, "-c", email, "commit", "-m", message) == 0 ? 0 : -1;
}

int git_repo_commit(const git_priv_t *repo, const char *skill_name, int count) {
    const char *dir = repo->git_cache_dir;
    int dirty = GIT(dir, "diff", "--cached", "--quiet");
    if (dirty == 0) return 0;
    if (dirty != 1) return -1;

    char message[256];
    if (skill_name) snprintf(message, sizeof(message), "sync(skill): update %.127s [SkillBoard]", skill_name);
    else snprintf(message, sizeof(message), "sync(all): full sync of %d skills [SkillBoard]", count);
    return git_repo_commit_message(repo, dir, message);
}

void git_repo_restore(const git_priv_t *repo, int revision) {
    if (revision == GIT_REPO_EMPTY) GIT(repo->git_cache_dir, "read-tree", "--empty");
    else GIT(repo->git_cache_dir, "reset", "--hard", "HEAD");
}

int git_repo_push_from(const git_priv_t *repo, const char *dir) {
    char refspec[sizeof(repo->branch) + sizeof("HEAD:refs/heads/")];
    snprintf(refspec, sizeof(refspec), "HEAD:refs/heads/%s", repo->branch);
    return GIT(dir, "push", "origin", refspec) == 0 ? 0 : -1;
}

int git_repo_push(const git_priv_t *repo) {
    return git_repo_push_from(repo, repo->git_cache_dir);
}
