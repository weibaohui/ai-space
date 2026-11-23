#include "view_sync.h"
#include "view_common.h"
#include <stdio.h>

static void render_sync_result(cui_ctx *ui, app_state_t *s, const sync_drift_item_t *item, bool busy) {
    static const char *labels[] = {
        [SYNC_DRIFT_IN_SYNC] = "已一致",
        [SYNC_DRIFT_REMOTE_AHEAD] = "待拉取",
        [SYNC_DRIFT_LOCAL_AHEAD] = "待推送",
        [SYNC_DRIFT_REMOTE_ONLY] = "远端独有",
        [SYNC_DRIFT_LOCAL_ONLY] = "本地独有",
        [SYNC_DRIFT_CONFLICT] = "版本冲突",
    };
    static const cui_tone tones[] = {
        [SYNC_DRIFT_IN_SYNC] = CUI_TONE_SUCCESS,
        [SYNC_DRIFT_REMOTE_AHEAD] = CUI_TONE_INFO,
        [SYNC_DRIFT_LOCAL_AHEAD] = CUI_TONE_WARNING,
        [SYNC_DRIFT_REMOTE_ONLY] = CUI_TONE_INFO,
        [SYNC_DRIFT_LOCAL_ONLY] = CUI_TONE_WARNING,
        [SYNC_DRIFT_CONFLICT] = CUI_TONE_DANGER,
    };

    bool can_pull = item->status == SYNC_DRIFT_REMOTE_AHEAD || item->status == SYNC_DRIFT_REMOTE_ONLY;
    bool can_push = item->status == SYNC_DRIFT_LOCAL_AHEAD || item->status == SYNC_DRIFT_LOCAL_ONLY;
    cui_card(ui, .width = CUI_FILL, .pad = 12, .gap = 4) {
        cui_row(ui, .width = CUI_FILL, .gap = 10, .align = CUI_ALIGN_CENTER) {
            cui_icon(ui, ICON_SKILLS, 16, CUI_COLOR_TEXT_SECONDARY);
            cui_column(ui, .width = CUI_FILL, .gap = 2) {
                cui_text(ui, item->skill_name, .font = CUI_FONT_MEDIUM, .width = CUI_FILL, .nowrap = true);
                if (item->local_hash[0] || item->remote_hash[0]) {
                    char hashes[100];
                    snprintf(hashes, sizeof(hashes), "主仓指纹  %s\n远端指纹  %s",
                             item->local_hash[0] ? item->local_hash : "—",
                             item->remote_hash[0] ? item->remote_hash : "—");
                    cui_tooltip(ui, hashes);
                }
                cui_text(ui, item->status_desc, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY,
                         .width = CUI_FILL, .nowrap = true);
            }
            cui_badge(ui, labels[item->status], .tone = tones[item->status], .size = CUI_SIZE_SM);
            if (can_pull && cui_button(ui, "拉取", .variant = CUI_BUTTON_GHOST, .size = CUI_SIZE_SM,
                                       .icon = CUI_ICON_DOWNLOAD, .disabled = busy)) {
                start_sync_pull_skill_thread(s, item->skill_name);
            }
            if (can_push && cui_button(ui, "推送", .variant = CUI_BUTTON_GHOST, .size = CUI_SIZE_SM,
                                       .icon = CUI_ICON_UPLOAD, .disabled = busy)) {
                start_sync_push_skill_thread(s, item->skill_name);
            }
        }
    }
}

void render_sync_manager_page(cui_ctx *ui, app_state_t *s) {
    char endpoint[680];
    switch (s->sync_cfg.type) {
    case SYNC_BACKEND_GIT:
        snprintf(endpoint, sizeof(endpoint), "Git · %s · %s", s->sync_cfg.git_branch, s->sync_cfg.git_repo_url);
        break;
    case SYNC_BACKEND_WEBDAV:
        snprintf(endpoint, sizeof(endpoint), "WebDAV · %s", s->sync_cfg.webdav_url);
        break;
    case SYNC_BACKEND_NFS:
        snprintf(endpoint, sizeof(endpoint), "NFS · %s", s->sync_cfg.nfs_mount_path);
        break;
    default:
        snprintf(endpoint, sizeof(endpoint), "单机模式");
        break;
    }

    bool offline = s->sync_cfg.type == SYNC_BACKEND_NONE;
    bool busy = s->sync_is_busy || s->op_progress.active;
    cui_row(ui, .width = CUI_FILL, .gap = 12, .align = CUI_ALIGN_CENTER) {
        render_workspace_heading(ui, ICON_SYNC, "同步管理", endpoint);
        if (offline) {
            if (cui_button(ui, "配置远端", .variant = CUI_BUTTON_GHOST, .icon = CUI_ICON_SETTINGS)) {
                s->current_page = PAGE_SETTINGS;
                s->settings_active_tab = SETTINGS_TAB_REMOTE_SYNC;
            }
        } else if (cui_button(ui, "检查远端", .variant = CUI_BUTTON_GHOST,
                              .icon = CUI_ICON_REFRESH_CW, .disabled = busy)) {
            start_sync_check_thread(s);
        }
    }

    if (!offline && !busy && s->sync_status_msg[0]) {
        cui_box(ui, .width = CUI_FILL, .pad = 12, .radius = 8,
                .bg = cui_alpha(cui_color_of(ui, CUI_COLOR_DANGER), 0.12f)) {
            cui_row(ui, .width = CUI_FILL, .gap = 8, .align = CUI_ALIGN_CENTER) {
                cui_icon(ui, CUI_ICON_TRIANGLE_ALERT, 16, CUI_COLOR_DANGER);
                cui_text(ui, s->sync_status_msg, .width = CUI_FILL, .color = CUI_COLOR_DANGER);
            }
        }
    }

    if (offline || !s->sync_drift_loaded) {
        cui_card(ui, .width = CUI_FILL, .height = CUI_FILL, .pad = 24) {
            cui_column(ui, .width = CUI_FILL, .height = CUI_FILL,
                       .align = CUI_ALIGN_CENTER, .justify = CUI_JUSTIFY_CENTER) {
                if (offline) {
                    cui_empty_state(ui, "当前为单机模式", .icon = ICON_SYNC,
                                    .description = "配置远端后可在多台设备间共享主仓技能。");
                } else {
                    cui_empty_state(ui, s->sync_status_msg[0] ? "远端状态不可用" : "等待远端检查",
                                    .icon = CUI_ICON_CLOUD,
                                    .description = "检查后可查看哪些技能需要拉取、推送或处理冲突。");
                }
            }
        }
        return;
    }

    int to_pull = s->sync_drift.remote_ahead_count + s->sync_drift.remote_only_count;
    int to_push = s->sync_drift.local_ahead_count + s->sync_drift.local_only_count;
    int conflicts = s->sync_drift.conflict_count;
    char pull_count[24], push_count[24], conflict_count[24], matched_count[24];
    snprintf(pull_count, sizeof(pull_count), "%d", to_pull);
    snprintf(push_count, sizeof(push_count), "%d", to_push);
    snprintf(conflict_count, sizeof(conflict_count), "%d", conflicts);
    snprintf(matched_count, sizeof(matched_count), "%d", s->sync_drift.in_sync_count);

    cui_row(ui, .width = CUI_FILL, .gap = 12) {
        cui_metric(ui, "待拉取", pull_count, .icon = CUI_ICON_DOWNLOAD, .width = CUI_GROW(1));
        cui_metric(ui, "待推送", push_count, .icon = CUI_ICON_UPLOAD, .width = CUI_GROW(1));
        cui_metric(ui, "版本冲突", conflict_count, .icon = CUI_ICON_TRIANGLE_ALERT, .width = CUI_GROW(1));
        cui_metric(ui, "已一致", matched_count, .icon = CUI_ICON_CIRCLE_CHECK, .width = CUI_GROW(1));
    }

    cui_row(ui, .width = CUI_FILL, .gap = 8, .align = CUI_ALIGN_CENTER) {
        if (s->sync_drift.last_checked_at[0]) {
            char checked[72];
            snprintf(checked, sizeof(checked), "上次检查  %s", s->sync_drift.last_checked_at);
            cui_text(ui, checked, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);
        }
        cui_spacer(ui);
        if (cui_button(ui, "批量拉取", .variant = CUI_BUTTON_GHOST, .icon = CUI_ICON_DOWNLOAD,
                       .disabled = busy || to_pull == 0)) {
            start_sync_pull_all_thread(s);
        }
        if (cui_button(ui, "批量推送", .variant = CUI_BUTTON_GHOST, .icon = CUI_ICON_UPLOAD,
                       .disabled = busy || to_push == 0)) {
            start_sync_push_all_thread(s);
        }
    }

    if (s->sync_drift.count == 0 || s->sync_drift.in_sync_count == s->sync_drift.count) {
        cui_card(ui, .width = CUI_FILL, .height = CUI_FILL, .pad = 24) {
            cui_column(ui, .width = CUI_FILL, .height = CUI_FILL,
                       .align = CUI_ALIGN_CENTER, .justify = CUI_JUSTIFY_CENTER) {
                cui_empty_state(ui, "两端技能库已一致", .icon = CUI_ICON_CIRCLE_CHECK);
            }
        }
        return;
    }

    cui_scroll(ui, .id = "sync_result_list", .width = CUI_FILL, .height = CUI_FILL, .gap = 6) {
        for (int group = 0; group < 3; group++) {
            for (int i = 0; i < s->sync_drift.count; i++) {
                const sync_drift_item_t *item = &s->sync_drift.items[i];
                int priority = item->status == SYNC_DRIFT_CONFLICT ? 0 :
                               item->status == SYNC_DRIFT_IN_SYNC ? 2 : 1;
                if (group != priority) continue;
                cui_push_id_int(ui, i);
                render_sync_result(ui, s, item, busy);
                cui_pop_id(ui);
            }
        }
    }
}
