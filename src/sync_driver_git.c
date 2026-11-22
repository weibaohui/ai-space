#include "sync_backend.h"
#include "hub_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include "portability.h"

typedef struct {
    char repo_url[512];
    char branch[64];
    char auth_token[256];
    char user_name[128];
    char user_email[128];
    char git_cache_dir[512]; /* Expanded path to local git tracking directory */
} git_priv_t;

static void expand_path_local(const char *in, char *out, size_t cap) {
    if (!in || in[0] == '\0') {
        if (out && cap > 0) out[0] = '\0';
        return;
    }
    if (in[0] == '~' && (in[1] == '/' || in[1] == '\0')) {
        const char *h = ai_home_dir();
        if (h && h[0]) {
            snprintf(out, cap, "%s%s", h, in + 1);
            return;
        }
    }
    strncpy(out, in, cap - 1);
    out[cap - 1] = '\0';
}

static void git_build_auth_url(const git_priv_t *priv, char *out_url, size_t cap) {
    if (priv->auth_token[0] != '\0' && strncmp(priv->repo_url, "https://", 8) == 0) {
        snprintf(out_url, cap, "https://oauth2:%s@%s", priv->auth_token, priv->repo_url + 8);
    } else {
        strncpy(out_url, priv->repo_url, cap - 1);
        out_url[cap - 1] = '\0';
    }
}

static int git_init(sync_backend_t *self, const sync_config_t *cfg) {
    git_priv_t *priv = (git_priv_t *)calloc(1, sizeof(git_priv_t));
    if (!priv) return -1;

    strncpy(priv->repo_url, cfg->git_repo_url, sizeof(priv->repo_url) - 1);
    strncpy(priv->branch, cfg->git_branch[0] ? cfg->git_branch : "main", sizeof(priv->branch) - 1);
    strncpy(priv->auth_token, cfg->git_auth_token, sizeof(priv->auth_token) - 1);
    strncpy(priv->user_name, cfg->git_user_name[0] ? cfg->git_user_name : "AI-Space User", sizeof(priv->user_name) - 1);
    strncpy(priv->user_email, cfg->git_user_email[0] ? cfg->git_user_email : "user@example.com", sizeof(priv->user_email) - 1);

    char raw_cache[512] = "";
    if (cfg->git_cache_dir[0] != '\0') {
        strncpy(raw_cache, cfg->git_cache_dir, sizeof(raw_cache) - 1);
    } else {
        const char *env_cache = getenv("AI_SPACE_GIT_CACHE");
        if (env_cache && env_cache[0] != '\0') {
            strncpy(raw_cache, env_cache, sizeof(raw_cache) - 1);
        } else {
            /* If custom hub dir is active, derive git cache relative to custom hub parent */
            char custom_hub[512] = "";
            hub_get_custom_dirs(custom_hub, sizeof(custom_hub), NULL, 0);
            if (custom_hub[0] != '\0' && strstr(custom_hub, ".ai-space/skills") == NULL) {
                char tmp[512];
                strncpy(tmp, custom_hub, sizeof(tmp) - 1);
                tmp[sizeof(tmp) - 1] = '\0';
                char *slash = strrchr(tmp, '/');
                if (slash) *slash = '\0';
                snprintf(raw_cache, sizeof(raw_cache), "%s/cache/git_remote", tmp);
            } else {
                strncpy(raw_cache, "~/.ai-space/cache/git_remote", sizeof(raw_cache) - 1);
            }
        }
    }
    expand_path_local(raw_cache, priv->git_cache_dir, sizeof(priv->git_cache_dir));

    self->priv_data = priv;
    return 0;
}

static void git_destroy(sync_backend_t *self) {
    if (self->priv_data) {
        free(self->priv_data);
        self->priv_data = NULL;
    }
}

static int git_check_available(sync_backend_t *self, char *err_buf, size_t err_cap) {
    git_priv_t *priv = (git_priv_t *)self->priv_data;
    if (!priv || priv->repo_url[0] == '\0') {
        if (err_buf) snprintf(err_buf, err_cap, "未配置 Git 远端仓库地址");
        return -1;
    }

    if (strncmp(priv->repo_url, "http://", 7) != 0 &&
        strncmp(priv->repo_url, "https://", 8) != 0 &&
        strncmp(priv->repo_url, "git@", 4) != 0) {
        if (err_buf) snprintf(err_buf, err_cap, "Git 仓库地址格式不正确 (必须以 https://、http:// 或 git@ 开头)");
        return -1;
    }

    char auth_url[768];
    git_build_auth_url(priv, auth_url, sizeof(auth_url));

    char cmd[1280];
    /* env vars instead of the POSIX "VAR=val cmd" prefix (not valid under cmd.exe) */
    ai_setenv("GIT_TERMINAL_PROMPT", "0");
    ai_setenv("GIT_SSH_COMMAND", "ssh -o BatchMode=yes -o ConnectTimeout=8");
    snprintf(cmd, sizeof(cmd),
             "git ls-remote --exit-code \"%s\" HEAD 2>&1",
             auth_url);

    FILE *pipe = AI_POPEN(cmd, "r");
    if (!pipe) {
        if (err_buf) snprintf(err_buf, err_cap, "无法执行 git 检测命令");
        return -1;
    }
    char line[512] = {0};
    char first_line[256] = {0};
    if (fgets(line, sizeof(line), pipe)) {
        strncpy(first_line, line, sizeof(first_line) - 1);
        size_t l = strlen(first_line);
        if (l > 0 && first_line[l - 1] == '\n') first_line[l - 1] = '\0';
    }
    int rc = AI_PCLOSE(pipe);

    if (rc == 0) {
        self->is_connected = true;
        if (err_buf) snprintf(err_buf, err_cap, "Git 远端版本库检测通过，连接正常！");
        snprintf(self->status_msg, sizeof(self->status_msg), "Git 远端仓库就绪: %s [%s]", priv->repo_url, priv->branch);
        return 0;
    } else {
        if (err_buf) {
            if (strstr(first_line, "Repository not found") || strstr(first_line, "not found")) {
                snprintf(err_buf, err_cap, "Git 仓库不存在或无读取权限");
            } else if (strstr(first_line, "Authentication failed") || strstr(first_line, "fatal: Authentication")) {
                snprintf(err_buf, err_cap, "Git 鉴权失败：Access Token 无效或过期");
            } else if (first_line[0] != '\0') {
                snprintf(err_buf, err_cap, "Git 远端连接失败: %s", first_line);
            } else {
                snprintf(err_buf, err_cap, "Git 远端检测失败 (退出代码: %d)", rc);
            }
        }
        return -1;
    }
}

static int git_test_connection(sync_backend_t *self, char *err_buf, size_t err_cap) {
    git_priv_t *priv = (git_priv_t *)self->priv_data;
    if (!priv || priv->repo_url[0] == '\0') {
        if (err_buf) snprintf(err_buf, err_cap, "未配置 Git 远端仓库地址");
        return -1;
    }
    self->is_connected = true;
    return 0;
}

static int ensure_git_cache_ready(git_priv_t *priv, char *err_buf, size_t err_cap) {
    if (!priv || priv->git_cache_dir[0] == '\0') {
        if (err_buf) snprintf(err_buf, err_cap, "Git 缓存路径未初始化");
        return -1;
    }

    char git_dir[1024];
    snprintf(git_dir, sizeof(git_dir), "%s/.git", priv->git_cache_dir);
    struct stat st;

    char auth_url[768];
    git_build_auth_url(priv, auth_url, sizeof(auth_url));

    if (stat(git_dir, &st) != 0) {
        /* Local clone does not exist: clone from remote */
        ai_mkdir_p(priv->git_cache_dir);

        char clone_cmd[2048];
        snprintf(clone_cmd, sizeof(clone_cmd),
                 "git clone --branch \"%s\" \"%s\" \"%s\" 2>&1",
                 priv->branch, auth_url, priv->git_cache_dir);

        FILE *pipe = AI_POPEN(clone_cmd, "r");
        char out[512] = "";
        if (pipe) {
            char line[256];
            while (fgets(line, sizeof(line), pipe)) {
                if (out[0] == '\0') strncpy(out, line, sizeof(out) - 1);
            }
            int rc = AI_PCLOSE(pipe);
            if (rc != 0) {
                /* Branch might not exist yet, try cloning default branch */
                snprintf(clone_cmd, sizeof(clone_cmd),
                         "git clone \"%s\" \"%s\" 2>&1",
                         auth_url, priv->git_cache_dir);
                FILE *p2 = AI_POPEN(clone_cmd, "r");
                if (p2) AI_PCLOSE(p2);
            }
        }
    } else {
        /* Already cloned: ensure remote URL is updated */
        char set_url_cmd[1024];
        snprintf(set_url_cmd, sizeof(set_url_cmd),
                 "cd \"%s\" && git remote set-url origin \"%s\" 2>&1",
                 priv->git_cache_dir, auth_url);
        system(set_url_cmd);
    }

    /* Configure user identity inside this cache repo */
    if (priv->user_name[0] != '\0') {
        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "cd \"%s\" && git config user.name \"%s\"",
                 priv->git_cache_dir, priv->user_name);
        system(cmd);
    }
    if (priv->user_email[0] != '\0') {
        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "cd \"%s\" && git config user.email \"%s\"",
                 priv->git_cache_dir, priv->user_email);
        system(cmd);
    }

    return 0;
}

static int git_fetch_remote_manifest(sync_backend_t *self, sync_remote_manifest_t *out_manifest,
                                     char *err_buf, size_t err_cap) {
    git_priv_t *priv = (git_priv_t *)self->priv_data;
    if (!priv || priv->repo_url[0] == '\0') {
        if (err_buf) snprintf(err_buf, err_cap, "Git URL 未配置");
        return -1;
    }

    if (ensure_git_cache_ready(priv, err_buf, err_cap) != 0) {
        return -1;
    }

    /* Fetch from remote and update tracking branch */
    char fetch_cmd[1024];
    snprintf(fetch_cmd, sizeof(fetch_cmd),
             "cd \"%s\" && git fetch origin \"%s\" 2>&1 && git checkout -B \"%s\" \"origin/%s\" 2>&1",
             priv->git_cache_dir, priv->branch, priv->branch, priv->branch);
    system(fetch_cmd);

    /* Enumerate skills in git_cache_dir */
    DIR *d = opendir(priv->git_cache_dir);
    if (!d) {
        if (err_buf) snprintf(err_buf, err_cap, "无法读取 Git 本地缓存目录: %s", priv->git_cache_dir);
        return -1;
    }

    int cap = 32;
    out_manifest->items = (sync_remote_skill_item_t *)calloc(cap, sizeof(sync_remote_skill_item_t));
    out_manifest->count = 0;
    strncpy(out_manifest->remote_root, priv->repo_url, sizeof(out_manifest->remote_root) - 1);
    out_manifest->fetched_at = time(NULL);

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        if (strcmp(ent->d_name, "node_modules") == 0 || strcmp(ent->d_name, "__pycache__") == 0) continue;

        char skill_dir[1024];
        snprintf(skill_dir, sizeof(skill_dir), "%s/%s", priv->git_cache_dir, ent->d_name);

        struct stat st;
        if (stat(skill_dir, &st) == 0 && S_ISDIR(st.st_mode)) {
            char skill_md[1024];
            snprintf(skill_md, sizeof(skill_md), "%s/SKILL.md", skill_dir);
            struct stat md_st;
            if (stat(skill_md, &md_st) == 0) {
                if (out_manifest->count >= cap) {
                    cap *= 2;
                    out_manifest->items = (sync_remote_skill_item_t *)realloc(
                        out_manifest->items, sizeof(sync_remote_skill_item_t) * cap);
                }
                sync_remote_skill_item_t *item = &out_manifest->items[out_manifest->count];
                strncpy(item->skill_name, ent->d_name, sizeof(item->skill_name) - 1);
                item->mtime = st.st_mtime;

                time_t dir_mtime = 0;
                hub_compute_skill_hash(skill_dir, item->hash, sizeof(item->hash), &dir_mtime);
                if (dir_mtime > item->mtime) item->mtime = dir_mtime;

                out_manifest->count++;
            }
        }
    }
    closedir(d);
    return 0;
}

static int git_check_drift(sync_backend_t *self, const char *local_hub_dir,
                           sync_drift_summary_t *out_diff, char *err_buf, size_t err_cap) {
    sync_remote_manifest_t manifest = {0};
    if (git_fetch_remote_manifest(self, &manifest, err_buf, err_cap) != 0) {
        return -1;
    }

    char exp_local[512];
    expand_path_local(local_hub_dir ? local_hub_dir : "~/.ai-space/skills", exp_local, sizeof(exp_local));

    int cap = manifest.count + 32;
    out_diff->items = (sync_drift_item_t *)calloc(cap, sizeof(sync_drift_item_t));
    out_diff->count = 0;
    out_diff->in_sync_count = 0;
    out_diff->remote_ahead_count = 0;
    out_diff->local_ahead_count = 0;
    out_diff->remote_only_count = 0;
    out_diff->local_only_count = 0;
    out_diff->conflict_count = 0;

    time_t now = time(NULL);
    struct tm tm_info;
    localtime_r(&now, &tm_info);
    strftime(out_diff->last_checked_at, sizeof(out_diff->last_checked_at), "%Y-%m-%d %H:%M:%S", &tm_info);

    /* 1. Compare remote items with local */
    for (int i = 0; i < manifest.count; i++) {
        sync_remote_skill_item_t *rm = &manifest.items[i];
        sync_drift_item_t *item = &out_diff->items[out_diff->count++];
        strncpy(item->skill_name, rm->skill_name, sizeof(item->skill_name) - 1);
        strncpy(item->remote_hash, rm->hash, sizeof(item->remote_hash) - 1);
        item->remote_mtime = rm->mtime;

        char local_skill_dir[1024];
        snprintf(local_skill_dir, sizeof(local_skill_dir), "%s/%s", exp_local, rm->skill_name);

        struct stat st;
        if (stat(local_skill_dir, &st) == 0 && S_ISDIR(st.st_mode)) {
            time_t l_mtime = 0;
            hub_compute_skill_hash(local_skill_dir, item->local_hash, sizeof(item->local_hash), &l_mtime);
            item->local_mtime = l_mtime;

            if (strcmp(item->local_hash, item->remote_hash) == 0) {
                item->status = SYNC_DRIFT_IN_SYNC;
                snprintf(item->status_desc, sizeof(item->status_desc), "两端完全一致");
                out_diff->in_sync_count++;
            } else if (item->remote_mtime > item->local_mtime) {
                item->status = SYNC_DRIFT_REMOTE_AHEAD;
                snprintf(item->status_desc, sizeof(item->status_desc), "远端已更新 (需拉取)");
                out_diff->remote_ahead_count++;
            } else {
                item->status = SYNC_DRIFT_LOCAL_AHEAD;
                snprintf(item->status_desc, sizeof(item->status_desc), "本地有新改动 (需推送)");
                out_diff->local_ahead_count++;
            }
        } else {
            item->status = SYNC_DRIFT_REMOTE_ONLY;
            item->local_hash[0] = '\0';
            snprintf(item->status_desc, sizeof(item->status_desc), "远端独有 (待拉取)");
            out_diff->remote_only_count++;
        }
    }

    /* 2. Check local items missing on remote */
    DIR *ld = opendir(exp_local);
    if (ld) {
        struct dirent *ent;
        while ((ent = readdir(ld)) != NULL) {
            if (ent->d_name[0] == '.') continue;
            char ldir[1024];
            snprintf(ldir, sizeof(ldir), "%s/%s", exp_local, ent->d_name);
            struct stat lst;
            if (stat(ldir, &lst) == 0 && S_ISDIR(lst.st_mode)) {
                bool found = false;
                for (int i = 0; i < out_diff->count; i++) {
                    if (strcmp(out_diff->items[i].skill_name, ent->d_name) == 0) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    if (out_diff->count >= cap) {
                        cap *= 2;
                        out_diff->items = (sync_drift_item_t *)realloc(
                            out_diff->items, sizeof(sync_drift_item_t) * cap);
                    }
                    sync_drift_item_t *item = &out_diff->items[out_diff->count++];
                    strncpy(item->skill_name, ent->d_name, sizeof(item->skill_name) - 1);
                    item->status = SYNC_DRIFT_LOCAL_ONLY;
                    time_t lm = 0;
                    hub_compute_skill_hash(ldir, item->local_hash, sizeof(item->local_hash), &lm);
                    item->local_mtime = lm;
                    item->remote_hash[0] = '\0';
                    snprintf(item->status_desc, sizeof(item->status_desc), "本地主仓独有 (待推送)");
                    out_diff->local_only_count++;
                }
            }
        }
        closedir(ld);
    }

    sync_free_manifest(&manifest);
    return 0;
}

static int git_pull_skill(sync_backend_t *self, const char *skill_name, const char *target_local_hub,
                          sync_progress_cb cb, void *user_data) {
    git_priv_t *priv = (git_priv_t *)self->priv_data;
    if (!priv || !skill_name) return -1;

    char exp_local[512];
    expand_path_local(target_local_hub ? target_local_hub : "~/.ai-space/skills", exp_local, sizeof(exp_local));
    AI_MKDIR(exp_local);

    if (cb) cb(0.2f, "正在从 Git 远端获取最新提交...", user_data);
    char pull_cmd[1024];
    snprintf(pull_cmd, sizeof(pull_cmd),
             "cd \"%s\" && git fetch origin \"%s\" 2>&1 && git reset --hard \"origin/%s\" 2>&1",
             priv->git_cache_dir, priv->branch, priv->branch);
    system(pull_cmd);

    char src_dir[1024];
    snprintf(src_dir, sizeof(src_dir), "%s/%s", priv->git_cache_dir, skill_name);

    char dst_dir[1024];
    snprintf(dst_dir, sizeof(dst_dir), "%s/%s", exp_local, skill_name);

    struct stat st;
    if (stat(src_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        if (cb) cb(1.0f, "远端未找到该技能目录", user_data);
        return -1;
    }

    if (cb) cb(0.6f, "正在同步技能文件到本地主仓...", user_data);
    int rc = hub_copy_dir_recursive(src_dir, dst_dir);
    if (cb) cb(1.0f, "Git 技能拉取完成", user_data);
    return rc;
}

static int git_push_skill(sync_backend_t *self, const char *skill_name, const char *source_local_hub,
                          sync_progress_cb cb, void *user_data) {
    git_priv_t *priv = (git_priv_t *)self->priv_data;
    if (!priv || !skill_name) return -1;

    char exp_local[512];
    expand_path_local(source_local_hub ? source_local_hub : "~/.ai-space/skills", exp_local, sizeof(exp_local));

    char src_dir[1024];
    snprintf(src_dir, sizeof(src_dir), "%s/%s", exp_local, skill_name);

    char dst_dir[1024];
    snprintf(dst_dir, sizeof(dst_dir), "%s/%s", priv->git_cache_dir, skill_name);

    struct stat st;
    if (stat(src_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        if (cb) cb(1.0f, "本地技能目录不存在", user_data);
        return -1;
    }

    if (cb) cb(0.2f, "准备提交技能到 Git 本地暂存区...", user_data);
    if (ensure_git_cache_ready(priv, NULL, 0) != 0) return -1;

    /* Fetch and rebase first to avoid out-of-date conflicts */
    char sync_cmd[1024];
    snprintf(sync_cmd, sizeof(sync_cmd),
             "cd \"%s\" && git fetch origin \"%s\" 2>&1 && git rebase \"origin/%s\" 2>&1",
             priv->git_cache_dir, priv->branch, priv->branch);
    system(sync_cmd);

    /* Copy local skill to git cache */
    hub_copy_dir_recursive(src_dir, dst_dir);

    if (cb) cb(0.5f, "创建 Git 变更提交...", user_data);
    char commit_cmd[2048];
    snprintf(commit_cmd, sizeof(commit_cmd),
             "cd \"%s\" && git add \"%s\" 2>&1 && "
             "(git diff --cached --quiet || git commit -m \"sync(skill): update %s [AI-Space]\") 2>&1",
             priv->git_cache_dir, skill_name, skill_name);
    system(commit_cmd);

    if (cb) cb(0.8f, "正在推送到远端 Git 仓库...", user_data);
    char push_cmd[1024];
    snprintf(push_cmd, sizeof(push_cmd),
             "cd \"%s\" && git push origin \"%s\" 2>&1",
             priv->git_cache_dir, priv->branch);
    int rc = system(push_cmd);
    if (rc != 0) {
        /* Retry with rebase once */
        snprintf(push_cmd, sizeof(push_cmd),
                 "cd \"%s\" && git pull --rebase origin \"%s\" 2>&1 && git push origin \"%s\" 2>&1",
                 priv->git_cache_dir, priv->branch, priv->branch);
        rc = system(push_cmd);
    }

    if (cb) cb(1.0f, rc == 0 ? "Git 技能推送完成" : "Git 技能推送失败", user_data);
    return rc == 0 ? 0 : -1;
}

static int git_pull_all(sync_backend_t *self, const char *target_local_hub,
                        sync_progress_cb cb, void *user_data) {
    git_priv_t *priv = (git_priv_t *)self->priv_data;
    if (!priv) return -1;

    char exp_local[512];
    expand_path_local(target_local_hub ? target_local_hub : "~/.ai-space/skills", exp_local, sizeof(exp_local));
    AI_MKDIR(exp_local);

    if (cb) cb(0.2f, "正在获取远端 Git 最新版本库...", user_data);
    char fetch_cmd[1024];
    snprintf(fetch_cmd, sizeof(fetch_cmd),
             "cd \"%s\" && git fetch origin \"%s\" 2>&1 && git reset --hard \"origin/%s\" 2>&1",
             priv->git_cache_dir, priv->branch, priv->branch);
    system(fetch_cmd);

    DIR *d = opendir(priv->git_cache_dir);
    if (!d) return -1;

    struct dirent *ent;
    int count = 0;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        if (strcmp(ent->d_name, "node_modules") == 0 || strcmp(ent->d_name, "__pycache__") == 0) continue;

        char s_dir[1024];
        snprintf(s_dir, sizeof(s_dir), "%s/%s", priv->git_cache_dir, ent->d_name);

        struct stat st;
        if (stat(s_dir, &st) == 0 && S_ISDIR(st.st_mode)) {
            char skill_md[1024];
            snprintf(skill_md, sizeof(skill_md), "%s/SKILL.md", s_dir);
            struct stat md_st;
            if (stat(skill_md, &md_st) == 0) {
                char d_dir[1024];
                snprintf(d_dir, sizeof(d_dir), "%s/%s", exp_local, ent->d_name);
                hub_copy_dir_recursive(s_dir, d_dir);
                count++;
            }
        }
    }
    closedir(d);

    char msg[128];
    snprintf(msg, sizeof(msg), "全量拉取完成 (已同步 %d 个技能)", count);
    if (cb) cb(1.0f, msg, user_data);
    return 0;
}

static int git_push_all(sync_backend_t *self, const char *source_local_hub,
                        sync_progress_cb cb, void *user_data) {
    git_priv_t *priv = (git_priv_t *)self->priv_data;
    if (!priv) return -1;

    char exp_local[512];
    expand_path_local(source_local_hub ? source_local_hub : "~/.ai-space/skills", exp_local, sizeof(exp_local));

    if (cb) cb(0.2f, "准备提交所有本地技能...", user_data);
    if (ensure_git_cache_ready(priv, NULL, 0) != 0) return -1;

    DIR *d = opendir(exp_local);
    if (!d) return -1;

    struct dirent *ent;
    int count = 0;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        char s_dir[1024];
        snprintf(s_dir, sizeof(s_dir), "%s/%s", exp_local, ent->d_name);
        struct stat st;
        if (stat(s_dir, &st) == 0 && S_ISDIR(st.st_mode)) {
            char d_dir[1024];
            snprintf(d_dir, sizeof(d_dir), "%s/%s", priv->git_cache_dir, ent->d_name);
            hub_copy_dir_recursive(s_dir, d_dir);
            count++;
        }
    }
    closedir(d);

    if (cb) cb(0.5f, "创建 Git 全量提交...", user_data);
    char commit_cmd[2048];
    snprintf(commit_cmd, sizeof(commit_cmd),
             "cd \"%s\" && git add . 2>&1 && "
             "(git diff --cached --quiet || git commit -m \"sync(all): full sync of %d skills [AI-Space]\") 2>&1",
             priv->git_cache_dir, count);
    system(commit_cmd);

    if (cb) cb(0.8f, "正在推送到远端 Git 仓库...", user_data);
    char push_cmd[1024];
    snprintf(push_cmd, sizeof(push_cmd),
             "cd \"%s\" && git push origin \"%s\" 2>&1",
             priv->git_cache_dir, priv->branch);
    int rc = system(push_cmd);
    if (rc != 0) {
        snprintf(push_cmd, sizeof(push_cmd),
                 "cd \"%s\" && git pull --rebase origin \"%s\" 2>&1 && git push origin \"%s\" 2>&1",
                 priv->git_cache_dir, priv->branch, priv->branch);
        rc = system(push_cmd);
    }

    if (cb) cb(1.0f, rc == 0 ? "Git 全量推送完成" : "Git 全量推送失败", user_data);
    return rc == 0 ? 0 : -1;
}

const sync_driver_ops_t g_git_driver_ops = {
    .type = SYNC_BACKEND_GIT,
    .name = "git",
    .display_name = "Git 远端版本库",
    .init = git_init,
    .destroy = git_destroy,
    .test_connection = git_test_connection,
    .check_available = git_check_available,
    .fetch_remote_manifest = git_fetch_remote_manifest,
    .check_drift = git_check_drift,
    .pull_skill = git_pull_skill,
    .push_skill = git_push_skill,
    .pull_all = git_pull_all,
    .push_all = git_push_all,
};
