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

/* -------------------- Page UI Rendering -------------------- */

void render_storage_page(cui_ctx *ui, app_state_t *s) {
    if (!ui || !s) return;

    if (!s->storage_stats.loaded) {
        app_refresh_storage_stats(s);
    }

    storage_stats_t *st = &s->storage_stats;

    char exp_backup[512] = "";
    expand_path_local(s->setting_backup_dir[0] ? s->setting_backup_dir : "~/.ai-space/backups",
                      exp_backup, sizeof(exp_backup));

    char exp_cache[512] = "";
    expand_path_local("~/.ai-space/cache", exp_cache, sizeof(exp_cache));

    char exp_hub[512] = "";
    expand_path_local(s->setting_hub_dir[0] ? s->setting_hub_dir : "~/.ai-space/skills",
                      exp_hub, sizeof(exp_hub));

    cui_column(ui, .width = CUI_FILL, .gap = 16) {

        /* 1. Header Row */
        cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
            cui_row(ui, .gap = 12, .align = CUI_ALIGN_CENTER) {
                cui_box(ui, .width = CUI_PX(40), .height = CUI_PX(40), .radius = 8,
                        .bg = cui_color_of(ui, CUI_COLOR_ACCENT),
                        .dir = CUI_STACK, .align = CUI_ALIGN_CENTER, .justify = CUI_JUSTIFY_CENTER) {
                    cui_icon(ui, CUI_ICON_HARD_DRIVE, 20, CUI_COLOR_BACKGROUND);
                }
                cui_column(ui, .gap = 2) {
                    cui_heading(ui, "本地存储管理", 2);
                    cui_text(ui, "监控与管理备份历史快照、运行临时缓存及技能主仓物理存储",
                             .size = 13, .color = CUI_COLOR_TEXT_SECONDARY);
                }
            }

            cui_spacer(ui);

            cui_row(ui, .gap = 10, .align = CUI_ALIGN_CENTER) {
                if (cui_button(ui, "刷新统计", .variant = CUI_BUTTON_SECONDARY, .icon = CUI_ICON_REFRESH_CW)) {
                    app_refresh_storage_stats(s);
                    app_set_toast(s, "存储统计数据已刷新");
                }

                if (cui_button(ui, "一键清理", .variant = CUI_BUTTON_PRIMARY, .icon = CUI_ICON_TRASH_2)) {
                    app_clear_storage_cache(s);
                    app_prune_backups_keep_latest(s, 3);
                    app_set_toast(s, "已清空临时缓存并保留最新3份快照");
                }
            }
        }

        /* 2. Top Summary Metrics Row (3 Cards) */
        cui_row(ui, .width = CUI_FILL, .gap = 12) {
            /* Card 1: Backups */
            char backup_size_buf[64];
            format_size((size_t)st->backup_total_bytes, backup_size_buf, sizeof(backup_size_buf));
            cui_card(ui, .width = CUI_GROW(1), .pad = 16, .gap = 8) {
                cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                    cui_row(ui, .gap = 6, .align = CUI_ALIGN_CENTER) {
                        cui_icon(ui, CUI_ICON_SHIELD, 16, CUI_COLOR_ACCENT);
                        cui_text(ui, "快照备份占用", .size = 12, .color = CUI_COLOR_TEXT_MUTED);
                    }
                    cui_spacer(ui);
                    char count_sub[32];
                    snprintf(count_sub, sizeof(count_sub), "%d 个快照", st->snapshot_count);
                    cui_badge(ui, count_sub, .tone = CUI_TONE_ACCENT, .size = CUI_SIZE_SM);
                }
                cui_text(ui, backup_size_buf, .font = CUI_FONT_DISPLAY, .size = 24);
                cui_text(ui, "覆盖分发历史物理快照", .size = 11, .color = CUI_COLOR_TEXT_MUTED);
            }

            /* Card 2: Cache */
            char cache_size_buf[64];
            format_size((size_t)st->cache_total_bytes, cache_size_buf, sizeof(cache_size_buf));
            cui_card(ui, .width = CUI_GROW(1), .pad = 16, .gap = 8) {
                cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                    cui_row(ui, .gap = 6, .align = CUI_ALIGN_CENTER) {
                        cui_icon(ui, CUI_ICON_FOLDER_SYNC, 16, CUI_COLOR_ACCENT);
                        cui_text(ui, "运行缓存占用", .size = 12, .color = CUI_COLOR_TEXT_MUTED);
                    }
                    cui_spacer(ui);
                    char cache_sub[32];
                    snprintf(cache_sub, sizeof(cache_sub), "%d 个文件", st->cache_file_count);
                    cui_badge(ui, cache_sub, .tone = CUI_TONE_NEUTRAL, .size = CUI_SIZE_SM);
                }
                cui_text(ui, cache_size_buf, .font = CUI_FONT_DISPLAY, .size = 24);
                cui_text(ui, "Git远端同步与网络分片临时缓存", .size = 11, .color = CUI_COLOR_TEXT_MUTED);
            }

            /* Card 3: Master Hub */
            char hub_size_buf[64];
            format_size((size_t)st->hub_total_bytes, hub_size_buf, sizeof(hub_size_buf));
            cui_card(ui, .width = CUI_GROW(1), .pad = 16, .gap = 8) {
                cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                    cui_row(ui, .gap = 6, .align = CUI_ALIGN_CENTER) {
                        cui_icon(ui, CUI_ICON_ARCHIVE, 16, CUI_COLOR_ACCENT);
                        cui_text(ui, "技能主仓占用", .size = 12, .color = CUI_COLOR_TEXT_MUTED);
                    }
                    cui_spacer(ui);
                    char hub_sub[32];
                    snprintf(hub_sub, sizeof(hub_sub), "%d 个技能", st->hub_skill_count);
                    cui_badge(ui, hub_sub, .tone = CUI_TONE_SUCCESS, .size = CUI_SIZE_SM);
                }
                cui_text(ui, hub_size_buf, .font = CUI_FONT_DISPLAY, .size = 24);
                cui_text(ui, "中央基准技能库核心数据", .size = 11, .color = CUI_COLOR_TEXT_MUTED);
            }
        }

        /* 3. Section: 运行缓存清理 Card */
        cui_box(ui, .width = CUI_FILL, .radius = 8, .pad = 16, .gap = 12,
                .bg = cui_color_of(ui, CUI_COLOR_SURFACE_RAISED),
                .border = 1, .border_color = cui_color_of(ui, CUI_COLOR_BORDER)) {

            cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                    cui_icon(ui, CUI_ICON_FOLDER_SYNC, 18, CUI_COLOR_ACCENT);
                    cui_heading(ui, "本地临时缓存清理", 4);
                }
                cui_spacer(ui);
                cui_badge(ui, (st->cache_total_bytes > 0) ? "可清理" : "已清空",
                          .tone = (st->cache_total_bytes > 0) ? CUI_TONE_INFO : CUI_TONE_SUCCESS,
                          .size = CUI_SIZE_SM);
            }

            cui_text(ui, "存储 Git 远端协同版本库工作副本、网络临时下载分片等数据。清理后不会影响任何本地技能，下次同步时将按需重新生成。",
                     .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);

            /* Path & Details Box */
            cui_box(ui, .width = CUI_FILL, .radius = 6, .pad = 10, .gap = 6,
                    .bg = cui_alpha(cui_color_of(ui, CUI_COLOR_SURFACE), 0.6f),
                    .border = 1, .border_color = cui_color_of(ui, CUI_COLOR_BORDER)) {
                cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                    cui_icon(ui, CUI_ICON_FOLDER, 13, CUI_COLOR_TEXT_MUTED);
                    cui_text(ui, "物理缓存目录:", .size = 12, .color = CUI_COLOR_TEXT_MUTED);
                    cui_text(ui, exp_cache, .size = 12, .color = CUI_COLOR_FOREGROUND);
                }
                cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                    cui_icon(ui, CUI_ICON_DATABASE, 13, CUI_COLOR_TEXT_MUTED);
                    char cache_dtl[128];
                    snprintf(cache_dtl, sizeof(cache_dtl), "当前占用: %s  (包含 %d 个文件，%d 个子目录)",
                             cache_size_buf, st->cache_file_count, st->cache_dir_count);
                    cui_text(ui, cache_dtl, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);
                }
            }

            /* Action Buttons */
            cui_row(ui, .gap = 10, .align = CUI_ALIGN_CENTER) {
                if (cui_button(ui, "清理缓存", .variant = CUI_BUTTON_DANGER, .icon = CUI_ICON_TRASH_2)) {
                    app_clear_storage_cache(s);
                    app_set_toast(s, "本地运行缓存已清理完成");
                }

                if (cui_button(ui, "打开目录", .variant = CUI_BUTTON_SECONDARY, .icon = CUI_ICON_FOLDER)) {
                    open_folder_in_explorer(exp_cache);
                }
            }
        }

        /* 4. Section: 快照备份管理 Card */
        cui_box(ui, .width = CUI_FILL, .radius = 8, .pad = 16, .gap = 12,
                .bg = cui_color_of(ui, CUI_COLOR_SURFACE_RAISED),
                .border = 1, .border_color = cui_color_of(ui, CUI_COLOR_BORDER)) {

            cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                    cui_icon(ui, CUI_ICON_SHIELD, 18, CUI_COLOR_ACCENT);
                    cui_heading(ui, "覆盖安全快照备份管理", 4);
                }
                cui_spacer(ui);
                char snap_badge[32];
                snprintf(snap_badge, sizeof(snap_badge), "%d 个快照", st->snapshot_count);
                cui_badge(ui, snap_badge,
                          .tone = (st->snapshot_count > 0) ? CUI_TONE_ACCENT : CUI_TONE_NEUTRAL,
                          .size = CUI_SIZE_SM);
            }

            cui_text(ui, "在对本地工具覆盖分发或从工具合入主仓前系统强制创建的历史物理快照。支持一键保留最新或批量清理，释放磁盘空间。",
                     .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);

            /* Path & Details Box */
            cui_box(ui, .width = CUI_FILL, .radius = 6, .pad = 10, .gap = 6,
                    .bg = cui_alpha(cui_color_of(ui, CUI_COLOR_SURFACE), 0.6f),
                    .border = 1, .border_color = cui_color_of(ui, CUI_COLOR_BORDER)) {
                cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                    cui_icon(ui, CUI_ICON_FOLDER, 13, CUI_COLOR_TEXT_MUTED);
                    cui_text(ui, "快照存储路径:", .size = 12, .color = CUI_COLOR_TEXT_MUTED);
                    cui_text(ui, exp_backup, .size = 12, .color = CUI_COLOR_FOREGROUND);
                }
                cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                    cui_icon(ui, CUI_ICON_DATABASE, 13, CUI_COLOR_TEXT_MUTED);
                    char backup_dtl[128];
                    snprintf(backup_dtl, sizeof(backup_dtl), "当前占用: %s  (共 %d 个快照，%d 个文件)",
                             backup_size_buf, st->snapshot_count, st->backup_file_count);
                    cui_text(ui, backup_dtl, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);
                }
            }

            /* Action Buttons */
            cui_row(ui, .gap = 10, .align = CUI_ALIGN_CENTER) {
                if (cui_button(ui, "保留最新", .variant = CUI_BUTTON_PRIMARY, .icon = CUI_ICON_CHECK)) {
                    app_prune_backups_keep_latest(s, 3);
                    app_set_toast(s, "已保留最新3份快照，其余旧快照已清理");
                }

                if (cui_button(ui, "清空备份", .variant = CUI_BUTTON_DANGER, .icon = CUI_ICON_TRASH_2)) {
                    app_clear_all_backups(s);
                    app_set_toast(s, "所有历史备份快照已清空");
                }

                if (cui_button(ui, "打开目录", .variant = CUI_BUTTON_SECONDARY, .icon = CUI_ICON_FOLDER)) {
                    open_folder_in_explorer(exp_backup);
                }
            }

            cui_separator(ui);

            /* Snapshot Item List */
            cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                cui_text(ui, "历史备份快照清单", .size = 13, .color = CUI_COLOR_TEXT_MUTED);
                cui_spacer(ui);
                char cnt_lbl[64];
                snprintf(cnt_lbl, sizeof(cnt_lbl), "按时间倒序排列 (共 %d 项)", st->snapshot_count);
                cui_text(ui, cnt_lbl, .size = 12, .color = CUI_COLOR_TEXT_MUTED);
            }

            if (st->snapshot_count == 0) {
                cui_column(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER, .pad = 30, .gap = 10) {
                    cui_icon(ui, CUI_ICON_SHIELD_CHECK, 36, CUI_COLOR_SUCCESS);
                    cui_heading(ui, "暂无备份快照", 4);
                    cui_text(ui, "当前备份目录干净，未占用多余磁盘空间。",
                             .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);
                }
            } else {
                for (int i = 0; i < st->snapshot_count; i++) {
                    storage_snapshot_item_t *item = &st->snapshots[i];

                    char sz_buf[32];
                    format_size((size_t)item->size_bytes, sz_buf, sizeof(sz_buf));

                    cui_box(ui, .width = CUI_FILL, .radius = 6, .pad = 10,
                            .bg = cui_color_of(ui, CUI_COLOR_SURFACE),
                            .border = 1, .border_color = cui_color_of(ui, CUI_COLOR_BORDER)) {
                        cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                            /* Icon & Time */
                            cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER, .width = CUI_PX(180)) {
                                cui_icon(ui, CUI_ICON_CLOCK, 14, CUI_COLOR_ACCENT);
                                cui_text(ui, item->time_str, .size = 12, .font = CUI_FONT_MONO);
                            }

                            /* Tool badge */
                            cui_box(ui, .width = CUI_PX(120), .align = CUI_ALIGN_START) {
                                cui_badge(ui, item->tool_name, .tone = CUI_TONE_INFO, .size = CUI_SIZE_SM);
                            }

                            /* Skill Name */
                            cui_row(ui, .gap = 6, .align = CUI_ALIGN_CENTER, .width = CUI_PX(220)) {
                                cui_icon(ui, CUI_ICON_PACKAGE, 14, CUI_COLOR_TEXT_SECONDARY);
                                cui_text(ui, item->skill_name, .size = 13, .color = CUI_COLOR_FOREGROUND);
                            }

                            /* File size & count */
                            char size_info[64];
                            snprintf(size_info, sizeof(size_info), "%s (%d 个文件)", sz_buf, item->file_count);
                            cui_text(ui, size_info, .size = 12, .color = CUI_COLOR_TEXT_MUTED);

                            cui_spacer(ui);

                            /* Delete Single Item */
                            if (cui_button(ui, "删除", .variant = CUI_BUTTON_OUTLINE, .size = CUI_SIZE_SM, .icon = CUI_ICON_TRASH)) {
                                app_delete_single_snapshot(s, item->dir_name);
                                app_set_toast(s, "快照已成功删除");
                            }
                        }
                    }
                }
            }
        }
    }
}
