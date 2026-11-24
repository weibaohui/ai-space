#include "view_storage.h"
#include "view_common.h"
#include "file_tree.h"

#include <stdio.h>
#include <stdlib.h>

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

static const char *backup_path(const app_state_t *s) {
    return s->setting_backup_dir[0] ? s->setting_backup_dir : STORAGE_DEFAULT_BACKUP;
}

static void refresh_storage(app_state_t *s) {
    const char *hub = s->setting_hub_dir[0] ? s->setting_hub_dir : STORAGE_DEFAULT_HUB;
    storage_collect(hub, backup_path(s), STORAGE_DEFAULT_CACHE, &s->storage_stats);
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
                    int rc = 0;
                    switch (s->storage_cleanup_action) {
                    case STORAGE_CLEANUP_CACHE:
                        rc = storage_clear_directory(STORAGE_DEFAULT_CACHE);
                        break;
                    case STORAGE_CLEANUP_OLD_SNAPSHOTS:
                        rc = storage_prune_snapshots(backup_path(s), 3);
                        break;
                    case STORAGE_CLEANUP_ALL_SNAPSHOTS:
                        rc = storage_prune_snapshots(backup_path(s), 0);
                        break;
                    case STORAGE_CLEANUP_SINGLE_SNAPSHOT:
                        rc = storage_delete_snapshot(backup_path(s), s->storage_cleanup_snapshot);
                        break;
                    case STORAGE_CLEANUP_CACHE_AND_OLD_SNAPSHOTS: {
                        int cache_rc = storage_clear_directory(STORAGE_DEFAULT_CACHE);
                        int snapshot_rc = storage_prune_snapshots(backup_path(s), 3);
                        rc = cache_rc != 0 || snapshot_rc != 0 ? -1 : 0;
                        break;
                    }
                    }
                    app_storage_invalidate(s);
                    refresh_storage(s);
                    s->show_storage_cleanup_modal = false;
                    cui_toast(ui, rc == 0 ? "存储统计已更新" : "部分存储清理未完成",
                              .tone = rc == 0 ? CUI_TONE_INFO : CUI_TONE_WARNING);
                }
            }
        }
    }
}

void render_storage_page(cui_ctx *ui, app_state_t *s) {
    if (!s->storage_stats.loaded) refresh_storage(s);

    const storage_stats_t *st = &s->storage_stats;
    bool busy = s->hub_scan.is_scanning || s->sync_is_busy || s->op_progress.active;
    bool has_cache = st->cache_file_count > 0 || st->cache_dir_count > 0;
    char backup_dir[SYNC_PATH_CAP] = "", cache_dir[SYNC_PATH_CAP] = "";
    if (sync_expand(backup_path(s), backup_dir, sizeof(backup_dir)) != 0) backup_dir[0] = '\0';
    if (sync_expand(STORAGE_DEFAULT_CACHE, cache_dir, sizeof(cache_dir)) != 0) cache_dir[0] = '\0';

    char backup_size[32], cache_size[32], hub_size[32];
    format_size((size_t)st->backup_total_bytes, backup_size, sizeof(backup_size));
    format_size((size_t)st->cache_total_bytes, cache_size, sizeof(cache_size));
    format_size((size_t)st->hub_total_bytes, hub_size, sizeof(hub_size));

    cui_column(ui, .width = CUI_FILL, .gap = 14) {
        cui_row(ui, .width = CUI_FILL, .gap = 8, .align = CUI_ALIGN_CENTER) {
            render_workspace_heading(ui, CUI_ICON_DATABASE, "本地存储", "管理临时缓存与历史快照");
            if (cui_button(ui, "刷新统计", .variant = CUI_BUTTON_SECONDARY,
                           .icon = CUI_ICON_REFRESH_CW, .disabled = busy)) {
                refresh_storage(s);
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
            cui_textf(ui, "%zu 份快照 · %d 个文件", st->snapshot_total, st->backup_file_count);

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
