#include "view_settings.h"
#include "view_common.h"
#include "db.h"
#include "hub_manager.h"
#include "sync_backend.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>
#include <dirent.h>

static void expand_path_helper(const char *in, char *out, size_t cap) {
    if (!in || in[0] == '\0') {
        if (out && cap > 0) out[0] = '\0';
        return;
    }
    if (in[0] == '~' && (in[1] == '/' || in[1] == '\0')) {
        const char *home = getenv("HOME");
        if (!home || home[0] == '\0') {
            struct passwd *pw = getpwuid(getuid());
            if (pw) home = pw->pw_dir;
        }
        if (home) {
            snprintf(out, cap, "%s%s", home, in + 1);
            return;
        }
    }
    strncpy(out, in, cap - 1);
    out[cap - 1] = '\0';
}

static void get_dir_stat_info(const char *raw_path, char *out_expanded, size_t exp_cap,
                              char *out_info, size_t info_cap, bool *out_exists, int *out_subdirs) {
    expand_path_helper(raw_path, out_expanded, exp_cap);

    if (out_subdirs) *out_subdirs = 0;

    if (!out_expanded || out_expanded[0] == '\0') {
        if (out_info && info_cap > 0) snprintf(out_info, info_cap, "未配置路径");
        if (out_exists) *out_exists = false;
        return;
    }

    struct stat st;
    if (stat(out_expanded, &st) == 0 && S_ISDIR(st.st_mode)) {
        if (out_exists) *out_exists = true;

        /* Count child directories */
        int count = 0;
        DIR *d = opendir(out_expanded);
        if (d) {
            struct dirent *ent;
            while ((ent = readdir(d)) != NULL) {
                if (ent->d_name[0] == '.') continue;
                char sub_path[1024];
                snprintf(sub_path, sizeof(sub_path), "%s/%s", out_expanded, ent->d_name);
                struct stat sub_st;
                if (stat(sub_path, &sub_st) == 0 && S_ISDIR(sub_st.st_mode)) {
                    count++;
                }
            }
            closedir(d);
        }
        if (out_subdirs) *out_subdirs = count;

        if (access(out_expanded, W_OK | R_OK) == 0) {
            snprintf(out_info, info_cap, "物理目录已就绪 (可读写，含 %d 个子目录项)", count);
        } else {
            snprintf(out_info, info_cap, "物理目录已存在 (读写权限受限)");
        }
    } else {
        if (out_exists) *out_exists = false;
        snprintf(out_info, info_cap, "目录尚未在磁盘创建 (保存后系统将自动初始化)");
    }
}

static void render_tab_storage(cui_ctx *ui, app_state_t *s) {
    char hub_expanded[512] = "";
    char hub_info[256] = "";
    bool hub_exists = false;
    int hub_subdirs = 0;
    get_dir_stat_info(s->setting_hub_dir, hub_expanded, sizeof(hub_expanded),
                      hub_info, sizeof(hub_info), &hub_exists, &hub_subdirs);

    char backup_expanded[512] = "";
    char backup_info[256] = "";
    bool backup_exists = false;
    int backup_subdirs = 0;
    get_dir_stat_info(s->setting_backup_dir, backup_expanded, sizeof(backup_expanded),
                      backup_info, sizeof(backup_info), &backup_exists, &backup_subdirs);

    cui_column(ui, .width = CUI_FILL, .gap = 20) {
        /* Section 1: Master Hub Repository Directory */
        cui_box(ui, .width = CUI_FILL, .radius = 8, .pad = 16, .gap = 12,
                .bg = cui_color_of(ui, CUI_COLOR_SURFACE_RAISED),
                .border = 1, .border_color = cui_color_of(ui, CUI_COLOR_BORDER)) {
            cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                    cui_icon(ui, CUI_ICON_ARCHIVE, 18, CUI_COLOR_ACCENT);
                    cui_heading(ui, "主仓存储路径", 4);
                }
                cui_spacer(ui);
                cui_badge(ui, hub_exists ? "已就绪" : "待初始化",
                          .tone = hub_exists ? CUI_TONE_SUCCESS : CUI_TONE_NEUTRAL,
                          .size = CUI_SIZE_SM);
            }

            cui_text(ui, "标准化基准技能物理存储根目录",
                     .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);

            /* Path input field */
            cui_input(ui, "input_setting_hub_dir", s->setting_hub_dir, sizeof(s->setting_hub_dir),
                      .placeholder = "~/.ai-space/skills",
                      .icon = CUI_ICON_FOLDER,
                      .width = CUI_FILL);

            /* Real disk info card */
            cui_box(ui, .width = CUI_FILL, .radius = 6, .pad = 10, .gap = 4,
                    .bg = cui_alpha(cui_color_of(ui, CUI_COLOR_SURFACE), 0.6f),
                    .border = 1, .border_color = cui_color_of(ui, CUI_COLOR_BORDER)) {
                cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                    cui_icon(ui, CUI_ICON_INFO, 13, CUI_COLOR_TEXT_MUTED);
                    cui_text(ui, "实际磁盘解析绝对路径:", .size = 12, .color = CUI_COLOR_TEXT_MUTED);
                    cui_text(ui, hub_expanded[0] ? hub_expanded : "(未配置)",
                             .size = 12, .color = CUI_COLOR_FOREGROUND);
                }
                cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                    cui_icon(ui, CUI_ICON_SHIELD, 13, CUI_COLOR_TEXT_MUTED);
                    cui_text(ui, hub_info, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);
                }
            }

            /* Action Buttons */
            cui_row(ui, .gap = 10, .align = CUI_ALIGN_CENTER) {
                if (cui_button(ui, "保存路径", .variant = CUI_BUTTON_PRIMARY, .icon = CUI_ICON_SAVE)) {
                    app_save_settings(s);
                    s->hub_matrix_loaded = false;
                    char toast[256];
                    snprintf(toast, sizeof(toast), "主仓路径已更新为: %s", s->setting_hub_dir);
                    app_set_toast(s, toast);
                }

                if (cui_button(ui, "恢复默认", .variant = CUI_BUTTON_SECONDARY, .icon = CUI_ICON_UNDO_2)) {
                    strncpy(s->setting_hub_dir, "~/.ai-space/skills", sizeof(s->setting_hub_dir) - 1);
                    s->setting_hub_dir[sizeof(s->setting_hub_dir) - 1] = '\0';
                    app_save_settings(s);
                    s->hub_matrix_loaded = false;
                    app_set_toast(s, "已恢复主仓默认路径: ~/.ai-space/skills");
                }
            }
        }

        /* Section 2: Snapshot Backup Directory */
        cui_box(ui, .width = CUI_FILL, .radius = 8, .pad = 16, .gap = 12,
                .bg = cui_color_of(ui, CUI_COLOR_SURFACE_RAISED),
                .border = 1, .border_color = cui_color_of(ui, CUI_COLOR_BORDER)) {
            cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                    cui_icon(ui, CUI_ICON_SHIELD, 18, CUI_COLOR_ACCENT);
                    cui_heading(ui, "快照备份存储路径", 4);
                }
                cui_spacer(ui);
                cui_badge(ui, backup_exists ? "正常" : "待初始化",
                          .tone = backup_exists ? CUI_TONE_SUCCESS : CUI_TONE_NEUTRAL,
                          .size = CUI_SIZE_SM);
            }

            cui_text(ui, "覆盖或同步修改前自动归档时间戳历史快照，保障数据安全",
                     .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);

            /* Path input field */
            cui_input(ui, "input_setting_backup_dir", s->setting_backup_dir, sizeof(s->setting_backup_dir),
                      .placeholder = "~/.ai-space/backups",
                      .icon = CUI_ICON_FOLDER,
                      .width = CUI_FILL);

            /* Real disk info card */
            cui_box(ui, .width = CUI_FILL, .radius = 6, .pad = 10, .gap = 4,
                    .bg = cui_alpha(cui_color_of(ui, CUI_COLOR_SURFACE), 0.6f),
                    .border = 1, .border_color = cui_color_of(ui, CUI_COLOR_BORDER)) {
                cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                    cui_icon(ui, CUI_ICON_INFO, 13, CUI_COLOR_TEXT_MUTED);
                    cui_text(ui, "实际磁盘解析绝对路径:", .size = 12, .color = CUI_COLOR_TEXT_MUTED);
                    cui_text(ui, backup_expanded[0] ? backup_expanded : "(未配置)",
                             .size = 12, .color = CUI_COLOR_FOREGROUND);
                }
                cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                    cui_icon(ui, CUI_ICON_SHIELD, 13, CUI_COLOR_TEXT_MUTED);
                    cui_text(ui, backup_info, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);
                }
            }

            /* Action Buttons */
            cui_row(ui, .gap = 10, .align = CUI_ALIGN_CENTER) {
                if (cui_button(ui, "保存路径", .variant = CUI_BUTTON_PRIMARY, .icon = CUI_ICON_SAVE)) {
                    app_save_settings(s);
                    char toast[256];
                    snprintf(toast, sizeof(toast), "备份路径已更新为: %s", s->setting_backup_dir);
                    app_set_toast(s, toast);
                }

                if (cui_button(ui, "恢复默认", .variant = CUI_BUTTON_SECONDARY, .icon = CUI_ICON_UNDO_2)) {
                    strncpy(s->setting_backup_dir, "~/.ai-space/backups", sizeof(s->setting_backup_dir) - 1);
                    s->setting_backup_dir[sizeof(s->setting_backup_dir) - 1] = '\0';
                    app_save_settings(s);
                    app_set_toast(s, "已恢复备份默认路径: ~/.ai-space/backups");
                }
            }
        }
    }
}

static void render_tab_remote_sync(cui_ctx *ui, app_state_t *s) {
    cui_column(ui, .width = CUI_FILL, .gap = 16) {
        cui_box(ui, .width = CUI_FILL, .radius = 8, .pad = 16, .gap = 12,
                .bg = cui_color_of(ui, CUI_COLOR_SURFACE_RAISED),
                .border = 1, .border_color = cui_color_of(ui, CUI_COLOR_BORDER)) {
            cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                    cui_icon(ui, CUI_ICON_GLOBE, 18, CUI_COLOR_ACCENT);
                    cui_heading(ui, "跨主机远端同步服务", 4);
                }
                cui_spacer(ui);
                char act_lbl[128];
                snprintf(act_lbl, sizeof(act_lbl), "当前生效: %s", sync_backend_display_name(s->sync_cfg.type));
                cui_badge(ui, act_lbl,
                          .tone = (s->sync_cfg.type != SYNC_BACKEND_NONE) ? CUI_TONE_ACCENT : CUI_TONE_NEUTRAL,
                          .size = CUI_SIZE_SM);
            }

            cui_text(ui, "选择远端同步后端。配置相同后端参数的多台主机将自动共享云端主仓并协同版本。",
                     .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);

            /* Radio-style Backend Selection (Strictly 1 Active Backend) */
            cui_row(ui, .width = CUI_FILL, .gap = 10, .align = CUI_ALIGN_CENTER) {
                bool is_cand_none = (s->setting_sync_candidate_type == SYNC_BACKEND_NONE);
                if (cui_button(ui, "单机模式",
                               .variant = is_cand_none ? CUI_BUTTON_PRIMARY : CUI_BUTTON_SECONDARY,
                               .icon = CUI_ICON_BOX)) {
                    s->setting_sync_candidate_type = SYNC_BACKEND_NONE;
                    s->sync_has_checked_candidate = false;
                }

                bool is_cand_webdav = (s->setting_sync_candidate_type == SYNC_BACKEND_WEBDAV);
                if (cui_button(ui, "网盘同步",
                               .variant = is_cand_webdav ? CUI_BUTTON_PRIMARY : CUI_BUTTON_SECONDARY,
                               .icon = CUI_ICON_GLOBE)) {
                    s->setting_sync_candidate_type = SYNC_BACKEND_WEBDAV;
                    s->sync_has_checked_candidate = false;
                }

                bool is_cand_nfs = (s->setting_sync_candidate_type == SYNC_BACKEND_NFS);
                if (cui_button(ui, "局域共享",
                               .variant = is_cand_nfs ? CUI_BUTTON_PRIMARY : CUI_BUTTON_SECONDARY,
                               .icon = CUI_ICON_FOLDER)) {
                    s->setting_sync_candidate_type = SYNC_BACKEND_NFS;
                    s->sync_has_checked_candidate = false;
                }

                bool is_cand_git = (s->setting_sync_candidate_type == SYNC_BACKEND_GIT);
                if (cui_button(ui, "Git库",
                               .variant = is_cand_git ? CUI_BUTTON_PRIMARY : CUI_BUTTON_SECONDARY,
                               .icon = CUI_ICON_GIT_BRANCH)) {
                    s->setting_sync_candidate_type = SYNC_BACKEND_GIT;
                    s->sync_has_checked_candidate = false;
                }
            }
        }

        /* Backend Specific Configuration Card */
        cui_box(ui, .width = CUI_FILL, .radius = 8, .pad = 16, .gap = 14,
                .bg = cui_color_of(ui, CUI_COLOR_SURFACE_RAISED),
                .border = 1, .border_color = cui_color_of(ui, CUI_COLOR_BORDER)) {

            bool is_active = (s->sync_cfg.type == s->setting_sync_candidate_type);

            if (s->setting_sync_candidate_type == SYNC_BACKEND_NONE) {
                cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                    cui_heading(ui, "单机本地模式", 4);
                    cui_spacer(ui);
                    cui_badge(ui, is_active ? "已生效" : "未激活",
                              .tone = is_active ? CUI_TONE_SUCCESS : CUI_TONE_NEUTRAL,
                              .size = CUI_SIZE_SM);
                }

                cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                    cui_icon(ui, CUI_ICON_INFO, 16, CUI_COLOR_TEXT_MUTED);
                    cui_text(ui, "仅在本地主仓与各个本地工具间流转，不与远端服务器同步。",
                             .size = 12, .color = CUI_COLOR_TEXT_MUTED);
                }

                cui_row(ui, .gap = 12, .align = CUI_ALIGN_CENTER) {
                    if (cui_button(ui, "激活模式", .variant = CUI_BUTTON_PRIMARY, .icon = CUI_ICON_SAVE)) {
                        s->sync_cfg.type = SYNC_BACKEND_NONE;
                        app_save_settings(s);
                        s->sync_drift_loaded = false;
                        app_set_toast(s, "已成功激活单机本地模式");
                    }
                }

            } else if (s->setting_sync_candidate_type == SYNC_BACKEND_WEBDAV) {
                cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                    cui_heading(ui, "WebDAV 网盘服务配置", 4);
                    cui_spacer(ui);
                    cui_badge(ui, is_active ? "已生效" : "未激活",
                              .tone = is_active ? CUI_TONE_SUCCESS : CUI_TONE_WARNING,
                              .size = CUI_SIZE_SM);
                }

                cui_text(ui, "支持坚果云、Nextcloud、ownCloud 等 WebDAV 网盘",
                         .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);

                cui_input(ui, "webdav_url_input", s->sync_cfg.webdav_url, sizeof(s->sync_cfg.webdav_url),
                          .label = "WebDAV 服务器 URL (含根路径):",
                          .placeholder = "https://dav.jianguoyun.com/dav/ai-space/",
                          .width = CUI_FILL);

                cui_row(ui, .width = CUI_FILL, .gap = 12) {
                    cui_input(ui, "webdav_user_input", s->sync_cfg.webdav_user, sizeof(s->sync_cfg.webdav_user),
                              .label = "用户名 / 账号:",
                              .placeholder = "username@example.com",
                              .width = CUI_GROW(1));

                    cui_input(ui, "webdav_pass_input", s->sync_cfg.webdav_pass, sizeof(s->sync_cfg.webdav_pass),
                              .label = "密码 / 应用专用授权码:",
                              .placeholder = "••••••••",
                              .password = true,
                              .width = CUI_GROW(1));
                }

                cui_input(ui, "webdav_path_input", s->sync_cfg.webdav_remote_path, sizeof(s->sync_cfg.webdav_remote_path),
                          .label = "网盘远端相对子目录:",
                          .placeholder = "/ai-space/skills",
                          .width = CUI_FILL);

            } else if (s->setting_sync_candidate_type == SYNC_BACKEND_NFS) {
                cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                    cui_heading(ui, "局域网共享挂载目录", 4);
                    cui_spacer(ui);
                    cui_badge(ui, is_active ? "已生效" : "未激活",
                              .tone = is_active ? CUI_TONE_SUCCESS : CUI_TONE_WARNING,
                              .size = CUI_SIZE_SM);
                }

                cui_text(ui, "支持挂载在各主机的 NAS、SMB 共享文件夹或网络文件系统",
                         .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);

                cui_input(ui, "nfs_path_input", s->sync_cfg.nfs_mount_path, sizeof(s->sync_cfg.nfs_mount_path),
                          .label = "共享目录绝对路径 / 挂载点:",
                          .placeholder = "~/.ai-space/remote-share 或 /Volumes/nas/ai-space/skills",
                          .icon = CUI_ICON_FOLDER,
                          .width = CUI_FILL);

                char nfs_exp[512] = "";
                char nfs_stat[256] = "";
                bool nfs_ex = false;
                get_dir_stat_info(s->sync_cfg.nfs_mount_path, nfs_exp, sizeof(nfs_exp),
                                  nfs_stat, sizeof(nfs_stat), &nfs_ex, NULL);

                cui_box(ui, .width = CUI_FILL, .radius = 6, .pad = 8, .gap = 4,
                        .bg = cui_alpha(cui_color_of(ui, CUI_COLOR_SURFACE), 0.6f),
                        .border = 1, .border_color = cui_color_of(ui, CUI_COLOR_BORDER)) {
                    cui_text(ui, nfs_stat, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);
                }

            } else if (s->setting_sync_candidate_type == SYNC_BACKEND_GIT) {
                cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                    cui_heading(ui, "Git 远端版本库配置", 4);
                    cui_spacer(ui);
                    cui_badge(ui, is_active ? "已生效" : "未激活",
                              .tone = is_active ? CUI_TONE_SUCCESS : CUI_TONE_WARNING,
                              .size = CUI_SIZE_SM);
                }

                cui_text(ui, "以 Git 仓库作为共享源，支持 GitHub、GitLab、Gitea 等托管平台",
                         .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);

                cui_input(ui, "git_url_input", s->sync_cfg.git_repo_url, sizeof(s->sync_cfg.git_repo_url),
                          .label = "Git 远端仓库地址 (HTTPS 或 SSH):",
                          .placeholder = "https://github.com/myteam/ai-skills.git",
                          .width = CUI_FILL);

                cui_row(ui, .width = CUI_FILL, .gap = 12) {
                    cui_input(ui, "git_branch_input", s->sync_cfg.git_branch, sizeof(s->sync_cfg.git_branch),
                              .label = "目标分支:",
                              .placeholder = "main",
                              .width = CUI_PX(180));

                    cui_input(ui, "git_token_input", s->sync_cfg.git_auth_token, sizeof(s->sync_cfg.git_auth_token),
                              .label = "访问令牌:",
                              .placeholder = "ghp_xxxxxxxxxxxx",
                              .password = true,
                              .width = CUI_GROW(1));
                }

                cui_row(ui, .width = CUI_FILL, .gap = 12) {
                    cui_input(ui, "git_user_input", s->sync_cfg.git_user_name, sizeof(s->sync_cfg.git_user_name),
                              .label = "提交者姓名:",
                              .placeholder = "AI-Space User",
                              .width = CUI_GROW(1));

                    cui_input(ui, "git_email_input", s->sync_cfg.git_user_email, sizeof(s->sync_cfg.git_user_email),
                              .label = "提交者邮箱:",
                              .placeholder = "user@example.com",
                              .width = CUI_GROW(1));
                }
            }

            /* Action Buttons for non-none backends: Availability Check is strictly required! */
            if (s->setting_sync_candidate_type != SYNC_BACKEND_NONE) {
                cui_row(ui, .gap = 12, .align = CUI_ALIGN_CENTER) {
                    bool is_checking = s->op_progress.active;

                    /* Primary action: Check availability in background with progress modal, only activate if check succeeds */
                    if (cui_button(ui, "检测激活",
                                   .variant = CUI_BUTTON_PRIMARY,
                                   .icon = is_checking ? CUI_ICON_LOADER_CIRCLE : CUI_ICON_CHECK_CHECK,
                                   .disabled = is_checking)) {
                        start_sync_check_available_thread(s, true);
                    }

                    /* Secondary action: Check only, do not alter active backend */
                    if (cui_button(ui, "仅做检测",
                                   .variant = CUI_BUTTON_SECONDARY,
                                   .icon = is_checking ? CUI_ICON_LOADER_CIRCLE : CUI_ICON_ACTIVITY,
                                   .disabled = is_checking)) {
                        start_sync_check_available_thread(s, false);
                    }
                }
            }

            /* Pre-activation Check Feedback Banner */
            if (s->sync_has_checked_candidate && s->sync_candidate_check_msg[0] != '\0') {
                cui_box(ui, .width = CUI_FILL, .radius = 6, .pad = 10, .gap = 4,
                        .bg = s->sync_candidate_checked_ok ? cui_alpha(cui_color_of(ui, CUI_COLOR_SUCCESS), 0.12f)
                                                           : cui_alpha(cui_color_of(ui, CUI_COLOR_DANGER), 0.12f),
                        .border = 1,
                        .border_color = s->sync_candidate_checked_ok ? cui_color_of(ui, CUI_COLOR_SUCCESS)
                                                                     : cui_color_of(ui, CUI_COLOR_DANGER)) {
                    cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                        cui_icon(ui, s->sync_candidate_checked_ok ? CUI_ICON_CHECK : CUI_ICON_TRIANGLE_ALERT,
                                 15, s->sync_candidate_checked_ok ? CUI_COLOR_SUCCESS : CUI_COLOR_DANGER);
                        cui_text(ui, s->sync_candidate_checked_ok ? "检测通过，符合激活条件：" : "检测未通过，无法激活：",
                                 .size = 12, .color = s->sync_candidate_checked_ok ? CUI_COLOR_SUCCESS : CUI_COLOR_DANGER);
                        cui_text(ui, s->sync_candidate_check_msg, .size = 12,
                                 .color = s->sync_candidate_checked_ok ? CUI_COLOR_SUCCESS : CUI_COLOR_DANGER);
                    }
                }
            }
        }
    }
}

static void render_tab_scan(cui_ctx *ui, app_state_t *s) {
    cui_column(ui, .width = CUI_FILL, .gap = 20) {
        cui_box(ui, .width = CUI_FILL, .radius = 8, .pad = 16, .gap = 14,
                .bg = cui_color_of(ui, CUI_COLOR_SURFACE_RAISED),
                .border = 1, .border_color = cui_color_of(ui, CUI_COLOR_BORDER)) {
            cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                cui_icon(ui, CUI_ICON_REFRESH_CW, 18, CUI_COLOR_ACCENT);
                cui_heading(ui, "扫描策略与安全防御机制", 4);
            }

            cui_text(ui, "设置本地工具发现机制与写入安全防护规则。所有文件同步均为纯物理拷贝，不使用软链接。",
                     .size = 13, .color = CUI_COLOR_TEXT_SECONDARY);

            cui_column(ui, .width = CUI_FILL, .gap = 12, .pad = 4) {
                cui_checkbox(ui, "启动时自动执行全盘本地工具扫描", &s->setting_auto_scan);
                cui_text(ui, "   开启后，应用启动时将自动识别所有 ~/.<tool>/skills 目录变动并刷新数据库。",
                         .size = 12, .color = CUI_COLOR_TEXT_MUTED);

                cui_checkbox(ui, "覆盖写入前强制创建物理时间戳快照", &s->setting_auto_backup);
                cui_text(ui, "   强烈建议开启。在物理覆盖任何已有文件前，先将其原样备份至安全快照库。",
                         .size = 12, .color = CUI_COLOR_TEXT_MUTED);
            }

            cui_box(ui, .width = CUI_FILL, .radius = 6, .pad = 10, .gap = 8,
                    .bg = cui_alpha(cui_color_of(ui, CUI_COLOR_SURFACE), 0.6f),
                    .border = 1, .border_color = cui_color_of(ui, CUI_COLOR_BORDER)) {
                cui_text(ui, "内置目录过滤与保护规则 (自动忽略):", .size = 12, .color = CUI_COLOR_TEXT_MUTED);
                cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                    cui_badge(ui, ".git (版本库)", .tone = CUI_TONE_NEUTRAL, .size = CUI_SIZE_SM);
                    cui_badge(ui, "node_modules (包依赖)", .tone = CUI_TONE_NEUTRAL, .size = CUI_SIZE_SM);
                    cui_badge(ui, "__pycache__ (缓存)", .tone = CUI_TONE_NEUTRAL, .size = CUI_SIZE_SM);
                    cui_badge(ui, ".DS_Store (系统文件)", .tone = CUI_TONE_NEUTRAL, .size = CUI_SIZE_SM);
                }
            }

            cui_row(ui, .gap = 10, .align = CUI_ALIGN_CENTER) {
                if (cui_button(ui, "保存策略", .variant = CUI_BUTTON_PRIMARY, .icon = CUI_ICON_SAVE)) {
                    app_save_settings(s);
                    app_set_toast(s, "扫描与同步安全策略已保存生效");
                }
            }
        }
    }
}

static void render_tab_preferences(cui_ctx *ui, app_state_t *s) {
    cui_column(ui, .width = CUI_FILL, .gap = 20) {
        cui_box(ui, .width = CUI_FILL, .radius = 8, .pad = 16, .gap = 14,
                .bg = cui_color_of(ui, CUI_COLOR_SURFACE_RAISED),
                .border = 1, .border_color = cui_color_of(ui, CUI_COLOR_BORDER)) {
            cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                cui_icon(ui, CUI_ICON_SLIDERS_HORIZONTAL, 18, CUI_COLOR_ACCENT);
                cui_heading(ui, "常规偏好与显示设置", 4);
            }

            cui_text(ui, "个性化配置应用的启动视图与文本差异对比呈现形式。",
                     .size = 13, .color = CUI_COLOR_TEXT_SECONDARY);

            /* Setting 1: Default Startup View */
            cui_column(ui, .width = CUI_FILL, .gap = 8) {
                cui_text(ui, "默认启动主视图:", .size = 13, .color = CUI_COLOR_FOREGROUND);
                cui_row(ui, .gap = 10, .align = CUI_ALIGN_CENTER) {
                    bool is_tools = (s->setting_default_view == 0);
                    if (cui_button(ui, "工具列表",
                                   .variant = is_tools ? CUI_BUTTON_PRIMARY : CUI_BUTTON_SECONDARY,
                                   .icon = CUI_ICON_FOLDER)) {
                        s->setting_default_view = 0;
                    }

                    bool is_skills = (s->setting_default_view == 1);
                    if (cui_button(ui, "技能列表",
                                   .variant = is_skills ? CUI_BUTTON_PRIMARY : CUI_BUTTON_SECONDARY,
                                   .icon = CUI_ICON_PACKAGE)) {
                        s->setting_default_view = 1;
                    }

                    bool is_hub = (s->setting_default_view == 2);
                    if (cui_button(ui, "主仓管理",
                                   .variant = is_hub ? CUI_BUTTON_PRIMARY : CUI_BUTTON_SECONDARY,
                                   .icon = CUI_ICON_ARCHIVE)) {
                        s->setting_default_view = 2;
                    }

                    bool is_sync = (s->setting_default_view == 3);
                    if (cui_button(ui, "同步管理",
                                   .variant = is_sync ? CUI_BUTTON_PRIMARY : CUI_BUTTON_SECONDARY,
                                   .icon = CUI_ICON_GLOBE)) {
                        s->setting_default_view = 3;
                    }
                }
            }

            /* Setting 2: Default Diff Mode */
            cui_column(ui, .width = CUI_FILL, .gap = 8) {
                cui_text(ui, "差异对比视图呈现模式:", .size = 13, .color = CUI_COLOR_FOREGROUND);
                cui_row(ui, .gap = 10, .align = CUI_ALIGN_CENTER) {
                    bool is_split = (s->diff_mode == CUI_DIFF_SPLIT);
                    if (cui_button(ui, "双栏分屏",
                                   .variant = is_split ? CUI_BUTTON_PRIMARY : CUI_BUTTON_SECONDARY,
                                   .icon = CUI_ICON_LAYERS)) {
                        s->diff_mode = CUI_DIFF_SPLIT;
                    }

                    bool is_inline = (s->diff_mode == CUI_DIFF_INLINE);
                    if (cui_button(ui, "单栏行内",
                                   .variant = is_inline ? CUI_BUTTON_PRIMARY : CUI_BUTTON_SECONDARY,
                                   .icon = CUI_ICON_FILE_TEXT)) {
                        s->diff_mode = CUI_DIFF_INLINE;
                    }
                }
            }

            cui_row(ui, .gap = 10, .align = CUI_ALIGN_CENTER) {
                if (cui_button(ui, "保存设置", .variant = CUI_BUTTON_PRIMARY, .icon = CUI_ICON_SAVE)) {
                    app_save_settings(s);
                    app_set_toast(s, "常规偏好设置已更新");
                }
            }
        }
    }
}

void render_settings_page(cui_ctx *ui, app_state_t *s) {
    if (!ui || !s) return;

    /* 1. Global Navigation Bar */
    render_global_navbar(ui, s);

    /* 2. Top Header */
    cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
        cui_column(ui, .gap = 2) {
            cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                cui_icon(ui, CUI_ICON_SETTINGS, 20, CUI_COLOR_ACCENT);
                cui_heading(ui, "系统设置", 2);
            }
            cui_text(ui, "配置中心主库基准路径、跨主机远端同步后端及系统常规偏好",
                     .size = 13, .color = CUI_COLOR_TEXT_SECONDARY);
        }
    }

    /* 3. Main Web-style Tabbed Settings Layout */
    cui_row(ui, .width = CUI_FILL, .height = CUI_FILL, .gap = 16) {
        /* Left Category Navigation Sidebar */
        cui_box(ui, .width = CUI_PX(210), .height = CUI_FILL, .radius = 8,
                .bg = cui_color_of(ui, CUI_COLOR_SURFACE_RAISED),
                .pad = 10, .gap = 6,
                .border = 1, .border_color = cui_color_of(ui, CUI_COLOR_BORDER)) {
            cui_text(ui, "配置分类", .size = 12, .color = CUI_COLOR_TEXT_MUTED);

            /* Tab 0: 主库存储 */
            bool tab0_active = (s->settings_active_tab == 0);
            if (cui_button(ui, "主库存储",
                           .variant = tab0_active ? CUI_BUTTON_PRIMARY : CUI_BUTTON_GHOST,
                           .width = CUI_FILL,
                           .size = CUI_SIZE_MD,
                           .icon = CUI_ICON_FOLDER)) {
                s->settings_active_tab = 0;
            }

            /* Tab 1: 远端同步 */
            bool tab1_active = (s->settings_active_tab == 1);
            if (cui_button(ui, "远端同步",
                           .variant = tab1_active ? CUI_BUTTON_PRIMARY : CUI_BUTTON_GHOST,
                           .width = CUI_FILL,
                           .size = CUI_SIZE_MD,
                           .icon = CUI_ICON_GLOBE)) {
                s->settings_active_tab = 1;
            }

            /* Tab 2: 扫描策略 */
            bool tab2_active = (s->settings_active_tab == 2);
            if (cui_button(ui, "扫描策略",
                           .variant = tab2_active ? CUI_BUTTON_PRIMARY : CUI_BUTTON_GHOST,
                           .width = CUI_FILL,
                           .size = CUI_SIZE_MD,
                           .icon = CUI_ICON_REFRESH_CW)) {
                s->settings_active_tab = 2;
            }

            /* Tab 3: 常规偏好 */
            bool tab3_active = (s->settings_active_tab == 3);
            if (cui_button(ui, "常规偏好",
                           .variant = tab3_active ? CUI_BUTTON_PRIMARY : CUI_BUTTON_GHOST,
                           .width = CUI_FILL,
                           .size = CUI_SIZE_MD,
                           .icon = CUI_ICON_SLIDERS_HORIZONTAL)) {
                s->settings_active_tab = 3;
            }

            cui_spacer(ui);

            cui_box(ui, .width = CUI_FILL, .radius = 6, .pad = 8, .gap = 4,
                    .bg = cui_alpha(cui_color_of(ui, CUI_COLOR_SURFACE), 0.5f),
                    .border = 1, .border_color = cui_color_of(ui, CUI_COLOR_BORDER)) {
                cui_text(ui, "当前激活后端", .size = 11, .color = CUI_COLOR_TEXT_MUTED);
                cui_text(ui, sync_backend_display_name(s->sync_cfg.type),
                         .size = 11, .color = CUI_COLOR_ACCENT);
            }
        }

        /* Right Content Panel */
        cui_box(ui, .width = CUI_FILL, .height = CUI_FILL, .radius = 8,
                .bg = cui_color_of(ui, CUI_COLOR_SURFACE),
                .pad = 20, .gap = 16,
                .border = 1, .border_color = cui_color_of(ui, CUI_COLOR_BORDER),
                .scroll_y = true) {
            if (s->settings_active_tab == 0) {
                render_tab_storage(ui, s);
            } else if (s->settings_active_tab == 1) {
                render_tab_remote_sync(ui, s);
            } else if (s->settings_active_tab == 2) {
                render_tab_scan(ui, s);
            } else if (s->settings_active_tab == 3) {
                render_tab_preferences(ui, s);
            }
        }
    }
}
