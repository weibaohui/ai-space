#include "file_tree.h"
#include "portability.h"

#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <sys/utime.h>
#else
#include <utime.h>
#endif

bool sync_valid_component(const char *name) {
    if (!name || !name[0] || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return false;
    for (const unsigned char *p = (const unsigned char *)name; *p; p++) {
        if (*p < 32 || *p == 127 || strchr("/\\:", *p)) return false;
    }
    return true;
}

int sync_join(char *out, size_t cap, const char *parent, const char *name) {
    int n = snprintf(out, cap, "%s/%s", parent, name);
    return n >= 0 && (size_t)n < cap ? 0 : -1;
}

int sync_expand(const char *path, char *out, size_t cap) {
    if (!path || !path[0]) return -1;
    int n;
    if (path[0] == '~' && (path[1] == '/' || path[1] == '\0')) {
        const char *home = ai_home_dir();
        if (!home || !home[0]) return -1;
        n = snprintf(out, cap, "%s%s", home, path + 1);
    } else {
        n = snprintf(out, cap, "%s", path);
    }
    return n >= 0 && (size_t)n < cap ? 0 : -1;
}

int sync_make_stage(const char *root, char *out, size_t cap) {
    /* 放在目标目录所在的文件系统，允许后续通过 rename 替换。 */
    for (int attempt = 0; attempt < 128; attempt++) {
        int n = snprintf(out, cap, "%s/.skillboard-stage-%ld-%d", root, (long)getpid(), attempt);
        if (n < 0 || (size_t)n >= cap) return -1;
#ifdef _WIN32
        if (AI_MKDIR(out) == 0) return 0;
#else
        if (mkdir(out, 0700) == 0) return 0;
#endif
        if (errno != EEXIST) return -1;
    }
    return -1;
}

int sync_make_temp_stage(char *out, size_t cap) {
    const char *temp = getenv("TMPDIR");
#ifdef _WIN32
    char system_temp[SYNC_PATH_CAP];
    if (!temp || !temp[0]) {
        DWORD len = GetTempPathA((DWORD)sizeof(system_temp), system_temp);
        if (!len || len >= sizeof(system_temp)) return -1;
        temp = system_temp;
    }
#else
    if (!temp || !temp[0]) temp = "/tmp";
#endif
    return sync_make_stage(temp, out, cap);
}

void sync_cleanup_stage(const char *stage, int result) {
    if (result != SYNC_REPLACE_RESTORE_FAILED) sync_remove_tree(stage);
}

int sync_remove_tree(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0) return errno == ENOENT ? 0 : -1;
    if (!S_ISDIR(st.st_mode)) return remove(path);
    DIR *dir = opendir(path);
    if (!dir) return -1;

    int rc = 0;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        char child[SYNC_PATH_CAP];
        if (sync_join(child, sizeof(child), path, entry->d_name) != 0 ||
            sync_remove_tree(child) != 0) rc = -1;
    }
    closedir(dir);
#ifdef _WIN32
    if (_rmdir(path) != 0) rc = -1;
#else
    if (rmdir(path) != 0) rc = -1;
#endif
    return rc;
}

int sync_set_mtime(const char *path, time_t mtime) {
#ifdef _WIN32
    struct _utimbuf times = {mtime, mtime};
    return _utime(path, &times);
#else
    struct utimbuf times = {mtime, mtime};
    return utime(path, &times);
#endif
}

static int copy_file(const char *source, const char *target) {
    FILE *in = fopen(source, "rb");
    if (!in) return -1;
    FILE *out = fopen(target, "wb");
    if (!out) {
        fclose(in);
        return -1;
    }

    char buf[16384];
    size_t len;
    int rc = 0;
    while ((len = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, len, out) != len) {
            rc = -1;
            break;
        }
    }
    if (ferror(in)) rc = -1;
    fclose(in);
    if (fclose(out) != 0) rc = -1;
    return rc;
}

static int copy_tree(const char *source, const char *target, int depth, bool merge, file_tree_skip_fn skip) {
    struct stat st;
    if (depth >= SYNC_TREE_DEPTH || lstat(source, &st) != 0) return -1;
    if (S_ISREG(st.st_mode)) {
        if (merge) {
            struct stat existing;
            if (lstat(target, &existing) == 0) {
                if (!S_ISREG(existing.st_mode)) return -1;
            } else if (errno != ENOENT) {
                return -1;
            }
        }
        if (copy_file(source, target) != 0) return -1;
        return sync_set_mtime(target, st.st_mtime);
    }
    if (!S_ISDIR(st.st_mode)) return -1;

    DIR *dir = opendir(source);
    if (!dir) return -1;
    int rc = AI_MKDIR(target);
    if (rc != 0 && merge && errno == EEXIST) {
        struct stat existing;
        if (lstat(target, &existing) == 0 && S_ISDIR(existing.st_mode)) rc = 0;
    }
    while (rc == 0) {
        errno = 0;
        struct dirent *entry = readdir(dir);
        if (!entry) {
            if (errno) rc = -1;
            break;
        }
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        if (skip && skip(entry->d_name)) continue;
        char src[SYNC_PATH_CAP], dst[SYNC_PATH_CAP];
        if (!sync_valid_component(entry->d_name) ||
            sync_join(src, sizeof(src), source, entry->d_name) != 0 ||
            sync_join(dst, sizeof(dst), target, entry->d_name) != 0 ||
            copy_tree(src, dst, depth + 1, merge, skip) != 0) rc = -1;
    }
    closedir(dir);
    if (rc == 0) rc = sync_set_mtime(target, st.st_mtime);
    return rc;
}

int file_tree_copy(const char *source, const char *target, bool merge, file_tree_skip_fn skip) {
    return copy_tree(source, target, 0, merge, skip);
}

int sync_replace_tree(const char *stage, const char *replacement, const char *target) {
    char old[SYNC_PATH_CAP];
    if (sync_join(old, sizeof(old), stage, "previous") != 0) return -1;

    struct stat st;
    bool backed_up = lstat(target, &st) == 0;
    if (backed_up) {
        if (!S_ISDIR(st.st_mode) || rename(target, old) != 0) return -1;
    } else if (errno != ENOENT) {
        return -1;
    }

    if (rename(replacement, target) == 0) return 0;
    /* 回滚失败时保留 previous，避免清理暂存目录时丢掉旧版本。 */
    if (backed_up && rename(old, target) != 0) return SYNC_REPLACE_RESTORE_FAILED;
    return -1;
}
