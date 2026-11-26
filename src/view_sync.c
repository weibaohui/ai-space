#include "app_sync.h"
#include "view_sync.h"
#include "view_common.h"
#include <cui/cui_app.h>
#include <stdio.h>
#include <string.h>

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
            if (item->status == SYNC_DRIFT_CONFLICT &&
                cui_button(ui, "处理冲突", .variant = CUI_BUTTON_GHOST, .size = CUI_SIZE_SM,
                           .icon = CUI_ICON_TRIANGLE_ALERT, .disabled = busy)) {
                app_sync_open_conflict(s, item->skill_name);
            }
        }
    }
}

/* 覆盖操作需要用户先选择保留的一侧，最终写入仍会校验指纹。 */
static void open_merge_workspace(cui_ctx *ui, const char *path) {
    static const char hex[] = "0123456789ABCDEF";
    char url[3 * SYNC_PATH_CAP + 10];
    size_t used = (size_t)snprintf(url, sizeof(url), "%s", path[0] == '/' ? "file://" : "file:///");
    for (const unsigned char *p = (const unsigned char *)path; *p; p++) {
        unsigned char c = *p;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || strchr("-._~/:", c)) url[used++] = (char)c;
        else {
            url[used++] = '%';
            url[used++] = hex[c >> 4];
            url[used++] = hex[c & 15];
        }
    }
    url[used] = '\0';
    if (!cui_app_open_url(url)) {
        cui_set_clipboard_text(ui, path);
        cui_toast(ui, "无法打开文件管理器，已复制工作区路径", .tone = CUI_TONE_WARNING);
    }
}

static void render_conflict_dialog(cui_ctx *ui, app_state_t *s, bool busy) {
    if (!s->show_sync_conflict_modal) return;
    const sync_git_draft_t *draft = &s->sync_conflict_draft;
    bool has_draft = draft->workspace[0] != '\0';
    cui_dialog(ui, "处理技能冲突", &s->show_sync_conflict_modal, .width = 540) {
        cui_column(ui, .width = CUI_FILL, .gap = 12) {
            cui_text(ui, s->sync_conflict_skill, .width = CUI_FILL, .font = CUI_FONT_MEDIUM);
            if (has_draft) {
                if (draft->conflicts[0]) {
                    cui_text(ui, "打开工作区，在编辑器中解决列出的文件冲突，再暂存解决结果。",
                             .width = CUI_FILL);
                    cui_scroll(ui, .id = "merge_conflict_files", .width = CUI_FILL, .height = CUI_PX(82)) {
                        cui_text(ui, draft->conflicts, .width = CUI_FILL);
                    }
                } else {
                    cui_text(ui, "合并草稿已准备。可以在工作区检查结果，确认后发布并同步到本地主仓。",
                             .width = CUI_FILL);
                }
                cui_row(ui, .width = CUI_FILL, .gap = 8, .wrap = true) {
                    if (cui_button(ui, "打开工作区", .variant = CUI_BUTTON_OUTLINE, .icon = CUI_ICON_FOLDER_OPEN))
                        open_merge_workspace(ui, draft->workspace);
                    if (cui_button(ui, "复制工作区路径", .variant = CUI_BUTTON_GHOST))
                        cui_set_clipboard_text(ui, draft->workspace);
                }
                cui_row(ui, .width = CUI_FILL, .gap = 8, .wrap = true) {
                    if (cui_button(ui, "放弃草稿（保留文件）", .variant = CUI_BUTTON_GHOST, .disabled = busy))
                        app_sync_discard_git_merge(s, s->sync_conflict_skill);
                    cui_spacer(ui);
                    if (!draft->result_oid[0] &&
                        cui_button(ui, draft->conflicts[0] ? "暂存解决结果" : "暂存修改",
                                   .variant = draft->conflicts[0] ? CUI_BUTTON_PRIMARY : CUI_BUTTON_OUTLINE,
                                   .disabled = busy))
                        start_sync_git_merge_stage_thread(s, s->sync_conflict_skill);
                    if (!draft->conflicts[0] &&
                        cui_button(ui, "发布合并结果", .variant = CUI_BUTTON_PRIMARY, .disabled = busy))
                        start_sync_git_merge_thread(s, s->sync_conflict_skill, true);
                }
            } else {
                if (s->sync_cfg.type == SYNC_BACKEND_GIT) {
                    cui_text(ui, "用共同版本尝试合并，先生成草稿，确认后再发布。", .width = CUI_FILL);
                    if (cui_button(ui, "尝试合并", .variant = CUI_BUTTON_PRIMARY, .disabled = busy))
                        start_sync_git_merge_thread(s, s->sync_conflict_skill, false);
                }
                cui_text(ui, "保留主仓将覆盖远端副本。保留远端将覆盖本地主仓副本。", .width = CUI_FILL);
                cui_row(ui, .width = CUI_FILL, .gap = 8, .justify = CUI_JUSTIFY_END) {
                    if (cui_button(ui, "取消", .variant = CUI_BUTTON_GHOST))
                        s->show_sync_conflict_modal = false;
                    if (cui_button(ui, "保留主仓并推送", .variant = CUI_BUTTON_OUTLINE, .disabled = busy)) {
                        s->show_sync_conflict_modal = false;
                        start_sync_resolve_conflict_thread(s, s->sync_conflict_skill, false);
                    }
                    if (cui_button(ui, "保留远端并拉取", .variant = CUI_BUTTON_OUTLINE, .disabled = busy)) {
                        s->show_sync_conflict_modal = false;
                        start_sync_resolve_conflict_thread(s, s->sync_conflict_skill, true);
                    }
                }
            }
        }
    }
}

void render_sync_manager_page(cui_ctx *ui, app_state_t *s) {
    if (!s->sync_drafts_loaded) app_sync_refresh_git_drafts(s);
    char endpoint[1060];
    switch (s->sync_cfg.type) {
    case SYNC_BACKEND_GIT:
        snprintf(endpoint, sizeof(endpoint), "Git · %s · %s", s->sync_cfg.git_branch, s->sync_cfg.git_repo_url);
        break;
    case SYNC_BACKEND_WEBDAV:
        snprintf(endpoint, sizeof(endpoint), "WebDAV · %s", s->sync_cfg.webdav_url);
        break;
    case SYNC_BACKEND_NFS:
        if (s->sync_cfg.share_mode == SYNC_SHARE_MOUNTED) {
            snprintf(endpoint, sizeof(endpoint), "共享目录 · %s", s->sync_cfg.nfs_mount_path);
        } else {
            bool nfs = s->sync_cfg.share_mode == SYNC_SHARE_NFS;
            snprintf(endpoint, sizeof(endpoint), "%s · %s%s%s%s%.180s", nfs ? "NFS" : "SMB",
                     s->sync_cfg.share_server, nfs ? ":" : "/", s->sync_cfg.share_export,
                     s->sync_cfg.share_subdir[0] ? "/" : "", s->sync_cfg.share_subdir);
        }
        break;
    default:
        snprintf(endpoint, sizeof(endpoint), "单机模式");
        break;
    }

    bool offline = s->sync_cfg.type == SYNC_BACKEND_NONE;
    bool busy = s->sync_is_busy || s->op_progress.active || s->hub_scan.is_scanning;
    cui_row(ui, .width = CUI_FILL, .gap = 12, .align = CUI_ALIGN_CENTER) {
        render_workspace_heading(ui, ICON_SYNC, "同步管理", endpoint);
        if (s->sync_pending_merge_skill[0] &&
            cui_button(ui, "合并草稿", .variant = CUI_BUTTON_OUTLINE,
                       .icon = CUI_ICON_GIT_BRANCH, .disabled = busy))
            app_sync_open_conflict(s, s->sync_pending_merge_skill);
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
        render_conflict_dialog(ui, s, busy);
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
        render_conflict_dialog(ui, s, busy);
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
    render_conflict_dialog(ui, s, busy);
}
