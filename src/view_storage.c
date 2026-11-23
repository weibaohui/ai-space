#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include "portability.h"
#include "view_storage.h"
#include "view_common.h"
#include "app_state.h"

/* Path expander */
static void expand_path_local(const char *in, char *out, size_t cap) {
    if (!in || !out || cap == 0) return;
    if (in[0] == '~' && (in[1] == '/' || in[1] == '\0')) {
        const char *home = ai_home_dir();
        if (home && home[0]) {
            snprintf(out, cap, "%s%s", home, in + 1);
            return;
        }
    }
    snprintf(out, cap, "%s", in);
}

/* Recursive directory size calculation */
static int64_t compute_dir_size_recursive(const char *dir_path, int *out_file_count) {
    if (!dir_path || !dir_path[0]) return 0;
    DIR *d = opendir(dir_path);
    if (!d) return 0;

    int64_t total = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        char sub[1024];
        snprintf(sub, sizeof(sub), "%s/%s", dir_path, ent->d_name);
        struct stat st;
        if (lstat(sub, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                total += compute_dir_size_recursive(sub, out_file_count);
            } else if (S_ISREG(st.st_mode)) {
                total += (int64_t)st.st_size;
                if (out_file_count) (*out_file_count)++;
            }
        }
    }
    closedir(d);
    return total;
}

/* Recursive directory removal */
static int remove_dir_recursive_helper(const char *dir_path) {
    if (!dir_path || !dir_path[0]) return -1;
    DIR *d = opendir(dir_path);
    if (!d) return -1;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        char sub[1024];
        snprintf(sub, sizeof(sub), "%s/%s", dir_path, ent->d_name);
        struct stat st;
        if (lstat(sub, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                remove_dir_recursive_helper(sub);
            } else {
                unlink(sub);
            }
        }
    }
    closedir(d);
    return rmdir(dir_path);
}

/* Parse snapshot directory name: YYYYMMDD_HHMMSS_tool_skill */
static void parse_snapshot_name(const char *dir_name, char *out_time, size_t time_cap,
                                char *out_tool, size_t tool_cap,
                                char *out_skill, size_t skill_cap) {
    if (!dir_name) return;
    if (out_time && time_cap > 0) out_time[0] = '\0';
    if (out_tool && tool_cap > 0) out_tool[0] = '\0';
    if (out_skill && skill_cap > 0) out_skill[0] = '\0';

    /* Expecting format: 20261122_220139_agent_agent-browser */
    if (strlen(dir_name) >= 16 && dir_name[8] == '_' && dir_name[15] == '_') {
        if (out_time && time_cap >= 20) {
            snprintf(out_time, time_cap, "%.4s-%.2s-%.2s %.2s:%.2s:%.2s",
                     dir_name, dir_name + 4, dir_name + 6,
                     dir_name + 9, dir_name + 11, dir_name + 13);
        }

        const char *p = dir_name + 16;
        const char *next_us = strchr(p, '_');
        if (next_us) {
            size_t tlen = (size_t)(next_us - p);
            if (out_tool && tool_cap > 0) {
                if (tlen >= tool_cap) tlen = tool_cap - 1;
                strncpy(out_tool, p, tlen);
                out_tool[tlen] = '\0';
            }
            if (out_skill && skill_cap > 0) {
                strncpy(out_skill, next_us + 1, skill_cap - 1);
                out_skill[skill_cap - 1] = '\0';
            }
            return;
        }
    }

    /* Fallback if name doesn't match standard template */
    if (out_time && time_cap > 0) strncpy(out_time, "快照时间", time_cap - 1);
    if (out_tool && tool_cap > 0) strncpy(out_tool, "本地备份", tool_cap - 1);
    if (out_skill && skill_cap > 0) strncpy(out_skill, dir_name, skill_cap - 1);
}

/* Compare snapshots by mtime descending (newest first) */
static int compare_snapshots_desc(const void *a, const void *b) {
    const storage_snapshot_item_t *ia = (const storage_snapshot_item_t *)a;
    const storage_snapshot_item_t *ib = (const storage_snapshot_item_t *)b;
    if (ib->mtime != ia->mtime) {
        return (ib->mtime > ia->mtime) ? 1 : -1;
    }
    return strcmp(ib->dir_name, ia->dir_name);
}

/* Cross-platform open folder in system file manager */
static void open_folder_in_explorer(const char *path) {
    if (!path || !path[0]) return;
#ifdef __APPLE__
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "open \"%s\" &", path);
    system(cmd);
#elif defined(_WIN32)
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "explorer \"%s\"", path);
    system(cmd);
#else
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "xdg-open \"%s\" &", path);
    system(cmd);
#endif
}

/* Refresh storage statistics */
void app_refresh_storage_stats(app_state_t *s) {
    if (!s) return;
    storage_stats_t *st = &s->storage_stats;
    memset(st, 0, sizeof(storage_stats_t));

    /* 1. Backups statistics */
    char exp_backup[512] = "";
    expand_path_local(s->setting_backup_dir[0] ? s->setting_backup_dir : "~/.ai-space/backups",
                      exp_backup, sizeof(exp_backup));

    DIR *bd = opendir(exp_backup);
    if (bd) {
        struct dirent *ent;
        while ((ent = readdir(bd)) != NULL) {
            if (ent->d_name[0] == '.') continue;
            char snap_dir[1024];
            snprintf(snap_dir, sizeof(snap_dir), "%s/%s", exp_backup, ent->d_name);

            struct stat fst;
            if (lstat(snap_dir, &fst) == 0 && S_ISDIR(fst.st_mode)) {
                if (st->snapshot_count < 128) {
                    storage_snapshot_item_t *item = &st->snapshots[st->snapshot_count];
                    strncpy(item->dir_name, ent->d_name, sizeof(item->dir_name) - 1);
                    strncpy(item->full_path, snap_dir, sizeof(item->full_path) - 1);
                    item->mtime = fst.st_mtime;

                    int fc = 0;
                    item->size_bytes = compute_dir_size_recursive(snap_dir, &fc);
                    item->file_count = fc;

                    parse_snapshot_name(ent->d_name, item->time_str, sizeof(item->time_str),
                                        item->tool_name, sizeof(item->tool_name),
                                        item->skill_name, sizeof(item->skill_name));

                    st->backup_total_bytes += item->size_bytes;
                    st->backup_file_count += fc;
                    st->snapshot_count++;
                }
            }
        }
        closedir(bd);

        /* Sort snapshots newest first */
        if (st->snapshot_count > 1) {
            qsort(st->snapshots, st->snapshot_count, sizeof(storage_snapshot_item_t), compare_snapshots_desc);
        }
    }

    /* 2. Cache statistics (~/.ai-space/cache) */
    char exp_cache[512] = "";
    expand_path_local("~/.ai-space/cache", exp_cache, sizeof(exp_cache));

    DIR *cd = opendir(exp_cache);
    if (cd) {
        struct dirent *ent;
        while ((ent = readdir(cd)) != NULL) {
            if (ent->d_name[0] == '.') continue;
            char item_path[1024];
            snprintf(item_path, sizeof(item_path), "%s/%s", exp_cache, ent->d_name);

            struct stat fst;
            if (lstat(item_path, &fst) == 0) {
                if (S_ISDIR(fst.st_mode)) {
                    st->cache_dir_count++;
                    int fc = 0;
                    st->cache_total_bytes += compute_dir_size_recursive(item_path, &fc);
                    st->cache_file_count += fc;
                } else if (S_ISREG(fst.st_mode)) {
                    st->cache_total_bytes += (int64_t)fst.st_size;
                    st->cache_file_count++;
                }
            }
        }
        closedir(cd);
    }

    /* 3. Central Hub skills statistics */
    char exp_hub[512] = "";
    expand_path_local(s->setting_hub_dir[0] ? s->setting_hub_dir : "~/.ai-space/skills",
                      exp_hub, sizeof(exp_hub));

    DIR *hd = opendir(exp_hub);
    if (hd) {
        struct dirent *ent;
        while ((ent = readdir(hd)) != NULL) {
            if (ent->d_name[0] == '.') continue;
            char item_path[1024];
            snprintf(item_path, sizeof(item_path), "%s/%s", exp_hub, ent->d_name);

            struct stat fst;
            if (lstat(item_path, &fst) == 0 && S_ISDIR(fst.st_mode)) {
                st->hub_skill_count++;
                int fc = 0;
                st->hub_total_bytes += compute_dir_size_recursive(item_path, &fc);
            }
        }
        closedir(hd);
    }

    /* Timestamp */
    time_t now = time(NULL);
    struct tm tm_info;
    ai_localtime(&now, &tm_info);
    strftime(st->last_checked_at, sizeof(st->last_checked_at), "%Y-%m-%d %H:%M:%S", &tm_info);

    st->loaded = true;
}

/* Clear all items inside ~/.ai-space/cache */
void app_clear_storage_cache(app_state_t *s) {
    if (!s) return;
    char exp_cache[512] = "";
    expand_path_local("~/.ai-space/cache", exp_cache, sizeof(exp_cache));

    DIR *cd = opendir(exp_cache);
    if (cd) {
        struct dirent *ent;
        while ((ent = readdir(cd)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
            char item_path[1024];
            snprintf(item_path, sizeof(item_path), "%s/%s", exp_cache, ent->d_name);

            struct stat fst;
            if (lstat(item_path, &fst) == 0) {
                if (S_ISDIR(fst.st_mode)) {
                    remove_dir_recursive_helper(item_path);
                } else {
                    unlink(item_path);
                }
            }
        }
        closedir(cd);
    }

    app_refresh_storage_stats(s);
}

/* Clear all snapshots in backup dir */
void app_clear_all_backups(app_state_t *s) {
    if (!s) return;
    char exp_backup[512] = "";
    expand_path_local(s->setting_backup_dir[0] ? s->setting_backup_dir : "~/.ai-space/backups",
                      exp_backup, sizeof(exp_backup));

    DIR *bd = opendir(exp_backup);
    if (bd) {
        struct dirent *ent;
        while ((ent = readdir(bd)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
            char item_path[1024];
            snprintf(item_path, sizeof(item_path), "%s/%s", exp_backup, ent->d_name);

            struct stat fst;
            if (lstat(item_path, &fst) == 0) {
                if (S_ISDIR(fst.st_mode)) {
                    remove_dir_recursive_helper(item_path);
                } else {
                    unlink(item_path);
                }
            }
        }
        closedir(bd);
    }

    app_refresh_storage_stats(s);
}

/* Prune backups keeping newest keep_count */
void app_prune_backups_keep_latest(app_state_t *s, int keep_count) {
    if (!s) return;
    if (!s->storage_stats.loaded) {
        app_refresh_storage_stats(s);
    }

    storage_stats_t *st = &s->storage_stats;
    if (st->snapshot_count <= keep_count) return;

    for (int i = keep_count; i < st->snapshot_count; i++) {
        remove_dir_recursive_helper(st->snapshots[i].full_path);
    }

    app_refresh_storage_stats(s);
}

/* Delete a single snapshot */
void app_delete_single_snapshot(app_state_t *s, const char *snapshot_name) {
    if (!s || !snapshot_name || !snapshot_name[0]) return;
    char exp_backup[512] = "";
    expand_path_local(s->setting_backup_dir[0] ? s->setting_backup_dir : "~/.ai-space/backups",
                      exp_backup, sizeof(exp_backup));

    char target[1024];
    snprintf(target, sizeof(target), "%s/%s", exp_backup, snapshot_name);
    remove_dir_recursive_helper(target);

    app_refresh_storage_stats(s);
}

static void request_storage_cleanup(app_state_t *s, storage_cleanup_action_t action) {
    s->storage_cleanup_action = action;
    s->show_storage_cleanup_modal = true;
}

static void render_storage_cleanup_modal(cui_ctx *ui, app_state_t *s, bool busy) {
    if (!s->show_storage_cleanup_modal) return;

    const char *title;
    const char *description;
    switch (s->storage_cleanup_action) {
    case STORAGE_CLEANUP_CACHE:
        title = "清理缓存";
        description = "将删除本地临时缓存。下次同步时会按需重新生成。";
        break;
    case STORAGE_CLEANUP_OLD_SNAPSHOTS:
        title = "清理旧快照";
        description = "将保留最近 3 份快照，其余备份将被删除。";
        break;
    case STORAGE_CLEANUP_ALL_SNAPSHOTS:
        title = "清空备份";
        description = "将删除备份目录中的全部历史快照。";
        break;
    case STORAGE_CLEANUP_SINGLE_SNAPSHOT:
        title = "删除快照";
        description = s->storage_cleanup_snapshot;
        break;
    default:
        title = "清理缓存与旧快照";
        description = "将清空临时缓存，并删除最近 3 份以外的快照。";
        break;
    }

    cui_dialog(ui, title, &s->show_storage_cleanup_modal, .width = 460) {
        cui_column(ui, .width = CUI_FILL, .gap = 12) {
            cui_text(ui, description, .width = CUI_FILL);
            cui_row(ui, .width = CUI_FILL, .gap = 8, .justify = CUI_JUSTIFY_END) {
                if (cui_button(ui, "取消", .variant = CUI_BUTTON_SECONDARY)) {
                    s->show_storage_cleanup_modal = false;
                }
                if (cui_button(ui, "确认清理", .variant = CUI_BUTTON_DANGER,
                               .icon = CUI_ICON_TRASH_2, .disabled = busy)) {
                    switch (s->storage_cleanup_action) {
                    case STORAGE_CLEANUP_CACHE:
                        app_clear_storage_cache(s);
                        break;
                    case STORAGE_CLEANUP_OLD_SNAPSHOTS:
                        app_prune_backups_keep_latest(s, 3);
                        break;
                    case STORAGE_CLEANUP_ALL_SNAPSHOTS:
                        app_clear_all_backups(s);
                        break;
                    case STORAGE_CLEANUP_SINGLE_SNAPSHOT:
                        app_delete_single_snapshot(s, s->storage_cleanup_snapshot);
                        break;
                    case STORAGE_CLEANUP_CACHE_AND_OLD_SNAPSHOTS:
                        app_clear_storage_cache(s);
                        app_prune_backups_keep_latest(s, 3);
                        break;
                    }
                    s->show_storage_cleanup_modal = false;
                    cui_toast(ui, "存储统计已更新", .tone = CUI_TONE_INFO);
                }
            }
        }
    }
}

void render_storage_page(cui_ctx *ui, app_state_t *s) {
    if (!s->storage_stats.loaded) app_refresh_storage_stats(s);

    const storage_stats_t *st = &s->storage_stats;
    bool busy = s->hub_scan.is_scanning || s->sync_is_busy || s->op_progress.active;
    bool has_cache = st->cache_file_count > 0 || st->cache_dir_count > 0;
    char backup_dir[512], cache_dir[512];
    expand_path_local(s->setting_backup_dir[0] ? s->setting_backup_dir : "~/.ai-space/backups",
                      backup_dir, sizeof(backup_dir));
    expand_path_local("~/.ai-space/cache", cache_dir, sizeof(cache_dir));

    char backup_size[32], cache_size[32], hub_size[32];
    format_size((size_t)st->backup_total_bytes, backup_size, sizeof(backup_size));
    format_size((size_t)st->cache_total_bytes, cache_size, sizeof(cache_size));
    format_size((size_t)st->hub_total_bytes, hub_size, sizeof(hub_size));

    cui_column(ui, .width = CUI_FILL, .gap = 14) {
        cui_row(ui, .width = CUI_FILL, .gap = 8, .align = CUI_ALIGN_CENTER) {
            render_workspace_heading(ui, CUI_ICON_DATABASE, "本地存储", "管理临时缓存与历史快照");
            if (cui_button(ui, "刷新统计", .variant = CUI_BUTTON_SECONDARY,
                           .icon = CUI_ICON_REFRESH_CW, .disabled = busy)) {
                app_refresh_storage_stats(s);
            }
            cui_menu(ui, "更多维护操作", .icon_only = true, .variant = CUI_BUTTON_GHOST, .width = 220) {
                if (cui_menu_item(ui, "清理缓存与旧快照", .icon = CUI_ICON_TRASH_2,
                                  .danger = true, .disabled = busy || (!has_cache && st->snapshot_count <= 3))) {
                    request_storage_cleanup(s, STORAGE_CLEANUP_CACHE_AND_OLD_SNAPSHOTS);
                }
            }
        }

        cui_row(ui, .width = CUI_FILL, .gap = 12) {
            cui_metric(ui, "快照备份", backup_size, .icon = CUI_ICON_HISTORY, .width = CUI_GROW(1));
            cui_metric(ui, "临时缓存", cache_size, .icon = CUI_ICON_DATABASE, .width = CUI_GROW(1));
            cui_metric(ui, "技能主仓", hub_size, .icon = ICON_MASTER_HUB, .width = CUI_GROW(1));
        }

        cui_card(ui, .width = CUI_FILL, .pad = 16, .gap = 8) {
            cui_row(ui, .width = CUI_FILL, .gap = 8, .align = CUI_ALIGN_CENTER) {
                cui_heading(ui, "临时缓存", 4);
                cui_spacer(ui);
                cui_push_id(ui, "storage_cache");
                if (cui_button(ui, "打开目录", .variant = CUI_BUTTON_GHOST,
                               .icon = CUI_ICON_FOLDER_OPEN, .size = CUI_SIZE_SM)) {
                    open_folder_in_explorer(cache_dir);
                }
                if (cui_button(ui, "清理缓存", .variant = CUI_BUTTON_SECONDARY,
                               .icon = CUI_ICON_TRASH_2, .size = CUI_SIZE_SM, .disabled = busy || !has_cache)) {
                    request_storage_cleanup(s, STORAGE_CLEANUP_CACHE);
                }
                cui_pop_id(ui);
            }
            cui_text(ui, cache_dir, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY, .width = CUI_FILL, .nowrap = true);
            cui_tooltip(ui, cache_dir);
            cui_textf(ui, "%d 个文件 · %d 个目录", st->cache_file_count, st->cache_dir_count);
        }

        cui_card(ui, .width = CUI_FILL, .pad = 16, .gap = 8) {
            cui_row(ui, .width = CUI_FILL, .gap = 8, .align = CUI_ALIGN_CENTER) {
                cui_heading(ui, "历史快照", 4);
                cui_spacer(ui);
                cui_push_id(ui, "storage_snapshots");
                if (cui_button(ui, "打开目录", .variant = CUI_BUTTON_GHOST,
                               .icon = CUI_ICON_FOLDER_OPEN, .size = CUI_SIZE_SM)) {
                    open_folder_in_explorer(backup_dir);
                }
                if (cui_button(ui, "清理旧快照", .variant = CUI_BUTTON_SECONDARY, .icon = CUI_ICON_TRASH_2,
                               .size = CUI_SIZE_SM, .disabled = busy || st->snapshot_count <= 3)) {
                    request_storage_cleanup(s, STORAGE_CLEANUP_OLD_SNAPSHOTS);
                }
                cui_menu(ui, "更多快照操作", .icon_only = true, .variant = CUI_BUTTON_GHOST) {
                    if (cui_menu_item(ui, "清空备份", .icon = CUI_ICON_TRASH_2,
                                      .danger = true, .disabled = busy || st->snapshot_count == 0)) {
                        request_storage_cleanup(s, STORAGE_CLEANUP_ALL_SNAPSHOTS);
                    }
                }
                cui_pop_id(ui);
            }
            cui_text(ui, backup_dir, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY, .width = CUI_FILL, .nowrap = true);
            cui_tooltip(ui, backup_dir);
            cui_textf(ui, "%d 份快照 · %d 个文件", st->snapshot_count, st->backup_file_count);

            if (st->snapshot_count == 0) {
                cui_separator(ui);
                cui_text(ui, "暂无快照", .size = 12, .color = CUI_COLOR_TEXT_MUTED);
            }

            for (int i = 0; i < st->snapshot_count; i++) {
                const storage_snapshot_item_t *item = &st->snapshots[i];
                char size[24], detail[96];
                format_size((size_t)item->size_bytes, size, sizeof(size));
                snprintf(detail, sizeof(detail), "%s · %s · %d 个文件", item->time_str, size, item->file_count);

                cui_push_id(ui, item->dir_name);
                cui_separator(ui);
                cui_row(ui, .width = CUI_FILL, .gap = 10, .align = CUI_ALIGN_CENTER) {
                    cui_icon(ui, CUI_ICON_HISTORY, 16, CUI_COLOR_TEXT_SECONDARY);
                    cui_column(ui, .width = CUI_FILL, .gap = 2) {
                        cui_text(ui, item->skill_name, .font = CUI_FONT_MEDIUM, .width = CUI_FILL, .nowrap = true);
                        cui_text(ui, detail, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);
                    }
                    cui_badge(ui, item->tool_name, .tone = CUI_TONE_NEUTRAL, .size = CUI_SIZE_SM);
                    if (cui_icon_button(ui, CUI_ICON_TRASH_2, .size = CUI_SIZE_SM, .tooltip = "删除快照",
                                        .color = CUI_COLOR_DANGER, .disabled = busy)) {
                        snprintf(s->storage_cleanup_snapshot, sizeof(s->storage_cleanup_snapshot), "%s", item->dir_name);
                        request_storage_cleanup(s, STORAGE_CLEANUP_SINGLE_SNAPSHOT);
                    }
                }
                cui_pop_id(ui);
            }
        }
    }

    render_storage_cleanup_modal(ui, s, busy);
}
