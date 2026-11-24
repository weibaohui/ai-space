#include "view_sync_settings.h"
#include "view_common.h"
#include "app_sync.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "portability.h"

static void render_local_form(cui_ctx *ui, app_state_t *s, bool is_active) {
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
                                     .icon = CUI_ICON_MONITOR,
                                     .disabled = s->op_progress.active || s->hub_scan.is_scanning)) {
            app_sync_use_local_mode(s);
        }
    }
}

static void render_webdav_form(cui_ctx *ui, app_state_t *s, bool is_active) {
    cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
        cui_heading(ui, "WebDAV 网盘服务配置", 4);
        cui_spacer(ui);
        cui_badge(ui, is_active ? "已生效" : "未激活",
                  .tone = is_active ? CUI_TONE_SUCCESS : CUI_TONE_WARNING,
                  .size = CUI_SIZE_SM);
    }

    cui_text(ui, "匿名服务可留空账号和密码。认证服务请填写账号和密码或应用授权码。",
             .width = CUI_FILL, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);

    cui_input(ui, "webdav_url_input", s->sync_pending_cfg.webdav_url, sizeof(s->sync_pending_cfg.webdav_url),
              .label = "WebDAV 服务地址（含服务路径）",
              .placeholder = "https://dav.jianguoyun.com/dav/ai-space/",
              .width = CUI_FILL);

    cui_row(ui, .width = CUI_FILL, .gap = 12) {
        cui_input(ui, "webdav_user_input", s->sync_pending_cfg.webdav_user, sizeof(s->sync_pending_cfg.webdav_user),
                  .label = "用户名 / 账号:",
                  .placeholder = "username@example.com",
                  .width = CUI_GROW(1));

        cui_input(ui, "webdav_pass_input", s->sync_pending_cfg.webdav_pass, sizeof(s->sync_pending_cfg.webdav_pass),
                  .label = "密码 / 应用专用授权码:",
                  .placeholder = "••••••••",
                  .password = true,
                  .width = CUI_GROW(1));
    }

    cui_input(ui, "webdav_path_input", s->sync_pending_cfg.webdav_remote_path, sizeof(s->sync_pending_cfg.webdav_remote_path),
              .label = "同步子目录（留空使用服务地址对应目录）",
              .placeholder = "/ai-space/skills",
              .width = CUI_FILL);
}

static void render_share_form(cui_ctx *ui, app_state_t *s, bool is_active) {
    sync_config_t *cfg = &s->sync_pending_cfg;
    is_active = is_active && cfg->share_mode == s->sync_cfg.share_mode;
    cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
        cui_heading(ui, "SMB / NFS 共享目录", 4);
        cui_spacer(ui);
        cui_badge(ui, is_active ? "已生效" : "未激活",
                  .tone = is_active ? CUI_TONE_SUCCESS : CUI_TONE_WARNING,
                  .size = CUI_SIZE_SM);
    }

    const char *modes[] = {"NFS 直连", "SMB 直连", "已有挂载目录"};
    int mode = cfg->share_mode == SYNC_SHARE_MOUNTED ? 2 : cfg->share_mode - 1;
    if (cui_segmented(ui, "sync_share_mode", modes, 3, &mode, .size = CUI_SIZE_MD)) {
        cfg->share_mode = mode == 2 ? SYNC_SHARE_MOUNTED : (sync_share_mode_t)(mode + 1);
        s->sync_has_checked_candidate = false;
    }

    if (cfg->share_mode == SYNC_SHARE_MOUNTED) {
        cui_input(ui, "nfs_path_input", cfg->nfs_mount_path, sizeof(cfg->nfs_mount_path),
                  .label = "本机挂载点或挂载点下的同步目录",
                  .placeholder = "/Volumes/nas/ai-space/skills",
                  .icon = CUI_ICON_FOLDER, .width = CUI_FILL);
    } else {
        bool nfs = cfg->share_mode == SYNC_SHARE_NFS;
        cui_row(ui, .width = CUI_FILL, .gap = 12) {
            cui_input(ui, "share_server", cfg->share_server, sizeof(cfg->share_server),
                      .label = "服务器 IP 或主机名", .placeholder = "192.168.0.211",
                      .width = CUI_GROW(1));
            cui_input(ui, "share_export", cfg->share_export, sizeof(cfg->share_export),
                      .label = nfs ? "NFS 导出路径" : "SMB 共享名",
                      .placeholder = nfs ? "/srv/share" : "share", .width = CUI_GROW(1));
        }
        cui_input(ui, "share_subdir", cfg->share_subdir, sizeof(cfg->share_subdir),
                  .label = "同步子目录（选填，填写共享中已有的目录）",
                  .placeholder = "ai-space-skills", .width = CUI_FILL);
        if (nfs) {
            cui_text(ui, "使用 NFSv3，访问权限由服务端控制。", .width = CUI_FILL,
                     .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);
        } else {
            cui_row(ui, .width = CUI_FILL, .gap = 12) {
                cui_input(ui, "share_smb_user", cfg->smb_user, sizeof(cfg->smb_user),
                          .label = "用户名", .placeholder = "留空使用访客",
                          .width = CUI_GROW(1));
                cui_input(ui, "share_smb_pass", cfg->smb_pass, sizeof(cfg->smb_pass),
                          .label = "密码", .password = true, .width = CUI_GROW(1));
            }
        }
    }
}

static void render_git_form(cui_ctx *ui, app_state_t *s, bool is_active) {
    cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
        cui_heading(ui, "Git 远端版本库配置", 4);
        cui_spacer(ui);
        cui_badge(ui, is_active ? "已生效" : "未激活",
                  .tone = is_active ? CUI_TONE_SUCCESS : CUI_TONE_WARNING,
                  .size = CUI_SIZE_SM);
    }

    cui_text(ui, "以 Git 仓库作为共享源，支持 GitHub、GitLab、Gitea 等托管平台",
             .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);

    sync_config_t *cfg = &s->sync_pending_cfg;
    cui_input(ui, "git_url_input", cfg->git_repo_url, sizeof(cfg->git_repo_url),
              .label = "Git 远端仓库地址",
              .placeholder = "https://github.com/team/skills.git 或 git@github.com:team/skills.git",
              .width = CUI_FILL);

    cui_row(ui, .width = CUI_FILL, .gap = 12) {
        cui_input(ui, "git_branch_input", cfg->git_branch, sizeof(cfg->git_branch),
                  .label = "目标分支", .placeholder = "main", .width = CUI_PX(180));

        if (sync_git_uses_token(cfg->git_repo_url)) {
            cui_input(ui, "git_token_input", cfg->git_auth_token, sizeof(cfg->git_auth_token),
                      .label = "HTTPS 访问令牌", .password = true, .width = CUI_GROW(1));
        }
    }

    if (sync_git_uses_ssh(cfg->git_repo_url)) {
        cui_text(ui, "SSH 使用本机配置的密钥，可在仓库地址中指定登录用户名。",
                 .width = CUI_FILL, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);
    }

    static bool git_author_open = false;
    cui_accordion(ui, "提交者信息（选填）", &git_author_open) {
        cui_text(ui, "填写后用于 AI-Space 推送时生成的 Git 提交记录。",
                 .width = CUI_FILL, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);
        cui_row(ui, .width = CUI_FILL, .gap = 12) {
            cui_input(ui, "git_user_input", cfg->git_user_name, sizeof(cfg->git_user_name),
                      .label = "提交者姓名", .placeholder = "AI-Space User", .width = CUI_GROW(1));
            cui_input(ui, "git_email_input", cfg->git_user_email, sizeof(cfg->git_user_email),
                      .label = "提交者邮箱", .placeholder = "user@example.com", .width = CUI_GROW(1));
        }
    }
}

static void render_probe_controls(cui_ctx *ui, app_state_t *s) {
    if (s->setting_sync_candidate_type != SYNC_BACKEND_NONE) {
        cui_row(ui, .width = CUI_FILL, .gap = 8, .justify = CUI_JUSTIFY_END) {
            bool busy = s->op_progress.active || s->hub_scan.is_scanning;
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

void render_tab_remote_sync(cui_ctx *ui, app_state_t *s) {
    static const char *backends[] = { "单机模式", "WebDAV", "SMB / NFS", "Git 仓库" };
    static const char *backend_icons[] = { CUI_ICON_MONITOR, CUI_ICON_CLOUD, CUI_ICON_SERVER, CUI_ICON_GIT_BRANCH };
    sync_backend_type_t active = s->sync_cfg.type;
    const char *active_name = sync_backend_display_name(active);
    if (active == SYNC_BACKEND_NFS) {
        if (s->sync_cfg.share_mode == SYNC_SHARE_NFS) active_name = "NFS 直连";
        if (s->sync_cfg.share_mode == SYNC_SHARE_SMB) active_name = "SMB 直连";
    }
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
                    cui_heading(ui, active_name, 3);
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

            bool is_active = active == s->setting_sync_candidate_type;
            switch (s->setting_sync_candidate_type) {
            case SYNC_BACKEND_NONE: render_local_form(ui, s, is_active); break;
            case SYNC_BACKEND_WEBDAV: render_webdav_form(ui, s, is_active); break;
            case SYNC_BACKEND_NFS: render_share_form(ui, s, is_active); break;
            case SYNC_BACKEND_GIT: render_git_form(ui, s, is_active); break;
            }
            render_probe_controls(ui, s);
        }
    }
}
