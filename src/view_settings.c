#include "view_settings.h"
#include "view_common.h"
#include "view_dependencies.h"
#include "view_storage.h"
#include "db.h"
#include "hub_manager.h"
#include "sync_backend.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include "portability.h"

typedef struct {
    cui_color color;
    bool selected;
} swatch_data_t;

static void paint_swatch(cui_ctx *ui, cui_rect r, void *user) {
    swatch_data_t *d = (swatch_data_t *)user;
    cui_vec2 c = { r.x + r.w / 2.0f, r.y + r.h / 2.0f };
    if (d->selected) cui_draw_circle(ui, c, r.w / 2.0f, cui_color_of(ui, CUI_COLOR_FOREGROUND));
    cui_draw_circle(ui, c, r.w / 2.0f - (d->selected ? 2.0f : 0.0f), cui_color_of(ui, CUI_COLOR_SURFACE_MUTED));
    cui_draw_circle(ui, c, r.w / 2.0f - 4.0f, d->color);
}

static void expand_path_helper(const char *in, char *out, size_t cap) {
    if (!in || in[0] == '\0') {
        if (out && cap > 0) out[0] = '\0';
        return;
    }
    if (in[0] == '~' && (in[1] == '/' || in[1] == '\0')) {
        const char *home = ai_home_dir();
        if (home && home[0]) {
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
            cui_push_id(ui, "hub_path_actions");
            cui_row(ui, .width = CUI_FILL, .gap = 8, .justify = CUI_JUSTIFY_END) {
                if (cui_button(ui, "恢复默认", .variant = CUI_BUTTON_GHOST, .icon = CUI_ICON_ROTATE_CCW)) {
                    snprintf(s->setting_hub_dir, sizeof(s->setting_hub_dir), "~/.ai-space/skills");
                    app_save_settings(s);
                    s->hub_matrix_loaded = false;
                    s->storage_stats.loaded = false;
                    cui_toast(ui, "已恢复默认主仓路径", .tone = CUI_TONE_SUCCESS);
                }
                if (cui_button(ui, "保存路径", .variant = CUI_BUTTON_PRIMARY, .icon = CUI_ICON_SAVE)) {
                    app_save_settings(s);
                    s->hub_matrix_loaded = false;
                    s->storage_stats.loaded = false;
                    cui_toast(ui, "已保存主仓路径", .tone = CUI_TONE_SUCCESS);
                }
            }
            cui_pop_id(ui);
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
            cui_push_id(ui, "backup_path_actions");
            cui_row(ui, .width = CUI_FILL, .gap = 8, .justify = CUI_JUSTIFY_END) {
                if (cui_button(ui, "恢复默认", .variant = CUI_BUTTON_GHOST, .icon = CUI_ICON_ROTATE_CCW)) {
                    snprintf(s->setting_backup_dir, sizeof(s->setting_backup_dir), "~/.ai-space/backups");
                    app_save_settings(s);
                    s->storage_stats.loaded = false;
                    cui_toast(ui, "已恢复默认备份路径", .tone = CUI_TONE_SUCCESS);
                }
                if (cui_button(ui, "保存路径", .variant = CUI_BUTTON_PRIMARY, .icon = CUI_ICON_SAVE)) {
                    app_save_settings(s);
                    s->storage_stats.loaded = false;
                    cui_toast(ui, "已保存备份路径", .tone = CUI_TONE_SUCCESS);
                }
            }
            cui_pop_id(ui);
        }
    }
}

static void render_tab_remote_sync(cui_ctx *ui, app_state_t *s) {
    static const char *backends[] = { "单机模式", "WebDAV", "局域共享", "Git 仓库" };
    static const char *backend_icons[] = { CUI_ICON_MONITOR, CUI_ICON_CLOUD, CUI_ICON_SERVER, CUI_ICON_GIT_BRANCH };
    sync_backend_type_t active = s->sync_cfg.type;
    const char *active_icon = (active >= SYNC_BACKEND_NONE && active <= SYNC_BACKEND_GIT)
                              ? backend_icons[active] : ICON_SYNC;
    cui_color accent = cui_color_of(ui, CUI_COLOR_ACCENT);
    cui_color surface = cui_color_of(ui, CUI_COLOR_SURFACE);
    bool dark = cui_get_theme_mode(ui) == CUI_THEME_DARK;

    cui_column(ui, .width = CUI_FILL, .gap = 16) {
        cui_box(ui, .width = CUI_FILL, .radius = 12, .pad = 16,
                .bg = cui_mix(surface, accent, dark ? 0.19f : 0.09f),
                .border = 1, .border_color = cui_mix(cui_color_of(ui, CUI_COLOR_BORDER), accent, 0.55f)) {
            cui_row(ui, .width = CUI_FILL, .gap = 14, .align = CUI_ALIGN_CENTER) {
                cui_box(ui, .width = CUI_PX(46), .height = CUI_PX(46), .radius = 12,
                        .bg = accent, .dir = CUI_STACK, .align = CUI_ALIGN_CENTER,
                        .justify = CUI_JUSTIFY_CENTER) {
                    cui_icon(ui, active_icon, 23, CUI_COLOR_ACCENT_FOREGROUND);
                }
                cui_column(ui, .width = CUI_FILL, .gap = 2) {
                    cui_text(ui, "当前生效的后端", .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);
                    cui_heading(ui, sync_backend_display_name(active), 3);
                }
            }
        }

        cui_box(ui, .width = CUI_FILL, .radius = 8, .pad = 16, .gap = 12,
                .bg = cui_color_of(ui, CUI_COLOR_SURFACE_RAISED),
                .border = 1, .border_color = cui_color_of(ui, CUI_COLOR_BORDER)) {
            cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                cui_icon(ui, ICON_SYNC, 18, CUI_COLOR_ACCENT);
                cui_heading(ui, "选择要配置的后端", 4);
            }

            int candidate = (int)s->setting_sync_candidate_type;
            if (cui_segmented(ui, "sync_backend_choice", backends, 4, &candidate,
                              .size = CUI_SIZE_MD, .icons = backend_icons)) {
                s->setting_sync_candidate_type = (sync_backend_type_t)candidate;
                s->sync_has_checked_candidate = false;
            }
        }

        /* Backend Specific Configuration Card */
        cui_box(ui, .width = CUI_FILL, .radius = 8, .pad = 16, .gap = 14,
                .bg = cui_color_of(ui, CUI_COLOR_SURFACE_RAISED),
                .border = 1, .border_color = cui_color_of(ui, CUI_COLOR_BORDER)) {

            bool is_active = (active == s->setting_sync_candidate_type);

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
                    if (!is_active && cui_button(ui, "切换为单机模式", .variant = CUI_BUTTON_PRIMARY,
                                                 .icon = CUI_ICON_MONITOR)) {
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

            if (s->setting_sync_candidate_type != SYNC_BACKEND_NONE) {
                cui_row(ui, .width = CUI_FILL, .gap = 8, .justify = CUI_JUSTIFY_END) {
                    bool busy = s->op_progress.active;
                    if (cui_button(ui, "测试连接", .variant = CUI_BUTTON_SECONDARY,
                                   .icon = CUI_ICON_LINK, .disabled = busy)) {
                        start_sync_check_available_thread(s, false);
                    }
                    if (cui_button(ui, "测试并启用", .variant = CUI_BUTTON_PRIMARY,
                                   .icon = CUI_ICON_CHECK, .disabled = busy)) {
                        start_sync_check_available_thread(s, true);
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

            cui_row(ui, .width = CUI_FILL, .justify = CUI_JUSTIFY_END) {
                if (cui_button(ui, "保存策略", .variant = CUI_BUTTON_PRIMARY, .icon = CUI_ICON_SAVE)) {
                    app_save_settings(s);
                    cui_toast(ui, "扫描策略已保存", .tone = CUI_TONE_SUCCESS);
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

            cui_column(ui, .width = CUI_FILL, .gap = 8) {
                cui_label(ui, "启动页面");
                static const char *pages[] = { "首页", "技能列表", "主仓管理", "同步管理" };
                static const char *icons[] = { CUI_ICON_LAYOUT_DASHBOARD, ICON_SKILLS, ICON_MASTER_HUB, ICON_SYNC };
                cui_segmented(ui, "pref_start_page", pages, 4, &s->setting_default_view,
                              .icons = icons, .size = CUI_SIZE_MD);
            }

            cui_column(ui, .width = CUI_FILL, .gap = 8) {
                cui_label(ui, "差异显示");
                static const char *modes[] = { "左右分屏", "单列内联" };
                int mode = (int)s->diff_mode;
                if (cui_segmented(ui, "pref_diff_mode", modes, 2, &mode, .size = CUI_SIZE_MD)) {
                    s->diff_mode = (cui_diff_mode)mode;
                }
            }

            /* Setting 3: Theme Mode */
            cui_column(ui, .width = CUI_FILL, .gap = 8) {
                cui_label(ui, "界面主题");
                const char *modes[2] = { "浅色", "深色" };
                const char *mode_icons[2] = { CUI_ICON_SUN, CUI_ICON_MOON };
                int mode = s->dark_theme ? 1 : 0;
                if (cui_segmented(ui, "pref_theme_mode", modes, 2, &mode, .icons = mode_icons, .size = CUI_SIZE_MD)) {
                    s->dark_theme = (mode == 1);
                    cui_set_theme(ui, s->dark_theme ? CUI_THEME_DARK : CUI_THEME_LIGHT);
                    app_save_settings(s);
                }
            }

            /* Setting 4: Accent Color */
            cui_column(ui, .width = CUI_FILL, .gap = 8) {
                cui_label(ui, "强调色");
                static const char *accent_names_zh[CUI_ACCENT_COUNT] = {
                    "中性灰", "经典紫", "科技蓝", "翡翠绿", "琥珀黄", "活力橙", "珊瑚红", "玫瑰粉"
                };
                cui_row(ui, .gap = 14, .align = CUI_ALIGN_CENTER) {
                    for (int a = 0; a < CUI_ACCENT_COUNT; a++) {
                        static const unsigned hex[CUI_ACCENT_COUNT] = {
                            0x555555, 0x7747ff, 0x0562ef, 0x00864b,
                            0xf3ad20, 0xc25300, 0xd1403a, 0xd23286
                        };
                        swatch_data_t d = { CUI_RGB(hex[a]), s->accent_color == a };
                        char id[32];
                        snprintf(id, sizeof id, "accent-set-%d", a);
                        cui_canvas_result r = cui_canvas(ui, id, CUI_PX(28), CUI_PX(28), paint_swatch, &d, sizeof d);
                        cui_tooltip(ui, accent_names_zh[a]);
                        if (r.clicked) {
                            s->accent_color = a;
                            cui_set_accent(ui, (cui_accent)a);
                            app_save_settings(s);
                        }
                    }
                    cui_gap(ui, 8);
                    cui_badge(ui, accent_names_zh[s->accent_color], .tone = CUI_TONE_ACCENT, .size = CUI_SIZE_SM);
                }
            }

            cui_row(ui, .width = CUI_FILL, .justify = CUI_JUSTIFY_END) {
                if (cui_button(ui, "保存设置", .variant = CUI_BUTTON_PRIMARY, .icon = CUI_ICON_SAVE)) {
                    app_save_settings(s);
                    cui_toast(ui, "常规偏好已保存", .tone = CUI_TONE_SUCCESS);
                }
            }
        }
    }
}

void render_settings_page(cui_ctx *ui, app_state_t *s) {
    static const char *const titles[SETTINGS_TAB_COUNT] = {
        [SETTINGS_TAB_STORAGE_PATHS] = "主库存储",
        [SETTINGS_TAB_REMOTE_SYNC] = "远端同步",
        [SETTINGS_TAB_SCAN] = "扫描策略",
        [SETTINGS_TAB_PREFERENCES] = "常规偏好",
        [SETTINGS_TAB_DEPENDENCIES] = "依赖检测",
        [SETTINGS_TAB_LOCAL_STORAGE] = "本地存储",
        [SETTINGS_TAB_ABOUT] = "关于系统",
    };
    static const char *const icons[SETTINGS_TAB_COUNT] = {
        [SETTINGS_TAB_STORAGE_PATHS] = ICON_MASTER_HUB,
        [SETTINGS_TAB_REMOTE_SYNC] = ICON_SYNC,
        [SETTINGS_TAB_SCAN] = CUI_ICON_SLIDERS_HORIZONTAL,
        [SETTINGS_TAB_PREFERENCES] = CUI_ICON_PALETTE,
        [SETTINGS_TAB_DEPENDENCIES] = CUI_ICON_SHIELD,
        [SETTINGS_TAB_LOCAL_STORAGE] = CUI_ICON_DATABASE,
        [SETTINGS_TAB_ABOUT] = CUI_ICON_INFO,
    };
    static void (*const render_tab[SETTINGS_TAB_COUNT])(cui_ctx *, app_state_t *) = {
        [SETTINGS_TAB_STORAGE_PATHS] = render_tab_storage,
        [SETTINGS_TAB_REMOTE_SYNC] = render_tab_remote_sync,
        [SETTINGS_TAB_SCAN] = render_tab_scan,
        [SETTINGS_TAB_PREFERENCES] = render_tab_preferences,
        [SETTINGS_TAB_DEPENDENCIES] = render_dependencies_page,
        [SETTINGS_TAB_LOCAL_STORAGE] = render_storage_page,
        [SETTINGS_TAB_ABOUT] = render_about_page,
    };

    if (s->settings_active_tab < 0 || s->settings_active_tab >= SETTINGS_TAB_COUNT) {
        s->settings_active_tab = SETTINGS_TAB_STORAGE_PATHS;
    }

    cui_column(ui, .width = CUI_FILL, .height = CUI_FILL, .pad = 20, .gap = 14) {
        render_workspace_heading(ui, CUI_ICON_SETTINGS, "系统设置", NULL);
        cui_box(ui, .width = CUI_FILL, .pad = 4, .radius = 13,
                .bg = CUI_COLOR_SURFACE, .border = 1, .border_color = CUI_COLOR_BORDER) {
            cui_tabs(ui, "settings_tabs", titles, SETTINGS_TAB_COUNT, &s->settings_active_tab,
                     .variant = CUI_TABS_PILL, .full_width = true, .icons = icons);
        }

        int tab = s->settings_active_tab;
        cui_push_id_int(ui, tab);
        cui_scroll(ui, .id = "settings_content", .width = CUI_FILL, .height = CUI_FILL) {
            render_tab[tab](ui, s);
        }
        cui_pop_id(ui);
    }
}
