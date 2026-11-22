#include "view_settings.h"
#include "view_common.h"
#include "db.h"
#include "hub_manager.h"
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
            snprintf(out_info, info_cap, "🟢 物理目录已就绪 (可读写，含 %d 个子目录项)", count);
        } else {
            snprintf(out_info, info_cap, "🟡 物理目录已存在 (读写权限受限)");
        }
    } else {
        if (out_exists) *out_exists = false;
        snprintf(out_info, info_cap, "⚪ 目录尚未在磁盘创建 (保存后系统将自动初始化)");
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
                    cui_heading(ui, "核心主仓存储路径 (Master Hub Repository)", 4);
                }
                cui_spacer(ui);
                cui_badge(ui, hub_exists ? "主库已就绪" : "待初始化",
                          .tone = hub_exists ? CUI_TONE_SUCCESS : CUI_TONE_NEUTRAL,
                          .size = CUI_SIZE_SM);
            }

            cui_text(ui, "收录主库与跨工具多端同步的核心基准目录。所有已收录的 Skill 将在此以标准化目录树物理存储。",
                     .size = 13, .color = CUI_COLOR_TEXT_SECONDARY);

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
                if (cui_button(ui, " 💾 保存主仓路径", .variant = CUI_BUTTON_PRIMARY, .icon = CUI_ICON_SAVE)) {
                    app_save_settings(s);
                    s->hub_matrix_loaded = false;
                    char toast[256];
                    snprintf(toast, sizeof(toast), "主仓路径已更新为: %s", s->setting_hub_dir);
                    app_set_toast(s, toast);
                }

                if (cui_button(ui, " ↺ 恢复默认 (~/.ai-space/skills)", .variant = CUI_BUTTON_SECONDARY, .icon = CUI_ICON_UNDO_2)) {
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
                    cui_heading(ui, "覆盖快照安全备份路径 (Snapshot Backups)", 4);
                }
                cui_spacer(ui);
                cui_badge(ui, backup_exists ? "备份库正常" : "待初始化",
                          .tone = backup_exists ? CUI_TONE_SUCCESS : CUI_TONE_NEUTRAL,
                          .size = CUI_SIZE_SM);
            }

            cui_text(ui, "在执行单工具物理覆盖、版本回滚或全量广播前，系统将自动创建时间戳快照归档至此路径。",
                     .size = 13, .color = CUI_COLOR_TEXT_SECONDARY);

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
                if (cui_button(ui, " 💾 保存备份路径", .variant = CUI_BUTTON_PRIMARY, .icon = CUI_ICON_SAVE)) {
                    app_save_settings(s);
                    char toast[256];
                    snprintf(toast, sizeof(toast), "备份路径已更新为: %s", s->setting_backup_dir);
                    app_set_toast(s, toast);
                }

                if (cui_button(ui, " ↺ 恢复默认 (~/.ai-space/backups)", .variant = CUI_BUTTON_SECONDARY, .icon = CUI_ICON_UNDO_2)) {
                    strncpy(s->setting_backup_dir, "~/.ai-space/backups", sizeof(s->setting_backup_dir) - 1);
                    s->setting_backup_dir[sizeof(s->setting_backup_dir) - 1] = '\0';
                    app_save_settings(s);
                    app_set_toast(s, "已恢复备份默认路径: ~/.ai-space/backups");
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
                if (cui_button(ui, " 💾 保存同步策略", .variant = CUI_BUTTON_PRIMARY, .icon = CUI_ICON_SAVE)) {
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
                    if (cui_button(ui, "  工具列表 (Tool List)",
                                   .variant = is_tools ? CUI_BUTTON_PRIMARY : CUI_BUTTON_SECONDARY,
                                   .icon = CUI_ICON_FOLDER)) {
                        s->setting_default_view = 0;
                    }

                    bool is_hub = (s->setting_default_view == 1);
                    if (cui_button(ui, "  主仓管理 (Master Hub)",
                                   .variant = is_hub ? CUI_BUTTON_PRIMARY : CUI_BUTTON_SECONDARY,
                                   .icon = CUI_ICON_ARCHIVE)) {
                        s->setting_default_view = 1;
                    }
                }
            }

            /* Setting 2: Default Diff Mode */
            cui_column(ui, .width = CUI_FILL, .gap = 8) {
                cui_text(ui, "差异对比视图呈现模式:", .size = 13, .color = CUI_COLOR_FOREGROUND);
                cui_row(ui, .gap = 10, .align = CUI_ALIGN_CENTER) {
                    bool is_split = (s->diff_mode == CUI_DIFF_SPLIT);
                    if (cui_button(ui, "  双栏分屏 (Split)",
                                   .variant = is_split ? CUI_BUTTON_PRIMARY : CUI_BUTTON_SECONDARY,
                                   .icon = CUI_ICON_LAYERS)) {
                        s->diff_mode = CUI_DIFF_SPLIT;
                    }

                    bool is_inline = (s->diff_mode == CUI_DIFF_INLINE);
                    if (cui_button(ui, "  单栏行内 (Inline)",
                                   .variant = is_inline ? CUI_BUTTON_PRIMARY : CUI_BUTTON_SECONDARY,
                                   .icon = CUI_ICON_FILE_TEXT)) {
                        s->diff_mode = CUI_DIFF_INLINE;
                    }
                }
            }

            cui_row(ui, .gap = 10, .align = CUI_ALIGN_CENTER) {
                if (cui_button(ui, " 💾 保存偏好设置", .variant = CUI_BUTTON_PRIMARY, .icon = CUI_ICON_SAVE)) {
                    app_save_settings(s);
                    app_set_toast(s, "常规偏好设置已更新");
                }
            }
        }
    }
}

static void render_tab_about(cui_ctx *ui, app_state_t *s) {
    cui_column(ui, .width = CUI_FILL, .gap = 20) {
        /* Architecture specifications */
        cui_box(ui, .width = CUI_FILL, .radius = 8, .pad = 16, .gap = 12,
                .bg = cui_color_of(ui, CUI_COLOR_SURFACE_RAISED),
                .border = 1, .border_color = cui_color_of(ui, CUI_COLOR_BORDER)) {
            cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                cui_icon(ui, CUI_ICON_INFO, 18, CUI_COLOR_ACCENT);
                cui_heading(ui, "AI-Space 原生架构与规范", 4);
                cui_spacer(ui);
                cui_badge(ui, "v1.0.0 Release", .tone = CUI_TONE_ACCENT, .size = CUI_SIZE_SM);
            }

            cui_column(ui, .width = CUI_FILL, .gap = 8, .pad = 4) {
                cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                    cui_icon(ui, CUI_ICON_CHECK, 14, CUI_COLOR_ACCENT);
                    cui_text(ui, "拒绝软链接原则 (No Symlinks): 采用原子级物理复制，彻底避免跨运行时与容器挂载环境下的软链接断裂风险。",
                             .size = 13, .color = CUI_COLOR_TEXT_SECONDARY);
                }
                cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                    cui_icon(ui, CUI_ICON_CHECK, 14, CUI_COLOR_ACCENT);
                    cui_text(ui, "强事务与快照回退: 基于 SQLite3 本地持久化与时间戳快照机制，所有覆盖操作均有据可查、随时可撤销。",
                             .size = 13, .color = CUI_COLOR_TEXT_SECONDARY);
                }
                cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                    cui_icon(ui, CUI_ICON_CHECK, 14, CUI_COLOR_ACCENT);
                    cui_text(ui, "极速原生性能: 纯 C99 原生开发与 CUI 跨平台响应式渲染引擎，无 Node/Python 庞大依赖，毫秒级即时响应。",
                             .size = 13, .color = CUI_COLOR_TEXT_SECONDARY);
                }
            }
        }

        /* Database & Storage Status */
        cui_box(ui, .width = CUI_FILL, .radius = 8, .pad = 16, .gap = 12,
                .bg = cui_color_of(ui, CUI_COLOR_SURFACE_RAISED),
                .border = 1, .border_color = cui_color_of(ui, CUI_COLOR_BORDER)) {
            cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                cui_icon(ui, CUI_ICON_DATABASE, 18, CUI_COLOR_ACCENT);
                cui_heading(ui, "系统诊断与缓存维护", 4);
            }

            cui_row(ui, .width = CUI_FILL, .gap = 16) {
                char b1[32], b2[32], b3[32];
                snprintf(b1, sizeof(b1), "%d 个", s->stats.total_tools);
                snprintf(b2, sizeof(b2), "%d 个", s->stats.total_skills);
                snprintf(b3, sizeof(b3), "%d 个", s->hub_matrix.hub_managed_skills);

                cui_metric(ui, "已注册本地工具", b1, .icon = CUI_ICON_BOX);
                cui_metric(ui, "本地工具技能总数", b2, .icon = CUI_ICON_LAYERS);
                cui_metric(ui, "主仓已收录技能", b3, .icon = CUI_ICON_ARCHIVE);
            }

            cui_row(ui, .gap = 10, .align = CUI_ALIGN_CENTER) {
                if (cui_button(ui, " 🔄 重新全量扫描并刷新主仓缓存", .variant = CUI_BUTTON_OUTLINE, .icon = CUI_ICON_REFRESH_CW)) {
                    s->hub_matrix_loaded = false;
                    start_hub_scan_thread(s);
                    app_set_toast(s, "正在全量重新比对主仓与本地工具...");
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
            cui_text(ui, "配置中心主库基准路径、安全备份快照策略与系统常规偏好",
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

            /* Tab 0: 仓储与主库 */
            bool tab0_active = (s->settings_active_tab == 0);
            if (cui_button(ui, "  仓储与主库",
                           .variant = tab0_active ? CUI_BUTTON_PRIMARY : CUI_BUTTON_GHOST,
                           .width = CUI_FILL,
                           .size = CUI_SIZE_MD,
                           .icon = CUI_ICON_FOLDER)) {
                s->settings_active_tab = 0;
            }

            /* Tab 1: 扫描与同步 */
            bool tab1_active = (s->settings_active_tab == 1);
            if (cui_button(ui, "  扫描与同步",
                           .variant = tab1_active ? CUI_BUTTON_PRIMARY : CUI_BUTTON_GHOST,
                           .width = CUI_FILL,
                           .size = CUI_SIZE_MD,
                           .icon = CUI_ICON_REFRESH_CW)) {
                s->settings_active_tab = 1;
            }

            /* Tab 2: 常规偏好 */
            bool tab2_active = (s->settings_active_tab == 2);
            if (cui_button(ui, "  常规偏好",
                           .variant = tab2_active ? CUI_BUTTON_PRIMARY : CUI_BUTTON_GHOST,
                           .width = CUI_FILL,
                           .size = CUI_SIZE_MD,
                           .icon = CUI_ICON_SLIDERS_HORIZONTAL)) {
                s->settings_active_tab = 2;
            }

            /* Tab 3: 关于与维护 */
            bool tab3_active = (s->settings_active_tab == 3);
            if (cui_button(ui, "  关于与维护",
                           .variant = tab3_active ? CUI_BUTTON_PRIMARY : CUI_BUTTON_GHOST,
                           .width = CUI_FILL,
                           .size = CUI_SIZE_MD,
                           .icon = CUI_ICON_INFO)) {
                s->settings_active_tab = 3;
            }

            cui_spacer(ui);

            cui_box(ui, .width = CUI_FILL, .radius = 6, .pad = 8, .gap = 4,
                    .bg = cui_alpha(cui_color_of(ui, CUI_COLOR_SURFACE), 0.5f),
                    .border = 1, .border_color = cui_color_of(ui, CUI_COLOR_BORDER)) {
                cui_text(ui, "存储格式", .size = 11, .color = CUI_COLOR_TEXT_MUTED);
                cui_text(ui, "SQLite3 Key-Value", .size = 11, .color = CUI_COLOR_TEXT_SECONDARY);
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
                render_tab_scan(ui, s);
            } else if (s->settings_active_tab == 2) {
                render_tab_preferences(ui, s);
            } else if (s->settings_active_tab == 3) {
                render_tab_about(ui, s);
            }
        }
    }
}
