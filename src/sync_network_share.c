#include "skill_fingerprint.h"
#include "skill_metadata.h"
#include "sync_network_share.h"
#include "sync_directory.h"

#ifdef _WIN32
#include <winsock2.h>
#endif

#include "portability.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <nfsc/libnfs.h>
#include <smb2/smb2.h>
#include <smb2/libsmb2.h>

typedef struct {
    struct nfs_context *nfs;
    struct smb2_context *smb;
    char root[SYNC_PATH_CAP];
#ifdef _WIN32
    bool winsock_ready;
#endif
} share_t;

typedef enum { ENTRY_OTHER, ENTRY_FILE, ENTRY_DIRECTORY } entry_kind_t;

typedef struct {
    entry_kind_t kind;
    time_t mtime;
} entry_info_t;

typedef struct {
    struct nfsdir *nfs;
    struct smb2dir *smb;
} share_dir_t;

typedef struct {
    struct nfsfh *nfs;
    struct smb2fh *smb;
} share_file_t;

static void share_close(share_t *share) {
    if (share->smb) smb2_destroy_context(share->smb);
    if (share->nfs) nfs_destroy_context(share->nfs);
#ifdef _WIN32
    if (share->winsock_ready) WSACleanup();
#endif
}

static const char *share_error(share_t *share) {
    const char *message = share->smb ? smb2_get_error(share->smb)
                                    : share->nfs ? nfs_get_error(share->nfs) : NULL;
    return message && message[0] ? message : "请检查共享路径、网络和访问权限";
}

static bool valid_path(const char *path) {
    while (*path) {
        size_t len = strcspn(path, "/");
        char component[256];
        if (!len || len >= sizeof(component)) return false;
        memcpy(component, path, len);
        component[len] = '\0';
        if (!sync_valid_component(component)) return false;
        path += len;
        if (*path == '/') {
            path++;
            if (!*path) return false;
        }
    }
    return true;
}

/* Connect a fresh protocol context for each operation. NFS paths begin with /;
 * SMB paths are relative to the connected share. */
static int share_connect(share_t *share, const sync_config_t *cfg, char *error, size_t cap) {
    bool nfs = cfg->share_mode == SYNC_SHARE_NFS;
    bool smb = cfg->share_mode == SYNC_SHARE_SMB;
    const char *server = cfg->share_server;
    const char *export_path = cfg->share_export;
    if ((!nfs && !smb) || !server[0] || strpbrk(server, " /\\@?#\t\r\n") ||
        !export_path[0] || (nfs && (export_path[0] != '/' || !valid_path(export_path + 1))) ||
        (smb && !sync_valid_component(export_path)) || !valid_path(cfg->share_subdir) ||
        (smb && !cfg->smb_user[0] && cfg->smb_pass[0])) {
        snprintf(error, cap, "请检查服务器、导出路径或共享名，以及同步子目录的格式");
        return -1;
    }

    snprintf(share->root, sizeof(share->root), "%s%s", nfs ? "/" : "", cfg->share_subdir);
#ifdef _WIN32
    WSADATA socket_data;
    if (WSAStartup(MAKEWORD(2, 2), &socket_data) != 0) {
        snprintf(error, cap, "无法初始化网络连接");
        return -1;
    }
    share->winsock_ready = true;
#endif

    int rc = -1;
    if (nfs) {
        share->nfs = nfs_init_context();
        if (share->nfs) {
            nfs_set_version(share->nfs, 3);
            nfs_set_timeout(share->nfs, 5000);
            nfs_set_retrans(share->nfs, 0);
            nfs_set_autoreconnect(share->nfs, 0);
            nfs_set_dircache(share->nfs, 0);
            nfs_set_auto_traverse_mounts(share->nfs, 0);
            rc = nfs_mount(share->nfs, server, export_path);
        }
    } else {
        share->smb = smb2_init_context();
        if (share->smb) {
            smb2_set_timeout(share->smb, 5);
            smb2_set_authentication(share->smb, SMB2_SEC_NTLMSSP);
            const char *user = cfg->smb_user[0] ? cfg->smb_user : "guest";
            smb2_set_user(share->smb, user);
            smb2_set_password(share->smb, cfg->smb_user[0] ? cfg->smb_pass : NULL);
            rc = smb2_connect_share(share->smb, server, export_path, NULL);
        }
    }
    if (rc != 0)
        snprintf(error, cap, "%s 连接失败：%.180s", nfs ? "NFS" : "SMB", share_error(share));
    return rc;
}

static int share_path(char *out, const char *parent, const char *name) {
    const char *sep = parent[0] && strcmp(parent, "/") != 0 ? "/" : "";
    int n = snprintf(out, SYNC_PATH_CAP, "%s%s%s", parent, sep, name);
    return n >= 0 && n < SYNC_PATH_CAP ? 0 : -1;
}

static int share_stat(share_t *share, const char *path, entry_info_t *info) {
    if (share->nfs) {
        struct nfs_stat_64 st;
        int rc = nfs_lstat64(share->nfs, path, &st);
        if (rc != 0) return rc;
        info->kind = S_ISREG(st.nfs_mode) ? ENTRY_FILE :
                     S_ISDIR(st.nfs_mode) ? ENTRY_DIRECTORY : ENTRY_OTHER;
        info->mtime = (time_t)st.nfs_mtime;
    } else {
        struct smb2_stat_64 st;
        int rc = smb2_stat(share->smb, path, &st);
        if (rc != 0) return rc;
        info->kind = st.smb2_type == SMB2_TYPE_FILE ? ENTRY_FILE :
                     st.smb2_type == SMB2_TYPE_DIRECTORY ? ENTRY_DIRECTORY : ENTRY_OTHER;
        if (st.smb2_attributes & SMB2_FILE_ATTRIBUTE_REPARSE_POINT) info->kind = ENTRY_OTHER;
        info->mtime = (time_t)st.smb2_mtime;
    }
    return 0;
}

static int share_opendir(share_t *share, const char *path, share_dir_t *dir) {
    if (share->nfs) return nfs_opendir(share->nfs, path, &dir->nfs);
    dir->smb = smb2_opendir(share->smb, path);
    return dir->smb ? 0 : -1;
}

static const char *share_readdir(share_t *share, share_dir_t *dir) {
    if (share->nfs) {
        struct nfsdirent *entry = nfs_readdir(share->nfs, dir->nfs);
        return entry ? entry->name : NULL;
    }
    struct smb2dirent *entry = smb2_readdir(share->smb, dir->smb);
    return entry ? entry->name : NULL;
}

static void share_closedir(share_t *share, share_dir_t *dir) {
    if (share->nfs) nfs_closedir(share->nfs, dir->nfs);
    else smb2_closedir(share->smb, dir->smb);
}

static int share_mkdir(share_t *share, const char *path, bool staging) {
    if (share->nfs) return nfs_mkdir2(share->nfs, path, staging ? 0700 : 0755);
    return smb2_mkdir(share->smb, path);
}

static int share_rename(share_t *share, const char *from, const char *to) {
    return share->nfs ? nfs_rename(share->nfs, from, to) : smb2_rename(share->smb, from, to);
}

static int share_remove(share_t *share, const char *path, int depth) {
    entry_info_t info;
    int rc = share_stat(share, path, &info);
    if (rc == -ENOENT) return 0;
    if (rc != 0 || depth >= SYNC_TREE_DEPTH + 2) return -1;
    if (info.kind != ENTRY_DIRECTORY)
        return share->nfs ? nfs_unlink(share->nfs, path) : smb2_unlink(share->smb, path);

    share_dir_t dir;
    if (share_opendir(share, path, &dir) != 0) return -1;
    const char *name;
    while ((name = share_readdir(share, &dir))) {
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        char child[SYNC_PATH_CAP];
        if (!sync_valid_component(name) || share_path(child, path, name) != 0 ||
            share_remove(share, child, depth + 1) != 0) {
            rc = -1;
            break;
        }
    }
    share_closedir(share, &dir);
    if (rc != 0) return rc;
    return share->nfs ? nfs_rmdir(share->nfs, path) : smb2_rmdir(share->smb, path);
}

static int share_file_open(share_t *share, const char *path, int flags, share_file_t *file) {
    if (share->nfs) return nfs_open2(share->nfs, path, flags, 0644, &file->nfs);
    file->smb = smb2_open(share->smb, path, flags);
    return file->smb ? 0 : -1;
}

static int share_file_close(share_t *share, share_file_t *file) {
    return share->nfs ? nfs_close(share->nfs, file->nfs) : smb2_close(share->smb, file->smb);
}

static int share_read(share_t *share, share_file_t *file, char *buf, unsigned size) {
    return share->nfs ? nfs_read(share->nfs, file->nfs, buf, size)
                      : smb2_read(share->smb, file->smb, (uint8_t *)buf, size);
}

static int share_write(share_t *share, share_file_t *file, const char *buf, unsigned size) {
    while (size) {
        int n = share->nfs ? nfs_write(share->nfs, file->nfs, buf, size)
                           : smb2_write(share->smb, file->smb, (const uint8_t *)buf, size);
        if (n <= 0 || (unsigned)n > size) return -1;
        size -= (unsigned)n;
        buf += n;
    }
    return 0;
}

static int share_mtime(share_t *share, const char *path, time_t mtime, bool dir) {
    if (share->nfs) {
        struct timeval times[2] = {{mtime, 0}, {mtime, 0}};
        return nfs_utimes(share->nfs, path, times);
    }
    share_file_t file;
    if (share_file_open(share, path, O_WRONLY | (dir ? SMB2_O_DIRECTORY : 0), &file) != 0) return -1;
    int rc = smb2_set_mtime(share->smb, file.smb, mtime);
    if (share_file_close(share, &file) != 0) rc = -1;
    return rc;
}

static int share_stage(share_t *share, char *stage) {
    for (int attempt = 0; attempt < 128; attempt++) {
        char name[96];
        snprintf(name, sizeof(name), ".ai-space-stage-%lld-%ld-%d",
                 (long long)time(NULL), (long)getpid(), attempt);
        if (share_path(stage, share->root, name) != 0) return -1;
        int rc = share_mkdir(share, stage, true);
        if (rc != -EEXIST) return rc;
    }
    return -1;
}

static int copy_remote_file(share_t *share, const char *remote, const char *local, bool pull) {
    FILE *fp = fopen(local, pull ? "wb" : "rb");
    if (!fp) return -1;
    share_file_t file;
    int rc = share_file_open(share, remote, pull ? O_RDONLY : O_WRONLY | O_CREAT | O_EXCL, &file);
    if (rc == 0) {
        char buf[32768];
        if (pull) {
            int n;
            while ((n = share_read(share, &file, buf, sizeof(buf))) > 0) {
                if (fwrite(buf, 1, (size_t)n, fp) != (size_t)n) {
                    rc = -1;
                    break;
                }
            }
            if (n < 0) rc = -1;
        } else {
            size_t n;
            while (rc == 0 && (n = fread(buf, 1, sizeof(buf), fp)) > 0)
                rc = share_write(share, &file, buf, (unsigned)n);
            if (ferror(fp)) rc = -1;
            if (rc == 0)
                rc = share->nfs ? nfs_fsync(share->nfs, file.nfs) : smb2_fsync(share->smb, file.smb);
        }
        if (share_file_close(share, &file) != 0) rc = -1;
    }
    if (fclose(fp) != 0) rc = -1;
    return rc;
}

static int download_tree(share_t *share, const char *remote, const char *local, int depth) {
    entry_info_t info;
    if (depth >= SYNC_TREE_DEPTH || share_stat(share, remote, &info) != 0) return -1;
    if (info.kind == ENTRY_FILE) {
        if (copy_remote_file(share, remote, local, true) != 0) return -1;
        return sync_set_mtime(local, info.mtime);
    }
    if (info.kind != ENTRY_DIRECTORY || AI_MKDIR(local) != 0) return -1;
    share_dir_t dir;
    if (share_opendir(share, remote, &dir) != 0) return -1;
    int rc = 0;
    const char *name;
    while ((name = share_readdir(share, &dir))) {
        if (skill_skip_name(name)) continue;
        char remote_child[SYNC_PATH_CAP], local_child[SYNC_PATH_CAP];
        if (!sync_valid_component(name) || share_path(remote_child, remote, name) != 0 ||
            sync_join(local_child, sizeof(local_child), local, name) != 0 ||
            download_tree(share, remote_child, local_child, depth + 1) != 0) {
            rc = -1;
            break;
        }
    }
    share_closedir(share, &dir);
    return rc == 0 ? sync_set_mtime(local, info.mtime) : rc;
}

static int upload_tree(share_t *share, const char *remote, const char *local, int depth) {
    struct stat st;
    if (depth >= SYNC_TREE_DEPTH || lstat(local, &st) != 0) return -1;
    if (S_ISREG(st.st_mode)) {
        if (copy_remote_file(share, remote, local, false) != 0) return -1;
        return share_mtime(share, remote, st.st_mtime, false);
    }
    if (!S_ISDIR(st.st_mode) || share_mkdir(share, remote, false) != 0) return -1;
    DIR *dir = opendir(local);
    if (!dir) return -1;
    int rc = 0;
    while (rc == 0) {
        errno = 0;
        struct dirent *entry = readdir(dir);
        if (!entry) {
            if (errno) rc = -1;
            break;
        }
        if (skill_skip_name(entry->d_name)) continue;
        char remote_child[SYNC_PATH_CAP], local_child[SYNC_PATH_CAP];
        if (!sync_valid_component(entry->d_name) ||
            share_path(remote_child, remote, entry->d_name) != 0 ||
            sync_join(local_child, sizeof(local_child), local, entry->d_name) != 0) rc = -1;
        else rc = upload_tree(share, remote_child, local_child, depth + 1);
    }
    closedir(dir);
    return rc == 0 ? share_mtime(share, remote, st.st_mtime, true) : rc;
}

/* Return 1 for a skill, 0 for an unrelated entry, and -1 for an I/O error. */
static int share_has_skill(share_t *share, const char *path) {
    entry_info_t info;
    int rc = share_stat(share, path, &info);
    if (rc == -ENOENT) return 0;
    if (rc != 0) return -1;
    if (info.kind != ENTRY_DIRECTORY) return 0;
    char doc[SYNC_PATH_CAP];
    if (share_path(doc, path, SKILL_DOCUMENT) != 0) return -1;
    rc = share_stat(share, doc, &info);
    if (rc == -ENOENT) return 0;
    if (rc != 0) return -1;
    return info.kind == ENTRY_FILE;
}

static int share_replace(share_t *share, const char *stage, const char *replacement, const char *target) {
    char previous[SYNC_PATH_CAP];
    if (share_path(previous, stage, "previous") != 0) return -2;
    entry_info_t info;
    int rc = share_stat(share, target, &info);
    bool backed_up = rc == 0;
    if (backed_up) {
        if (info.kind != ENTRY_DIRECTORY) return -1;
        /* A lost reply can hide a successful rename. Retain the stage when
         * the server cannot confirm where the previous copy lives. */
        if (share_rename(share, target, previous) != 0) {
            return share_stat(share, previous, &info) == -ENOENT ? -1 : -2;
        }
    } else if (rc != -ENOENT) {
        return -1;
    }

    if (share_rename(share, replacement, target) == 0) return 0;
    if (backed_up && share_rename(share, previous, target) != 0) return -2;
    return -1;
}

static int test_share_rw(share_t *share) {
    char stage[SYNC_PATH_CAP], probe[SYNC_PATH_CAP], renamed[SYNC_PATH_CAP];
    if (share_stage(share, stage) != 0) return -1;
    int rc = -1;
    if (share_path(probe, stage, "probe") != 0 || share_path(renamed, stage, "renamed") != 0) goto done;

    share_file_t file;
    if (share_file_open(share, probe, O_WRONLY | O_CREAT | O_EXCL, &file) != 0) goto done;
    const char value[] = "ai-space";
    int written = share_write(share, &file, value, sizeof(value));
    int closed = share_file_close(share, &file);
    if (written != 0 || closed != 0 || share_rename(share, probe, renamed) != 0) goto done;

    if (share_file_open(share, renamed, O_RDONLY, &file) != 0) goto done;
    char buf[sizeof(value)];
    int read = share_read(share, &file, buf, sizeof(buf));
    closed = share_file_close(share, &file);
    if (read == sizeof(buf) && closed == 0 && memcmp(buf, value, sizeof(buf)) == 0) rc = 0;

done:
    if (share_remove(share, stage, 0) != 0) rc = -1;
    return rc;
}

int sync_network_check(const sync_config_t *cfg, char *error, size_t cap) {
    share_t share = {0};
    char message[256] = "";
    int rc = share_connect(&share, cfg, message, sizeof(message));
    if (rc == 0) {
        rc = test_share_rw(&share);
        if (rc == 0) snprintf(message, sizeof(message), "已验证共享目录读写");
        else snprintf(message, sizeof(message), "共享目录读写测试失败：%.180s", share_error(&share));
    }
    share_close(&share);
    if (error) snprintf(error, cap, "%s", message);
    return rc;
}

static int scan_share(share_t *share, const char *stage, sync_remote_manifest_t *manifest) {
    share_dir_t dir;
    if (share_opendir(share, share->root, &dir) != 0) return -1;
    int rc = 0;
    size_t capacity = 0;
    const char *name;
    while ((name = share_readdir(share, &dir))) {
        if (skill_skip_name(name)) continue;
        char remote[SYNC_PATH_CAP], local[SYNC_PATH_CAP];
        if (!sync_valid_component(name) || share_path(remote, share->root, name) != 0) {
            rc = -1;
            break;
        }
        int is_skill = share_has_skill(share, remote);
        if (is_skill == 0) continue;
        if (is_skill < 0 || strlen(name) >= sizeof(manifest->items[0].skill_name) ||
            sync_join(local, sizeof(local), stage, "skill") != 0 ||
            download_tree(share, remote, local, 0) != 0) {
            rc = -1;
            break;
        }
        if ((size_t)manifest->count == capacity) {
            capacity = capacity ? capacity * 2 : 16;
            void *items = realloc(manifest->items, capacity * sizeof(*manifest->items));
            if (!items) {
                rc = -1;
                break;
            }
            manifest->items = items;
        }
        sync_remote_skill_item_t *item = &manifest->items[manifest->count++];
        *item = (sync_remote_skill_item_t){0};
        snprintf(item->skill_name, sizeof(item->skill_name), "%s", name);
        if (hub_compute_skill_hash(local, item->hash, sizeof(item->hash), &item->mtime) != 0 ||
            sync_remove_tree(local) != 0) {
            rc = -1;
            break;
        }
    }
    share_closedir(share, &dir);
    return rc;
}

int sync_network_fetch(const sync_config_t *cfg, sync_remote_manifest_t *manifest,
                       char *error, size_t cap) {
    *manifest = (sync_remote_manifest_t){0};
    share_t share = {0};
    char message[256] = "";
    int rc = share_connect(&share, cfg, message, sizeof(message));
    if (rc == 0) {
        const char *temp = getenv("TMPDIR");
#ifdef _WIN32
        if (!temp || !*temp) temp = getenv("TEMP");
#else
        if (!temp || !*temp) temp = "/tmp";
#endif
        char stage[SYNC_PATH_CAP];
        if (temp && sync_make_stage(temp, stage, sizeof(stage)) == 0) {
            rc = scan_share(&share, stage, manifest);
            if (sync_remove_tree(stage) != 0) rc = -1;
            if (rc != 0) snprintf(message, sizeof(message), "无法完整读取共享目录：%.180s", share_error(&share));
        } else {
            rc = -1;
            snprintf(message, sizeof(message), "无法创建本地校验临时目录");
        }
    }
    share_close(&share);
    if (rc != 0) {
        sync_free_manifest(manifest);
        if (error) snprintf(error, cap, "%s", message);
    } else {
        manifest->fetched_at = time(NULL);
        snprintf(manifest->remote_root, sizeof(manifest->remote_root), "%.140s%s%.220s%s%.145s",
                 cfg->share_server, cfg->share_mode == SYNC_SHARE_NFS ? ":" : "/",
                 cfg->share_export, cfg->share_subdir[0] ? "/" : "", cfg->share_subdir);
    }
    return rc;
}

static bool remote_skill_matches(share_t *share, const char *remote, const char *hash) {
    entry_info_t info;
    if (!hash[0]) return share_stat(share, remote, &info) == -ENOENT;
    if (share_has_skill(share, remote) != 1) return false;
    const char *temp = getenv("TMPDIR");
#ifdef _WIN32
    if (!temp || !*temp) temp = getenv("TEMP");
#else
    if (!temp || !*temp) temp = "/tmp";
#endif
    char stage[SYNC_PATH_CAP], copy[SYNC_PATH_CAP];
    if (!temp || sync_make_stage(temp, stage, sizeof(stage)) != 0) return false;
    bool matches = sync_join(copy, sizeof(copy), stage, "copy") == 0 &&
                   download_tree(share, remote, copy, 0) == 0 && sync_skill_matches(copy, hash);
    sync_remove_tree(stage);
    return matches;
}

static int share_pull(share_t *share, const char *remote, const char *root, const char *target,
                      const sync_drift_item_t *expected) {
    char stage[SYNC_PATH_CAP], replacement[SYNC_PATH_CAP];
    if (share_has_skill(share, remote) != 1) return -1;
    ai_mkdir_p(root);
    if (sync_make_stage(root, stage, sizeof(stage)) != 0) return -1;
    int rc = -1;
    if (sync_join(replacement, sizeof(replacement), stage, "new") == 0 &&
        download_tree(share, remote, replacement, 0) == 0 && skill_is_directory(replacement)) {
        if (!expected || (sync_skill_matches(replacement, expected->remote_hash) &&
                          sync_skill_matches(target, expected->local_hash)))
            rc = sync_replace_tree(stage, replacement, target);
        else rc = SYNC_TRANSFER_CHANGED;
    }
    if (rc != -2) sync_remove_tree(stage);
    return rc;
}

static int share_push(share_t *share, const char *local, const char *target,
                      const sync_drift_item_t *expected) {
    char stage[SYNC_PATH_CAP], replacement[SYNC_PATH_CAP];
    if (!skill_is_directory(local) || share_stage(share, stage) != 0) return -1;
    int rc = -1;
    if (share_path(replacement, stage, "new") == 0 &&
        upload_tree(share, replacement, local, 0) == 0 &&
        share_has_skill(share, replacement) == 1) {
        if (!expected || (remote_skill_matches(share, replacement, expected->local_hash) &&
                          remote_skill_matches(share, target, expected->remote_hash)))
            rc = share_replace(share, stage, replacement, target);
        else rc = SYNC_TRANSFER_CHANGED;
    }
    if (rc != -2) share_remove(share, stage, 0);
    return rc;
}

int sync_network_transfer(const sync_config_t *cfg, const char *name, const char *local,
                          bool pull, const sync_drift_item_t *expected, sync_progress_cb cb, void *user) {
    if (!sync_valid_component(name) || skill_skip_name(name)) return -1;
    char root[SYNC_PATH_CAP], path[SYNC_PATH_CAP], remote[SYNC_PATH_CAP];
    if (sync_expand(local ? local : "~/.ai-space/skills", root, sizeof(root)) != 0 ||
        sync_join(path, sizeof(path), root, name) != 0) return -1;

    share_t share = {0};
    char message[256] = "";
    int rc = share_connect(&share, cfg, message, sizeof(message));
    if (rc == 0) {
        if (cb) cb(0.2f, pull ? "正在下载技能文件" : "正在上传技能文件", user);
        rc = share_path(remote, share.root, name);
        if (rc == 0) rc = pull ? share_pull(&share, remote, root, path, expected) : share_push(&share, path, remote, expected);
        if (rc == -2) snprintf(message, sizeof(message), "替换失败，暂存目录保留了原有内容");
        else if (rc == SYNC_TRANSFER_CHANGED) snprintf(message, sizeof(message), "技能状态已变化，请检查后重试");
        else if (rc != 0) snprintf(message, sizeof(message), "共享目录传输失败：%.180s", share_error(&share));
    }
    share_close(&share);
    if (cb) cb(rc == 0 ? 1.0f : 0.2f, rc == 0 ? "共享目录同步完成" : message, user);
    return rc;
}
