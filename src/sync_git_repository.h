#ifndef SYNC_GIT_REPOSITORY_H
#define SYNC_GIT_REPOSITORY_H

#include "sync_backend.h"
#include <stdio.h>

typedef struct {
    char repo_url[512];
    char branch[64];
    char auth_token[256];
    char user_name[128];
    char user_email[128];
    char git_cache_dir[512]; /* Expanded path to local git tracking directory */
    FILE *cache_lock;
} git_priv_t;

int git_repo_configure(git_priv_t *repo, const sync_config_t *cfg);
void git_repo_release(git_priv_t *repo);
int git_repo_check_available(const git_priv_t *repo, char *error, size_t cap);
enum { GIT_REPO_EMPTY = 1 };
int git_repo_refresh(git_priv_t *repo, char *error, size_t cap);
int git_repo_open_cache(git_priv_t *repo, char *error, size_t cap);
int git_repo_head(const char *dir, char out[SYNC_GIT_OID_CAP]);
int git_repo_commit_message(const git_priv_t *repo, const char *dir, const char *message);
int git_repo_push_from(const git_priv_t *repo, const char *dir);
int git_repo_stage(const git_priv_t *repo, const char *skill_name);
int git_repo_commit(const git_priv_t *repo, const char *skill_name, int count);
void git_repo_restore(const git_priv_t *repo, int revision);
int git_repo_push(const git_priv_t *repo);

#endif
