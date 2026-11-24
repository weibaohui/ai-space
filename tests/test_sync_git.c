#include "skill_fingerprint.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "sync_backend.h"

static void print_usage(const char *prog) {
    printf("Usage:\n");
    printf("  %s check-available <repo_url> <branch> [token]\n", prog);
    printf("  %s drift <repo_url> <branch> <local_hub_dir> <git_cache_dir> [token]\n", prog);
    printf("  %s push-all <repo_url> <branch> <local_hub_dir> <git_cache_dir> [token]\n", prog);
    printf("  %s push-skill <repo_url> <branch> <local_hub_dir> <git_cache_dir> <skill_name> [token]\n", prog);
    printf("  %s pull-all <repo_url> <branch> <local_hub_dir> <git_cache_dir> [token]\n", prog);
    printf("  %s pull-skill <repo_url> <branch> <local_hub_dir> <git_cache_dir> <skill_name> [token]\n", prog);
    printf("  %s hash <skill_dir>\n", prog);
}

static void setup_git_config(sync_config_t *cfg, const char *url, const char *branch,
                            const char *cache_dir, const char *token) {
    memset(cfg, 0, sizeof(sync_config_t));
    cfg->type = SYNC_BACKEND_GIT;
    strncpy(cfg->git_repo_url, url, sizeof(cfg->git_repo_url) - 1);
    strncpy(cfg->git_branch, branch ? branch : "main", sizeof(cfg->git_branch) - 1);
    if (cache_dir) strncpy(cfg->git_cache_dir, cache_dir, sizeof(cfg->git_cache_dir) - 1);
    if (token) strncpy(cfg->git_auth_token, token, sizeof(cfg->git_auth_token) - 1);
    strncpy(cfg->git_user_name, "AI-Space Sync Tester", sizeof(cfg->git_user_name) - 1);
    strncpy(cfg->git_user_email, "tester@ai-space.local", sizeof(cfg->git_user_email) - 1);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "check-available") == 0) {
        if (argc < 4) {
            print_usage(argv[0]);
            return 1;
        }
        const char *url = argv[2];
        const char *branch = argv[3];
        const char *token = (argc >= 5) ? argv[4] : NULL;

        sync_config_t cfg;
        setup_git_config(&cfg, url, branch, NULL, token);

        char err[512] = "";
        int rc = sync_backend_check_available(&cfg, err, sizeof(err));
        if (rc == 0) {
            printf("AVAILABILITY_OK: %s\n", err);
            return 0;
        } else {
            printf("AVAILABILITY_FAILED: %s\n", err);
            return 1;
        }
    }

    if (strcmp(cmd, "hash") == 0) {
        if (argc < 3) {
            print_usage(argv[0]);
            return 1;
        }
        char hash[64] = "";
        time_t mtime = 0;
        int rc = hub_compute_skill_hash(argv[2], hash, sizeof(hash), &mtime);
        if (rc == 0) {
            printf("HASH: %s (mtime: %ld)\n", hash, (long)mtime);
            return 0;
        } else {
            printf("HASH_FAILED: cannot compute hash for %s\n", argv[2]);
            return 1;
        }
    }

    if (strcmp(cmd, "drift") == 0) {
        if (argc < 6) {
            print_usage(argv[0]);
            return 1;
        }
        const char *url = argv[2];
        const char *branch = argv[3];
        const char *local_hub = argv[4];
        const char *cache_dir = argv[5];
        const char *token = (argc >= 7) ? argv[6] : NULL;

        sync_config_t cfg;
        setup_git_config(&cfg, url, branch, cache_dir, token);

        sync_backend_t *backend = NULL;
        if (sync_backend_create(&cfg, &backend) != 0 || !backend) {
            printf("DRIFT_FAILED: Failed to create sync backend\n");
            return 1;
        }

        sync_drift_summary_t diff;
        char err[512] = "";
        int rc = backend->ops->check_drift(backend, local_hub, &diff, err, sizeof(err));
        if (rc != 0) {
            printf("DRIFT_FAILED: %s\n", err);
            sync_backend_destroy(backend);
            return 1;
        }

        printf("DRIFT_OK: count=%d in_sync=%d remote_ahead=%d local_ahead=%d remote_only=%d local_only=%d conflict=%d\n",
               diff.count, diff.in_sync_count, diff.remote_ahead_count, diff.local_ahead_count,
               diff.remote_only_count, diff.local_only_count, diff.conflict_count);

        for (int i = 0; i < diff.count; i++) {
            sync_drift_item_t *it = &diff.items[i];
            const char *status_str = "UNKNOWN";
            switch (it->status) {
                case SYNC_DRIFT_IN_SYNC: status_str = "IN_SYNC"; break;
                case SYNC_DRIFT_REMOTE_AHEAD: status_str = "REMOTE_AHEAD"; break;
                case SYNC_DRIFT_LOCAL_AHEAD: status_str = "LOCAL_AHEAD"; break;
                case SYNC_DRIFT_REMOTE_ONLY: status_str = "REMOTE_ONLY"; break;
                case SYNC_DRIFT_LOCAL_ONLY: status_str = "LOCAL_ONLY"; break;
                case SYNC_DRIFT_CONFLICT: status_str = "CONFLICT"; break;
            }
            printf("  ITEM: name=%s status=%s local_hash=%s remote_hash=%s desc=%s\n",
                   it->skill_name, status_str, it->local_hash, it->remote_hash, it->status_desc);
        }

        sync_free_drift_summary(&diff);
        sync_backend_destroy(backend);
        return 0;
    }

    if (strcmp(cmd, "push-all") == 0) {
        if (argc < 6) {
            print_usage(argv[0]);
            return 1;
        }
        const char *url = argv[2];
        const char *branch = argv[3];
        const char *local_hub = argv[4];
        const char *cache_dir = argv[5];
        const char *token = (argc >= 7) ? argv[6] : NULL;

        sync_config_t cfg;
        setup_git_config(&cfg, url, branch, cache_dir, token);

        sync_backend_t *backend = NULL;
        if (sync_backend_create(&cfg, &backend) != 0 || !backend) {
            printf("PUSH_ALL_FAILED: Failed to create sync backend\n");
            return 1;
        }

        int rc = backend->ops->push_all(backend, local_hub, NULL, NULL);
        if (rc == 0) {
            printf("PUSH_ALL_OK\n");
        } else {
            printf("PUSH_ALL_FAILED\n");
        }
        sync_backend_destroy(backend);
        return rc == 0 ? 0 : 1;
    }

    if (strcmp(cmd, "push-skill") == 0) {
        if (argc < 7) {
            print_usage(argv[0]);
            return 1;
        }
        const char *url = argv[2];
        const char *branch = argv[3];
        const char *local_hub = argv[4];
        const char *cache_dir = argv[5];
        const char *skill_name = argv[6];
        const char *token = (argc >= 8) ? argv[7] : NULL;

        sync_config_t cfg;
        setup_git_config(&cfg, url, branch, cache_dir, token);

        sync_backend_t *backend = NULL;
        if (sync_backend_create(&cfg, &backend) != 0 || !backend) {
            printf("PUSH_SKILL_FAILED: Failed to create sync backend\n");
            return 1;
        }

        int rc = backend->ops->push_skill(backend, skill_name, local_hub, NULL, NULL);
        if (rc == 0) {
            printf("PUSH_SKILL_OK: %s\n", skill_name);
        } else {
            printf("PUSH_SKILL_FAILED: %s\n", skill_name);
        }
        sync_backend_destroy(backend);
        return rc == 0 ? 0 : 1;
    }

    if (strcmp(cmd, "pull-all") == 0) {
        if (argc < 6) {
            print_usage(argv[0]);
            return 1;
        }
        const char *url = argv[2];
        const char *branch = argv[3];
        const char *local_hub = argv[4];
        const char *cache_dir = argv[5];
        const char *token = (argc >= 7) ? argv[6] : NULL;

        sync_config_t cfg;
        setup_git_config(&cfg, url, branch, cache_dir, token);

        sync_backend_t *backend = NULL;
        if (sync_backend_create(&cfg, &backend) != 0 || !backend) {
            printf("PULL_ALL_FAILED: Failed to create sync backend\n");
            return 1;
        }

        int rc = backend->ops->pull_all(backend, local_hub, NULL, NULL);
        if (rc == 0) {
            printf("PULL_ALL_OK\n");
        } else {
            printf("PULL_ALL_FAILED\n");
        }
        sync_backend_destroy(backend);
        return rc == 0 ? 0 : 1;
    }

    if (strcmp(cmd, "pull-skill") == 0) {
        if (argc < 7) {
            print_usage(argv[0]);
            return 1;
        }
        const char *url = argv[2];
        const char *branch = argv[3];
        const char *local_hub = argv[4];
        const char *cache_dir = argv[5];
        const char *skill_name = argv[6];
        const char *token = (argc >= 8) ? argv[7] : NULL;

        sync_config_t cfg;
        setup_git_config(&cfg, url, branch, cache_dir, token);

        sync_backend_t *backend = NULL;
        if (sync_backend_create(&cfg, &backend) != 0 || !backend) {
            printf("PULL_SKILL_FAILED: Failed to create sync backend\n");
            return 1;
        }

        int rc = backend->ops->pull_skill(backend, skill_name, local_hub, NULL, NULL);
        if (rc == 0) {
            printf("PULL_SKILL_OK: %s\n", skill_name);
        } else {
            printf("PULL_SKILL_FAILED: %s\n", skill_name);
        }
        sync_backend_destroy(backend);
        return rc == 0 ? 0 : 1;
    }

    print_usage(argv[0]);
    return 1;
}
