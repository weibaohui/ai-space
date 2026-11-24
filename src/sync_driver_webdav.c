#include "sync_webdav_listing.h"
#include "skill_metadata.h"
#include "sync_directory.h"
#include "sync_local_files.h"
#include "portability.h"

#include <dirent.h>
#include <errno.h>
#include <sqlite3.h>
#include <string.h>
#include <sys/stat.h>
#ifndef _WIN32
#include <spawn.h>
#include <sys/wait.h>
extern char **environ;
#endif

#define DAV_LIST_LIMIT (16L * 1024 * 1024)

typedef struct {
    char base[DAV_URL_CAP];
    char root[DAV_URL_CAP];
} dav_state_t;

static bool dav_child_url(char *out, size_t cap, const char *parent, const char *name) {
    static const char hex[] = "0123456789ABCDEF";
    size_t used = strlen(parent);
    if (!sync_valid_component(name) || used + 2 >= cap) return false;
    memcpy(out, parent, used);
    if (used && out[used - 1] != '/') out[used++] = '/';
    for (const unsigned char *p = (const unsigned char *)name; *p; p++) {
        if (used + 4 >= cap) return false;
        unsigned char c = *p;
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') || strchr("-._~", c)) {
            out[used++] = c;
        } else {
            out[used++] = '%';
            out[used++] = hex[c >> 4];
            out[used++] = hex[c & 15];
        }
    }
    out[used] = '\0';
    return true;
}

static void curl_option(FILE *file, const char *option, const char *value) {
    fprintf(file, "%s = \"", option);
    for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
        switch (*p) {
        case '\\': fputs("\\\\", file); break;
        case '"': fputs("\\\"", file); break;
        case '\n': fputs("\\n", file); break;
        case '\r': fputs("\\r", file); break;
        case '\t': fputs("\\t", file); break;
        default: fputc(*p, file); break;
        }
    }
    fputs("\"\n", file);
}

static int run_curl(const char *path) {
#ifdef _WIN32
    char quoted[SYNC_PATH_CAP + 3];
    snprintf(quoted, sizeof(quoted), "\"%s\"", path);
    const char *args[] = {"curl.exe", "-q", "--config", quoted, NULL};
    return (int)_spawnvp(_P_WAIT, args[0], args);
#else
    char *args[] = {"curl", "-q", "--config", (char *)path, NULL};
    pid_t pid;
    if (posix_spawnp(&pid, args[0], NULL, NULL, args, environ) != 0) return -1;
    int status;
    pid_t waited;
    do { waited = waitpid(pid, &status, 0); } while (waited < 0 && errno == EINTR);
    return waited == pid && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

/* 将 curl 选项写入临时配置文件，进程参数只传配置文件的路径。 */
static int dav_request_headers(const sync_backend_t *self, const char *method, const char *url,
                               const char *header, const char *extra, const char *input, const char *output) {
    char stage[SYNC_PATH_CAP], config[SYNC_PATH_CAP], headers[SYNC_PATH_CAP];
    if (sync_make_temp_stage(stage, sizeof(stage)) != 0) return 0;
    int status = 0;
    if (sync_join(config, sizeof(config), stage, "curl.conf") != 0 ||
        sync_join(headers, sizeof(headers), stage, "headers") != 0) goto done;
    FILE *file = fopen(config, "wb");
    if (!file) goto done;

    fputs("silent\ngloboff\nconnect-timeout = 8\nmax-time = 90\n", file);
    curl_option(file, "proto", "=http,https");
    curl_option(file, "url", url);
    curl_option(file, "request", method);
    curl_option(file, "output", output ? output : AI_DEVNULL);
    curl_option(file, "dump-header", headers);
    if (header) curl_option(file, "header", header);
    if (extra) curl_option(file, "header", extra);
    curl_option(file, "header", "Expect:");
    if (strcmp(method, "PROPFIND") == 0) {
        curl_option(file, "header", "Content-Type: application/xml");
        curl_option(file, "data", "<propfind xmlns=\"DAV:\"><prop><resourcetype/><getlastmodified/></prop></propfind>");
        curl_option(file, "max-filesize", "16777216");
    }
    if (input) curl_option(file, "upload-file", input);
    if (output && strcmp(method, "GET") == 0) fputs("remote-time\n", file);
    if (self->cfg.webdav_user[0]) {
        char credential[sizeof self->cfg.webdav_user + sizeof self->cfg.webdav_pass + 1];
        snprintf(credential, sizeof(credential), "%s:%s", self->cfg.webdav_user, self->cfg.webdav_pass);
        curl_option(file, "user", credential);
        fputs("anyauth\n", file);
    }
    bool ok = !ferror(file);
    if (fclose(file) != 0) ok = false;
    if (!ok || run_curl(config) != 0) goto done;

    file = fopen(headers, "rb");
    if (!file) goto done;
    char line[512];
    while (fgets(line, sizeof(line), file)) {
        int code;
        if (sscanf(line, "HTTP/%*s %d", &code) == 1) status = code;
    }
    fclose(file);

done:
    sync_remove_tree(stage);
    return status;
}

static int dav_request(const sync_backend_t *self, const char *method, const char *url,
                       const char *header, const char *input, const char *output) {
    return dav_request_headers(self, method, url, header, NULL, input, output);
}

static bool dav_success(int code) {
    return code >= 200 && code < 300 && code != 207;
}

static void dav_error(char *error, size_t cap, const char *operation, int code) {
    if (!error) return;
    if (!code) snprintf(error, cap, "%s失败，curl 不可用或服务器无响应", operation);
    else if (code == 401) snprintf(error, cap, "%s失败，请检查 WebDAV 用户名和密码 (HTTP 401)", operation);
    else if (code == 403) snprintf(error, cap, "%s失败，WebDAV 目录拒绝访问 (HTTP 403)", operation);
    else snprintf(error, cap, "%s失败 (HTTP %d)", operation, code);
}

/* 目录查询使用 Depth 头，响应通过专用解析器处理。 */
static int dav_list(const sync_backend_t *self, const char *url, int depth, dav_listing_t *listing) {
    *listing = (dav_listing_t){0};
    char stage[SYNC_PATH_CAP], file_path[SYNC_PATH_CAP], collection[DAV_URL_CAP];
    int n = snprintf(collection, sizeof(collection), "%s%s", url, url[strlen(url) - 1] == '/' ? "" : "/");
    if (n < 0 || (size_t)n >= sizeof(collection) || sync_make_temp_stage(stage, sizeof(stage)) != 0) return 0;
    int rc = 0;
    if (sync_join(file_path, sizeof(file_path), stage, "listing") != 0) goto done;
    rc = dav_request(self, "PROPFIND", collection, depth ? "Depth: 1" : "Depth: 0", NULL, file_path);
    if (rc != 207) goto done;

    rc = 0;
    FILE *file = fopen(file_path, "rb");
    if (!file) goto done;
    if (fseek(file, 0, SEEK_END) == 0) {
        long size = ftell(file);
        if (size > 0 && size <= DAV_LIST_LIMIT && fseek(file, 0, SEEK_SET) == 0) {
            char *xml = malloc((size_t)size + 1);
            if (xml) {
                if (fread(xml, 1, (size_t)size, file) == (size_t)size) {
                    xml[size] = '\0';
                    if (dav_parse_listing(xml, (size_t)size, collection, listing)) rc = 207;
                }
                free(xml);
            }
        }
    }
    fclose(file);
    if (rc != 207) {
        free(listing->entries);
        *listing = (dav_listing_t){0};
    }

done:
    sync_remove_tree(stage);
    return rc;
}

static int dav_query_collection(const sync_backend_t *self, const char *url) {
    dav_listing_t listing;
    int status = dav_list(self, url, 0, &listing);
    free(listing.entries);
    return status;
}

static int dav_ensure_root(const sync_backend_t *self) {
    const dav_state_t *dav = self->priv_data;
    int status = dav_query_collection(self, dav->base);
    if (status != 207) return status;

    char url[DAV_URL_CAP];
    snprintf(url, sizeof(url), "%s", dav->root);
    size_t base_len = strlen(dav->base);
    for (char *p = url + base_len; *p; p++) {
        if (*p != '/') continue;
        char *end = strchr(p + 1, '/');
        if (end) *end = '\0';
        status = dav_query_collection(self, url);
        if (status == 404) {
            status = dav_request(self, "MKCOL", url, NULL, NULL, NULL);
            if (dav_success(status) || status == 405) status = dav_query_collection(self, url);
        }
        if (status != 207 || !end) return status;
        *end = '/';
        p = end - 1;
    }
    return status;
}

static int dav_init(sync_backend_t *self, const sync_config_t *cfg) {
    dav_state_t *dav = calloc(1, sizeof(*dav));
    if (!dav) return -1;
    self->priv_data = dav;
    const char *url = cfg->webdav_url;
    const char *scheme = strncmp(url, "https://", 8) == 0 ? url + 8 :
                         strncmp(url, "http://", 7) == 0 ? url + 7 : NULL;
    if (!scheme || !scheme[0] || scheme[0] == '/' || strpbrk(url, "?#\r\n\t\\\" ")) return 0;
    const char *at = strchr(scheme, '@');
    const char *slash = strchr(scheme, '/');
    if (at && (!slash || at < slash)) return 0;

    snprintf(dav->base, sizeof(dav->base), "%s", url);
    size_t len = strlen(dav->base);
    while (dav->base[len - 1] == '/') dav->base[--len] = '\0';
    snprintf(dav->root, sizeof(dav->root), "%s", dav->base);
    const char *p = cfg->webdav_remote_path;
    while (*p) {
        if (*p == '/') {
            p++;
            continue;
        }
        size_t part = strcspn(p, "/");
        char name[256], next[DAV_URL_CAP];
        if (part >= sizeof(name)) break;
        memcpy(name, p, part);
        name[part] = '\0';
        if (!dav_child_url(next, sizeof(next), dav->root, name)) break;
        snprintf(dav->root, sizeof(dav->root), "%s", next);
        p += part;
    }
    if (*p) dav->root[0] = '\0';
    return 0;
}

static void dav_destroy(sync_backend_t *self) {
    free(self->priv_data);
    self->priv_data = NULL;
}

static bool dav_scratch_url(char *out, size_t cap, const char *root) {
    unsigned char bytes[8];
    sqlite3_randomness((int)sizeof(bytes), bytes);
    char name[32];
    const char *prefix = ".ai-space-";
    size_t offset = strlen(prefix);
    memcpy(name, prefix, offset);
    for (size_t i = 0; i < sizeof(bytes); i++) {
        snprintf(name + offset + 2 * i, 3, "%02x", (unsigned)bytes[i]);
    }
    return dav_child_url(out, cap, root, name);
}

static int dav_delete_collection(const sync_backend_t *self, const char *url) {
    char collection[DAV_URL_CAP + 1];
    snprintf(collection, sizeof(collection), "%s/", url);
    return dav_request(self, "DELETE", collection, NULL, NULL, NULL);
}

static int dav_move_collection(const sync_backend_t *self, const char *source, const char *target, bool overwrite) {
    char collection[DAV_URL_CAP + 1];
    snprintf(collection, sizeof(collection), "%s/", source);
    char header[DAV_URL_CAP + 16];
    snprintf(header, sizeof(header), "Destination: %s/", target);
    return dav_request_headers(self, "MOVE", collection, header, overwrite ? NULL : "Overwrite: F", NULL, NULL);
}

static int dav_check_available(sync_backend_t *self, char *error, size_t cap) {
    const dav_state_t *dav = self->priv_data;
    self->is_connected = false;
    if (!dav->root[0]) {
        if (error) snprintf(error, cap, "请填写有效的 HTTP(S) WebDAV 地址及相对子目录");
        return -1;
    }
    int status = dav_ensure_root(self);
    if (status != 207) {
        dav_error(error, cap, "读取 WebDAV 同步目录", status);
        return -1;
    }

    char stage[SYNC_PATH_CAP], local[SYNC_PATH_CAP], result[SYNC_PATH_CAP];
    if (sync_make_temp_stage(stage, sizeof(stage)) != 0) return -1;
    int rc = -1;
    if (sync_join(local, sizeof(local), stage, "probe") != 0 ||
        sync_join(result, sizeof(result), stage, "result") != 0) goto done;
    FILE *file = fopen(local, "wb");
    if (!file) goto done;
    const char *probe = "AI-Space WebDAV";
    bool wrote = fputs(probe, file) >= 0;
    if (fclose(file) != 0 || !wrote) goto done;

    char scratch[DAV_URL_CAP], from[DAV_URL_CAP], to[DAV_URL_CAP];
    char upload[DAV_URL_CAP], download[DAV_URL_CAP];
    if (!dav_scratch_url(scratch, sizeof(scratch), dav->root) ||
        !dav_child_url(from, sizeof(from), scratch, "source") ||
        !dav_child_url(to, sizeof(to), scratch, "moved") ||
        !dav_child_url(upload, sizeof(upload), from, "probe") ||
        !dav_child_url(download, sizeof(download), to, "probe")) goto done;
    status = dav_request(self, "MKCOL", scratch, NULL, NULL, NULL);
    if (!dav_success(status)) {
        dav_error(error, cap, "创建 WebDAV 测试目录", status);
        goto done;
    }

    status = dav_request(self, "MKCOL", from, NULL, NULL, NULL);
    if (dav_success(status)) status = dav_request(self, "PUT", upload, NULL, local, NULL);
    if (dav_success(status)) status = dav_move_collection(self, from, to, false);
    if (dav_success(status)) status = dav_request(self, "GET", download, NULL, NULL, result);
    int removed = dav_delete_collection(self, scratch);
    if (!dav_success(status) || !dav_success(removed)) {
        dav_error(error, cap, "WebDAV 读写、移动或清理测试", dav_success(status) ? removed : status);
        goto done;
    }
    file = fopen(result, "rb");
    if (file) {
        char readback[32];
        if (fgets(readback, sizeof(readback), file) && strcmp(readback, probe) == 0) rc = 0;
        fclose(file);
    }
    if (error) snprintf(error, cap, "%s", rc == 0 ? "WebDAV 读写及目录同步验证通过" : "WebDAV 测试文件内容不一致");

done:
    sync_remove_tree(stage);
    self->is_connected = rc == 0;
    return rc;
}

static bool has_skill_file(const dav_listing_t *listing) {
    for (size_t i = 0; i < listing->count; i++) {
        if (!listing->entries[i].collection && strcmp(listing->entries[i].name, SKILL_DOCUMENT) == 0) return true;
    }
    return false;
}

static int dav_download(const sync_backend_t *self, const char *url, const char *local,
                        const dav_listing_t *listing, int depth) {
    if (depth >= SYNC_TREE_DEPTH || AI_MKDIR(local) != 0) return 0;
    for (size_t i = 0; i < listing->count; i++) {
        const dav_entry_t *entry = &listing->entries[i];
        char child_url[DAV_URL_CAP], child_local[SYNC_PATH_CAP];
        if (!dav_child_url(child_url, sizeof(child_url), url, entry->name) ||
            sync_join(child_local, sizeof(child_local), local, entry->name) != 0) return 0;
        int status;
        if (entry->collection) {
            dav_listing_t children;
            status = dav_list(self, child_url, 1, &children);
            if (status == 207) status = dav_download(self, child_url, child_local, &children, depth + 1);
            free(children.entries);
        } else {
            status = dav_request(self, "GET", child_url, NULL, NULL, child_local);
        }
        if (!dav_success(status)) return status;
    }
    time_t modified = listing->modified;
    return sync_set_mtime(local, modified) == 0 ? 200 : 0;
}

/* 先把远端技能下载到本地暂存目录，再用统一规则生成指纹清单。 */
static int dav_fetch(sync_backend_t *self, sync_remote_manifest_t *manifest, char *error, size_t cap) {
    *manifest = (sync_remote_manifest_t){0};
    const dav_state_t *dav = self->priv_data;
    if (!dav->root[0]) {
        if (error) snprintf(error, cap, "未配置有效的 WebDAV 同步目录");
        return -1;
    }
    char stage[SYNC_PATH_CAP];
    if (sync_make_temp_stage(stage, sizeof(stage)) != 0) return -1;
    dav_listing_t root;
    int status = dav_list(self, dav->root, 1, &root);
    int rc = -1;
    if (status != 207) goto done;
    for (size_t i = 0; i < root.count; i++) {
        const dav_entry_t *entry = &root.entries[i];
        if (!entry->collection) continue;
        char url[DAV_URL_CAP], target[SYNC_PATH_CAP];
        if (!dav_child_url(url, sizeof(url), dav->root, entry->name) ||
            sync_join(target, sizeof(target), stage, entry->name) != 0) {
            status = 0;
            goto done;
        }
        dav_listing_t listing;
        status = dav_list(self, url, 1, &listing);
        if (status == 207) {
            status = has_skill_file(&listing) ? dav_download(self, url, target, &listing, 0) : 200;
        }
        free(listing.entries);
        if (!dav_success(status)) goto done;
    }
    rc = sync_scan_directory(stage, false, manifest, error, cap);
    if (rc == 0) snprintf(manifest->remote_root, sizeof(manifest->remote_root), "%.511s", dav->root);

done:
    if (rc != 0 && (!dav_success(status) || status == 0)) dav_error(error, cap, "读取 WebDAV 技能", status);
    free(root.entries);
    sync_remove_tree(stage);
    return rc;
}

static int dav_pull(sync_backend_t *self, const char *name, const char *local,
                    sync_progress_cb cb, void *user) {
    const dav_state_t *dav = self->priv_data;
    char url[DAV_URL_CAP], root[SYNC_PATH_CAP], stage[SYNC_PATH_CAP];
    if (!dav->root[0] || !sync_valid_component(name) || skill_skip_name(name) ||
        !dav_child_url(url, sizeof(url), dav->root, name) ||
        sync_expand(local ? local : "~/.ai-space/skills", root, sizeof(root)) != 0) return -1;
    ai_mkdir_p(root);
    if (sync_make_stage(root, stage, sizeof(stage)) != 0) return -1;
    if (cb) cb(0.2f, "正在从 WebDAV 拉取技能", user);
    char replacement[SYNC_PATH_CAP], target[SYNC_PATH_CAP];
    int rc = -1;
    if (sync_join(replacement, sizeof(replacement), stage, "new") == 0 &&
        sync_join(target, sizeof(target), root, name) == 0) {
        dav_listing_t listing;
        int status = dav_list(self, url, 1, &listing);
        if (status == 207 && has_skill_file(&listing) &&
            dav_success(dav_download(self, url, replacement, &listing, 0))) {
            rc = sync_publish_local_skill(stage, replacement, target, self->expected_skill, true);
        }
        free(listing.entries);
    }
    sync_cleanup_stage(stage, rc);
    if (cb) cb(rc == 0 ? 1.0f : 0.2f, rc == 0 ? "WebDAV 拉取完成" : rc == SYNC_TRANSFER_CHANGED ? "技能状态已变化，请检查后重试" : "WebDAV 拉取失败", user);
    return rc;
}

static int dav_upload_contents(const sync_backend_t *self, const char *local, const char *url, int depth) {
    if (depth >= SYNC_TREE_DEPTH) return 0;
    DIR *dir = opendir(local);
    if (!dir) return 0;
    int status = 200;
    while (dav_success(status)) {
        errno = 0;
        struct dirent *entry = readdir(dir);
        if (!entry) {
            if (errno) status = 0;
            break;
        }
        if (skill_skip_name(entry->d_name)) continue;
        char child[SYNC_PATH_CAP], target[DAV_URL_CAP];
        struct stat st;
        if (sync_join(child, sizeof(child), local, entry->d_name) != 0 ||
            !dav_child_url(target, sizeof(target), url, entry->d_name) ||
            lstat(child, &st) != 0) {
            status = 0;
        } else if (S_ISDIR(st.st_mode)) {
            status = dav_request(self, "MKCOL", target, NULL, NULL, NULL);
            if (dav_success(status)) status = dav_upload_contents(self, child, target, depth + 1);
        } else if (S_ISREG(st.st_mode)) {
            status = dav_request(self, "PUT", target, NULL, child, NULL);
        } else {
            status = 0;
        }
    }
    closedir(dir);
    return status;
}

static bool dav_skill_matches(const sync_backend_t *self, const char *url, const char *hash) {
    dav_listing_t listing;
    int status = dav_list(self, url, 1, &listing);
    bool matches = !hash[0] && status == 404;
    char stage[SYNC_PATH_CAP], path[SYNC_PATH_CAP];
    if (hash[0] && status == 207 && has_skill_file(&listing) && sync_make_temp_stage(stage, sizeof(stage)) == 0) {
        matches = sync_join(path, sizeof(path), stage, "check") == 0 &&
                  dav_success(dav_download(self, url, path, &listing, 0)) && sync_skill_matches(path, hash);
        sync_remove_tree(stage);
    }
    free(listing.entries);
    return matches;
}

static int dav_push(sync_backend_t *self, const char *name, const char *local,
                    sync_progress_cb cb, void *user) {
    const dav_state_t *dav = self->priv_data;
    char root[SYNC_PATH_CAP], source[SYNC_PATH_CAP], stage[DAV_URL_CAP], target[DAV_URL_CAP];
    if (!dav->root[0] || !sync_valid_component(name) || skill_skip_name(name) ||
        sync_expand(local ? local : "~/.ai-space/skills", root, sizeof(root)) != 0 ||
        sync_join(source, sizeof(source), root, name) != 0 || !skill_is_directory(source) ||
        !dav_child_url(target, sizeof(target), dav->root, name) ||
        !dav_scratch_url(stage, sizeof(stage), dav->root)) return -1;
    if (cb) cb(0.2f, "正在向 WebDAV 上传技能", user);
    /* 在远端暂存目录完成上传并校验之后，再替换正式目录。 */
    const sync_drift_item_t *expected = self->expected_skill;
    int status = dav_request(self, "MKCOL", stage, NULL, NULL, NULL);
    if (dav_success(status)) {
        status = dav_upload_contents(self, source, stage, 0);
        if (dav_success(status) && expected &&
            (!dav_skill_matches(self, stage, expected->local_hash) ||
             !dav_skill_matches(self, target, expected->remote_hash))) status = SYNC_TRANSFER_CHANGED;
        if (dav_success(status)) status = dav_move_collection(self, stage, target,
                                                             !expected || expected->remote_hash[0]);
        if (!dav_success(status)) dav_delete_collection(self, stage);
    }
    bool ok = dav_success(status);
    if (cb) {
        char msg[160] = "WebDAV 推送完成";
        if (status == SYNC_TRANSFER_CHANGED || status == 412)
            snprintf(msg, sizeof(msg), "技能状态已变化，请检查后重试");
        else if (!ok) dav_error(msg, sizeof(msg), "WebDAV 推送", status);
        cb(ok ? 1.0f : 0.2f, msg, user);
    }
    return ok ? 0 : status == SYNC_TRANSFER_CHANGED || status == 412 ? SYNC_TRANSFER_CHANGED : -1;
}

const sync_driver_ops_t g_webdav_driver_ops = {
    .type = SYNC_BACKEND_WEBDAV,
    .name = "webdav",
    .display_name = "WebDAV 远端存储",
    .init = dav_init,
    .destroy = dav_destroy,
    .test_connection = dav_check_available,
    .check_available = dav_check_available,
    .fetch_remote_manifest = dav_fetch,
    .check_drift = sync_check_directory_drift,
    .pull_skill = dav_pull,
    .push_skill = dav_push,
    .pull_all = sync_batch_pull_all,
    .push_all = sync_batch_push_all,
    .transfer_batch = sync_transfer_batch,
};
