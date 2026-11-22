#include "view_dashboard.h"
#include "view_common.h"
#include "view_tool_skills.h"
#include "hub_manager.h"
#include "skill_scanner.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include "portability.h"

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

static int count_snapshots(const char *backup_dir_setting) {
    if (!backup_dir_setting || !backup_dir_setting[0]) return 0;
    char exp[512] = "";
    expand_path_local(backup_dir_setting, exp, sizeof(exp));
    DIR *d = opendir(exp);
    if (!d) return 0;
    int count = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        char sub[1024];
        snprintf(sub, sizeof(sub), "%s/%s", exp, ent->d_name);
        struct stat st;
        if (stat(sub, &st) == 0 && S_ISDIR(st.st_mode)) {
            count++;
        }
    }
    closedir(d);
    return count;
}

/* Top Global Header Bar (Window Title, App Info, Node status, Clock) */
void render_app_top_bar(cui_ctx *ui, app_state_t *s) {
    (void)s;
    cui_row(ui, .width = CUI_FILL, .height = CUI_PX(42), .pad_x = 16,
            .align = CUI_ALIGN_CENTER, .bg = CUI_COLOR_SURFACE) {

        /* Left: Logo & Subtitle */
        cui_row(ui, .gap = 10, .align = CUI_ALIGN_CENTER) {
            cui_box(ui, .width = CUI_PX(26), .height = CUI_PX(26), .radius = 6,
                    .bg = cui_color_of(ui, CUI_COLOR_ACCENT),
                    .dir = CUI_STACK, .align = CUI_ALIGN_CENTER, .justify = CUI_JUSTIFY_CENTER) {
                cui_icon(ui, CUI_ICON_BOX, 16, CUI_COLOR_BACKGROUND);
            }

            cui_text(ui, "AI-Space", .font = CUI_FONT_DISPLAY, .size = 16);
            cui_text(ui, "|", .size = 13, .color = CUI_COLOR_BORDER);
            cui_text(ui, "本地多工具 AI 技能的中央枢纽与基准调度中心",
                     .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);
        }

        cui_spacer(ui);

        /* Right: Running Status, Version, Hostname, Clock */
        cui_row(ui, .gap = 12, .align = CUI_ALIGN_CENTER) {
            /* 1. Status */
            cui_row(ui, .gap = 5, .align = CUI_ALIGN_CENTER) {
                cui_icon(ui, CUI_ICON_CIRCLE, 8, CUI_COLOR_SUCCESS);
                cui_text(ui, "运行中", .size = 12, .color = CUI_COLOR_SUCCESS);
            }

            cui_text(ui, "|", .size = 13, .color = CUI_COLOR_BORDER);

            /* 2. Version */
            cui_text(ui, g_ai_space_version, .size = 12, .color = CUI_COLOR_TEXT_MUTED);

            cui_text(ui, "|", .size = 13, .color = CUI_COLOR_BORDER);

            /* 3. Hostname */
            char host[64] = "devbox";
            gethostname(host, sizeof(host));
            char *dot = strchr(host, '.');
            if (dot) *dot = '\0';

            cui_row(ui, .gap = 5, .align = CUI_ALIGN_CENTER) {
                cui_icon(ui, CUI_ICON_MONITOR, 13, CUI_COLOR_TEXT_MUTED);
                cui_text(ui, host[0] ? host : "devbox", .size = 12, .color = CUI_COLOR_TEXT_MUTED);
            }

            cui_text(ui, "|", .size = 13, .color = CUI_COLOR_BORDER);

            /* 4. Real-time Clock */
            time_t now = time(NULL);
            struct tm *tm_info = localtime(&now);
            char time_str[64];
            strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);

            cui_row(ui, .gap = 5, .align = CUI_ALIGN_CENTER) {
                cui_icon(ui, CUI_ICON_CLOCK, 13, CUI_COLOR_TEXT_MUTED);
                cui_text(ui, time_str, .size = 12, .color = CUI_COLOR_TEXT_MUTED);
            }
        }
    }
}

/* Render Dashboard Page */
void render_dashboard_page(cui_ctx *ui, app_state_t *s) {
    if (!ui || !s) return;

    if (s->toast_frames > 0) s->toast_frames--;

    /* Kick off non-blocking hub scan if not yet loaded */
    if (!s->hub_matrix_loaded && !s->hub_scan.is_scanning) {
        start_hub_scan_thread(s);
    }

    /* Compute real metrics */
    int total_master_skills = 0;
    int in_sync_count = 0;
    int drifted_count = 0;
    int unadopted_count = 0;
    int conflict_count = s->sync_drift_loaded ? s->sync_drift.conflict_count : 0;
    int pending_total = 0;

    if (s->hub_matrix_loaded) {
        total_master_skills = s->hub_matrix.hub_managed_skills;
        for (int i = 0; i < s->hub_matrix.skill_count; i++) {
            if (!s->hub_matrix.skills[i].in_hub) {
                unadopted_count++;
            } else {
                if (s->hub_matrix.skills[i].modified_count > 0 ||
                    s->hub_matrix.skills[i].outdated_count > 0) {
                    drifted_count++;
                } else {
                    in_sync_count++;
                }
            }
        }
        pending_total = drifted_count + unadopted_count + conflict_count;
    }

    /* 1. Top Banner / Hero Card */
    cui_box(ui, .width = CUI_FILL, .radius = 8, .pad = 16, .gap = 14,
            .bg = cui_alpha(cui_color_of(ui, CUI_COLOR_ACCENT), 0.05f),
            .border = 1, .border_color = cui_alpha(cui_color_of(ui, CUI_COLOR_ACCENT), 0.25f)) {
        cui_row(ui, .gap = 14, .align = CUI_ALIGN_CENTER) {
            cui_box(ui, .width = CUI_PX(42), .height = CUI_PX(42), .radius = 8,
                    .bg = cui_alpha(cui_color_of(ui, CUI_COLOR_ACCENT), 0.14f),
                    .dir = CUI_STACK, .align = CUI_ALIGN_CENTER, .justify = CUI_JUSTIFY_CENTER) {
                cui_icon(ui, CUI_ICON_BOX, 24, CUI_COLOR_ACCENT);
            }
            cui_column(ui, .gap = 4, .width = CUI_FILL) {
                cui_heading(ui, "AI-Space — 您本地多工具 AI 技能的中央枢纽与基准调度中心", 4);
                cui_text(ui, "统一管理 Gemini、Antigravity、Cursor、Claude 等工具的技能，构建“基准库 + 分发拓扑 + 跨设备同步”的协同体系，让技能集中沉淀、统一分发、版本可控、多端一致。",
                         .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);
            }
        }
    }

    /* 2. Middle Area (Split into Left Topology Diagram and Right Tasks & Actions) */
    cui_row(ui, .width = CUI_FILL, .gap = 14, .align = CUI_ALIGN_STRETCH) {

        /* ---- LEFT: 系统运行拓扑与流程 ---- */
        cui_card(ui, .width = CUI_GROW(7), .pad = 16, .gap = 12) {
            cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                cui_icon(ui, CUI_ICON_SHARE_2, 16, CUI_COLOR_ACCENT);
                cui_heading(ui, "系统运行拓扑与流程", 4);
            }

            /* Box 1: 远端同步 */
            cui_box(ui, .width = CUI_FILL, .radius = 6, .pad = 12, .gap = 8,
                    .bg = cui_alpha(cui_color_of(ui, CUI_COLOR_ACCENT), 0.05f),
                    .border = 1, .border_color = cui_alpha(cui_color_of(ui, CUI_COLOR_ACCENT), 0.25f)) {
                cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                    cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                        cui_icon(ui, CUI_ICON_CLOUD, 20, CUI_COLOR_ACCENT);
                        cui_column(ui, .gap = 3) {
                            cui_heading(ui, "远端同步", 4);
                            cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                                cui_row(ui, .gap = 4, .align = CUI_ALIGN_CENTER) {
                                    cui_icon(ui, CUI_ICON_FOLDER, 12,
                                             (s->sync_cfg.type == SYNC_BACKEND_GIT) ? CUI_COLOR_ACCENT : CUI_COLOR_TEXT_MUTED);
                                    cui_text(ui, "Git 仓库", .size = 11,
                                             .color = (s->sync_cfg.type == SYNC_BACKEND_GIT) ? CUI_COLOR_FOREGROUND : CUI_COLOR_TEXT_MUTED);
                                }
                                cui_text(ui, "|", .size = 11, .color = CUI_COLOR_BORDER);
                                cui_row(ui, .gap = 4, .align = CUI_ALIGN_CENTER) {
                                    cui_icon(ui, CUI_ICON_GLOBE, 12,
                                             (s->sync_cfg.type == SYNC_BACKEND_WEBDAV) ? CUI_COLOR_ACCENT : CUI_COLOR_TEXT_MUTED);
                                    cui_text(ui, "WebDAV", .size = 11,
                                             .color = (s->sync_cfg.type == SYNC_BACKEND_WEBDAV) ? CUI_COLOR_FOREGROUND : CUI_COLOR_TEXT_MUTED);
                                }
                                cui_text(ui, "|", .size = 11, .color = CUI_COLOR_BORDER);
                                cui_row(ui, .gap = 4, .align = CUI_ALIGN_CENTER) {
                                    cui_icon(ui, CUI_ICON_DATABASE, 12,
                                             (s->sync_cfg.type == SYNC_BACKEND_NFS) ? CUI_COLOR_ACCENT : CUI_COLOR_TEXT_MUTED);
                                    cui_text(ui, "私有云 NFS", .size = 11,
                                             .color = (s->sync_cfg.type == SYNC_BACKEND_NFS) ? CUI_COLOR_FOREGROUND : CUI_COLOR_TEXT_MUTED);
                                }
                            }
                        }
                    }

                    cui_spacer(ui);

                    cui_column(ui, .align = CUI_ALIGN_END, .gap = 3) {
                        if (s->sync_cfg.type == SYNC_BACKEND_NONE) {
                            cui_badge(ui, "未配置", .tone = CUI_TONE_NEUTRAL, .size = CUI_SIZE_SM);
                            cui_text(ui, "单机本地模式", .size = 11, .color = CUI_COLOR_TEXT_MUTED);
                        } else if (s->sync_drift_loaded) {
                            cui_badge(ui, "已连接", .tone = CUI_TONE_SUCCESS, .size = CUI_SIZE_SM);
                            if (s->sync_drift.last_checked_at[0]) {
                                cui_textf(ui, "上次: %s", s->sync_drift.last_checked_at);
                            } else {
                                cui_text(ui, "-", .size = 11, .color = CUI_COLOR_TEXT_MUTED);
                            }
                        } else {
                            cui_badge(ui, "已配置", .tone = CUI_TONE_INFO, .size = CUI_SIZE_SM);
                            cui_text(ui, "待检测同步", .size = 11, .color = CUI_COLOR_TEXT_MUTED);
                        }
                    }
                }
            }

            /* Connector Arrow 1: 拉取 / 推送 */
            cui_row(ui, .width = CUI_FILL, .justify = CUI_JUSTIFY_CENTER, .align = CUI_ALIGN_CENTER, .gap = 8) {
                cui_icon(ui, CUI_ICON_ARROW_DOWN, 13, CUI_COLOR_ACCENT);
                cui_text(ui, "拉取：获取远端更新  /  推送：同步本地变更",
                         .size = 11, .color = CUI_COLOR_TEXT_SECONDARY);
                cui_icon(ui, CUI_ICON_ARROW_UP, 13, CUI_COLOR_ACCENT);
            }

            /* Box 2: 主仓基准库 */
            cui_box(ui, .width = CUI_FILL, .radius = 6, .pad = 14, .gap = 12,
                    .bg = cui_color_of(ui, CUI_COLOR_SURFACE_RAISED),
                    .border = 1.5f, .border_color = cui_color_of(ui, CUI_COLOR_ACCENT)) {

                cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                    cui_row(ui, .gap = 10, .align = CUI_ALIGN_CENTER) {
                        cui_icon(ui, CUI_ICON_DATABASE, 24, CUI_COLOR_ACCENT);
                        cui_column(ui, .gap = 2) {
                            cui_heading(ui, "主仓基准库", 4);
                            cui_text(ui, "~/.ai-space/skills/ · 唯一基准真理源",
                                     .size = 11, .color = CUI_COLOR_TEXT_MUTED);
                        }
                    }

                    cui_spacer(ui);

                    cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                        cui_icon(ui, CUI_ICON_LAYERS, 16, CUI_COLOR_TEXT_MUTED);
                        cui_column(ui, .align = CUI_ALIGN_END) {
                            cui_text(ui, "技能总数", .size = 10, .color = CUI_COLOR_TEXT_MUTED);
                            if (s->hub_matrix_loaded) {
                                cui_textf(ui, "%d", total_master_skills);
                            } else {
                                cui_text(ui, "-", .size = 11, .color = CUI_COLOR_TEXT_MUTED);
                            }
                        }
                        cui_badge(ui, g_ai_space_version, .tone = CUI_TONE_INFO, .size = CUI_SIZE_SM);
                    }
                }

                /* 4 Action Pills inside Master Hub */
                cui_row(ui, .width = CUI_FILL, .justify = CUI_JUSTIFY_BETWEEN, .gap = 8) {
                    if (cui_button(ui, "全局收录", .variant = CUI_BUTTON_OUTLINE, .icon = CUI_ICON_CHECK, .size = CUI_SIZE_SM)) {
                        s->current_page = PAGE_ALL_SKILLS;
                        start_hub_scan_thread(s);
                    }
                    if (cui_button(ui, "快照备份", .variant = CUI_BUTTON_OUTLINE, .icon = CUI_ICON_SHIELD, .size = CUI_SIZE_SM)) {
                        app_set_toast(s, "快照安全备份服务已就绪");
                    }
                    if (cui_button(ui, "版本对比", .variant = CUI_BUTTON_OUTLINE, .icon = CUI_ICON_GIT_BRANCH, .size = CUI_SIZE_SM)) {
                        s->current_page = PAGE_ALL_SKILLS;
                    }
                    if (cui_button(ui, "历史回溯", .variant = CUI_BUTTON_OUTLINE, .icon = CUI_ICON_CLOCK, .size = CUI_SIZE_SM)) {
                        app_set_toast(s, "历史快照浏览");
                    }
                }
            }

            /* Connector Arrows 2: 收录/合入 (↑) vs 分发/覆盖/安装 (↓) */
            cui_row(ui, .width = CUI_FILL, .justify = CUI_JUSTIFY_BETWEEN, .align = CUI_ALIGN_CENTER) {
                cui_row(ui, .gap = 6, .align = CUI_ALIGN_CENTER) {
                    cui_icon(ui, CUI_ICON_ARROW_UP, 14, CUI_COLOR_SUCCESS);
                    cui_column(ui, .gap = 1) {
                        cui_text(ui, "收录 / 合入", .size = 11, .font = CUI_FONT_MEDIUM, .color = CUI_COLOR_SUCCESS);
                        cui_text(ui, "将本地优质技能纳入基准库", .size = 10, .color = CUI_COLOR_TEXT_MUTED);
                    }
                }

                cui_row(ui, .gap = 6, .align = CUI_ALIGN_CENTER) {
                    cui_icon(ui, CUI_ICON_ARROW_DOWN, 14, CUI_COLOR_ACCENT);
                    cui_column(ui, .gap = 1) {
                        cui_text(ui, "分发 / 覆盖 / 安装", .size = 11, .font = CUI_FONT_MEDIUM, .color = CUI_COLOR_ACCENT);
                        cui_text(ui, "将基准技能分发到各本地工具", .size = 10, .color = CUI_COLOR_TEXT_MUTED);
                    }
                }
            }

            /* Box 3: 本地工具层 */
            cui_box(ui, .width = CUI_FILL, .radius = 6, .pad = 12, .gap = 10,
                    .bg = cui_alpha(cui_color_of(ui, CUI_COLOR_SURFACE), 0.7f),
                    .border = 1, .border_color = cui_color_of(ui, CUI_COLOR_BORDER)) {
                cui_row(ui, .gap = 6, .align = CUI_ALIGN_CENTER) {
                    cui_icon(ui, CUI_ICON_MONITOR, 14, CUI_COLOR_TEXT_SECONDARY);
                    cui_text(ui, "本地工具层", .font = CUI_FONT_MEDIUM, .size = 12);
                }

                /* Tool Cards Row: dynamically show real tools */
                cui_row(ui, .width = CUI_FILL, .gap = 8, .align = CUI_ALIGN_STRETCH) {
                    int show_cnt = s->record_count > 4 ? 4 : s->record_count;
                    if (show_cnt == 0) {
                        cui_text(ui, "暂未识别到本地工具目录，可在系统设置中添加配置", .size = 11, .color = CUI_COLOR_TEXT_MUTED);
                    } else {
                        for (int i = 0; i < show_cnt; i++) {
                            tool_record_t *rec = &s->records[i];
                            cui_box(ui, .width = CUI_GROW(1), .radius = 5, .pad = 8, .gap = 4,
                                    .bg = cui_color_of(ui, CUI_COLOR_SURFACE_RAISED),
                                    .border = 1, .border_color = cui_color_of(ui, CUI_COLOR_BORDER)) {
                                cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                                    cui_icon(ui, CUI_ICON_TERMINAL, 13, CUI_COLOR_ACCENT);
                                    cui_text(ui, rec->name, .font = CUI_FONT_MEDIUM, .size = 12);
                                    cui_spacer(ui);
                                    cui_icon(ui, (rec->skill_count > 0) ? CUI_ICON_CIRCLE_CHECK : CUI_ICON_CIRCLE,
                                             12, (rec->skill_count > 0) ? CUI_COLOR_SUCCESS : CUI_COLOR_TEXT_MUTED);
                                }
                                cui_text(ui, "skills/", .size = 10, .color = CUI_COLOR_TEXT_MUTED);
                                cui_textf(ui, "└─ %d 项技能", rec->skill_count);
                            }
                        }
                        if (s->record_count > 4) {
                            cui_box(ui, .width = CUI_PX(48), .radius = 5, .pad = 8,
                                    .bg = cui_alpha(cui_color_of(ui, CUI_COLOR_SURFACE_MUTED), 0.5f),
                                    .border = 1, .border_color = cui_color_of(ui, CUI_COLOR_BORDER),
                                    .dir = CUI_STACK, .align = CUI_ALIGN_CENTER, .justify = CUI_JUSTIFY_CENTER) {
                                cui_textf(ui, "+%d", s->record_count - 4);
                            }
                        }
                    }
                }
            }
        }

        /* ---- RIGHT: 待处理任务 + 常用操作 ---- */
        cui_column(ui, .width = CUI_GROW(3), .gap = 14) {

            /* Card 1: 待处理任务 (X) */
            cui_card(ui, .width = CUI_FILL, .pad = 14, .gap = 10) {
                cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                    cui_icon(ui, CUI_ICON_CLIPBOARD, 16, CUI_COLOR_ACCENT);
                    char t_hdr[64];
                    if (s->hub_matrix_loaded) {
                        snprintf(t_hdr, sizeof(t_hdr), "待处理任务 (%d)", pending_total);
                    } else {
                        snprintf(t_hdr, sizeof(t_hdr), "待处理任务 (0)");
                    }
                    cui_heading(ui, t_hdr, 4);
                }

                /* Task 1: 已同步 */
                cui_box(ui, .width = CUI_FILL, .radius = 5, .pad = 8, .gap = 4,
                        .bg = cui_color_of(ui, CUI_COLOR_SURFACE_RAISED)) {
                    cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                        cui_icon(ui, CUI_ICON_CIRCLE_CHECK, 16, CUI_COLOR_SUCCESS);
                        cui_column(ui, .gap = 1) {
                            cui_text(ui, "已同步 (最新一致)", .font = CUI_FONT_MEDIUM, .size = 12);
                            cui_text(ui, "主仓与本地工具一致，无需处理", .size = 10, .color = CUI_COLOR_TEXT_MUTED);
                        }
                        cui_spacer(ui);
                        char b_buf[16];
                        if (s->hub_matrix_loaded) {
                            snprintf(b_buf, sizeof(b_buf), "%d >", in_sync_count);
                        } else {
                            snprintf(b_buf, sizeof(b_buf), "0 >");
                        }
                        cui_badge(ui, b_buf, .tone = CUI_TONE_SUCCESS, .size = CUI_SIZE_SM);
                    }
                }

                /* Task 2: 存在差异 */
                cui_box(ui, .width = CUI_FILL, .radius = 5, .pad = 8, .gap = 4,
                        .bg = cui_color_of(ui, CUI_COLOR_SURFACE_RAISED)) {
                    cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                        cui_icon(ui, CUI_ICON_REFRESH_CW, 16, CUI_COLOR_WARNING);
                        cui_column(ui, .gap = 1) {
                            cui_text(ui, "存在差异 (需处理)", .font = CUI_FONT_MEDIUM, .size = 12);
                            cui_text(ui, "本地与主仓版本不一致，建议对比合并", .size = 10, .color = CUI_COLOR_TEXT_MUTED);
                        }
                        cui_spacer(ui);
                        char b_buf[16];
                        if (s->hub_matrix_loaded) {
                            snprintf(b_buf, sizeof(b_buf), "%d >", drifted_count);
                        } else {
                            snprintf(b_buf, sizeof(b_buf), "0 >");
                        }
                        cui_badge(ui, b_buf, .tone = (drifted_count > 0) ? CUI_TONE_WARNING : CUI_TONE_NEUTRAL, .size = CUI_SIZE_SM);
                    }
                }

                /* Task 3: 本地独有 */
                cui_box(ui, .width = CUI_FILL, .radius = 5, .pad = 8, .gap = 4,
                        .bg = cui_color_of(ui, CUI_COLOR_SURFACE_RAISED)) {
                    cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                        cui_icon(ui, CUI_ICON_PLUS_CIRCLE, 16, CUI_COLOR_ACCENT);
                        cui_column(ui, .gap = 1) {
                            cui_text(ui, "本地独有 (可收录)", .font = CUI_FONT_MEDIUM, .size = 12);
                            cui_text(ui, "仅存在于本地工具，建议合入主仓", .size = 10, .color = CUI_COLOR_TEXT_MUTED);
                        }
                        cui_spacer(ui);
                        char b_buf[16];
                        if (s->hub_matrix_loaded) {
                            snprintf(b_buf, sizeof(b_buf), "%d >", unadopted_count);
                        } else {
                            snprintf(b_buf, sizeof(b_buf), "0 >");
                        }
                        cui_badge(ui, b_buf, .tone = (unadopted_count > 0) ? CUI_TONE_INFO : CUI_TONE_NEUTRAL, .size = CUI_SIZE_SM);
                    }
                }

                /* Task 4: 存在冲突 */
                cui_box(ui, .width = CUI_FILL, .radius = 5, .pad = 8, .gap = 4,
                        .bg = cui_color_of(ui, CUI_COLOR_SURFACE_RAISED)) {
                    cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                        cui_icon(ui, CUI_ICON_TRIANGLE_ALERT, 16, CUI_COLOR_DANGER);
                        cui_column(ui, .gap = 1) {
                            cui_text(ui, "存在冲突 (需解决)", .font = CUI_FONT_MEDIUM, .size = 12);
                            cui_text(ui, "多端变更产生冲突，请手动合并", .size = 10, .color = CUI_COLOR_TEXT_MUTED);
                        }
                        cui_spacer(ui);
                        char b_buf[16];
                        snprintf(b_buf, sizeof(b_buf), "%d >", conflict_count);
                        cui_badge(ui, b_buf, .tone = (conflict_count > 0) ? CUI_TONE_DANGER : CUI_TONE_NEUTRAL, .size = CUI_SIZE_SM);
                    }
                }
            }

            /* Card 2: 常用操作 (2x2 Grid) */
            cui_card(ui, .width = CUI_FILL, .pad = 14, .gap = 10) {
                cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                    cui_icon(ui, CUI_ICON_ZAP, 16, CUI_COLOR_ACCENT);
                    cui_heading(ui, "常用操作", 4);
                }

                /* Row 1 */
                cui_row(ui, .width = CUI_FILL, .gap = 8) {
                    if (cui_button(ui, "一键同步", .variant = CUI_BUTTON_PRIMARY,
                                   .icon = CUI_ICON_REFRESH_CW, .size = CUI_SIZE_SM)) {
                        s->current_page = PAGE_SYNC_MANAGER;
                        start_sync_check_thread(s);
                    }
                    if (cui_button(ui, "分发工具", .variant = CUI_BUTTON_OUTLINE,
                                   .icon = CUI_ICON_SEND, .size = CUI_SIZE_SM)) {
                        s->current_page = PAGE_HUB_MATRIX;
                    }
                }

                /* Row 2 */
                cui_row(ui, .width = CUI_FILL, .gap = 8) {
                    if (cui_button(ui, "收录技能", .variant = CUI_BUTTON_OUTLINE,
                                   .icon = CUI_ICON_ARROW_UP, .size = CUI_SIZE_SM)) {
                        s->current_page = PAGE_ALL_SKILLS;
                    }
                    if (cui_button(ui, "对比差异", .variant = CUI_BUTTON_OUTLINE,
                                   .icon = CUI_ICON_GIT_BRANCH, .size = CUI_SIZE_SM)) {
                        s->current_page = PAGE_ALL_SKILLS;
                    }
                }
            }
        }
    }

    /* 3. Bottom Row: 运行概览 */
    cui_column(ui, .width = CUI_FILL, .gap = 8) {
        cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
            cui_icon(ui, CUI_ICON_ACTIVITY, 16, CUI_COLOR_ACCENT);
            cui_heading(ui, "运行概览", 4);
        }

        cui_row(ui, .width = CUI_FILL, .gap = 10, .align = CUI_ALIGN_STRETCH) {
            /* 1. 主仓技能 */
            cui_card(ui, .width = CUI_GROW(1), .pad = 12, .gap = 4) {
                cui_row(ui, .gap = 6, .align = CUI_ALIGN_CENTER) {
                    cui_icon(ui, CUI_ICON_DATABASE, 14, CUI_COLOR_ACCENT);
                    cui_text(ui, "主仓技能", .size = 11, .color = CUI_COLOR_TEXT_MUTED);
                }
                cui_row(ui, .gap = 8, .align = CUI_ALIGN_END) {
                    if (s->hub_matrix_loaded) {
                        char sk_txt[32];
                        snprintf(sk_txt, sizeof(sk_txt), "%d", total_master_skills);
                        cui_text(ui, sk_txt, .font = CUI_FONT_DISPLAY, .size = 20);
                    } else {
                        cui_text(ui, "-", .font = CUI_FONT_DISPLAY, .size = 20, .color = CUI_COLOR_TEXT_MUTED);
                    }
                }
            }

            /* 2. 本地工具 */
            cui_card(ui, .width = CUI_GROW(1), .pad = 12, .gap = 4) {
                cui_row(ui, .gap = 6, .align = CUI_ALIGN_CENTER) {
                    cui_icon(ui, CUI_ICON_MONITOR, 14, CUI_COLOR_ACCENT);
                    cui_text(ui, "本地工具", .size = 11, .color = CUI_COLOR_TEXT_MUTED);
                }
                cui_row(ui, .gap = 8, .align = CUI_ALIGN_END) {
                    if (s->record_count > 0) {
                        char tl_txt[32];
                        snprintf(tl_txt, sizeof(tl_txt), "%d", s->record_count);
                        cui_text(ui, tl_txt, .font = CUI_FONT_DISPLAY, .size = 20);
                    } else {
                        cui_text(ui, "-", .font = CUI_FONT_DISPLAY, .size = 20, .color = CUI_COLOR_TEXT_MUTED);
                    }
                }
            }

            /* 3. 同步状态 */
            cui_card(ui, .width = CUI_GROW(1), .pad = 12, .gap = 4) {
                cui_row(ui, .gap = 6, .align = CUI_ALIGN_CENTER) {
                    cui_icon(ui, CUI_ICON_REFRESH_CW, 14, CUI_COLOR_ACCENT);
                    cui_text(ui, "同步状态", .size = 11, .color = CUI_COLOR_TEXT_MUTED);
                }
                cui_row(ui, .gap = 6, .align = CUI_ALIGN_CENTER) {
                    if (s->sync_cfg.type == SYNC_BACKEND_NONE) {
                        cui_badge(ui, "未配置", .tone = CUI_TONE_NEUTRAL, .size = CUI_SIZE_SM);
                    } else if (s->sync_drift_loaded) {
                        if (s->sync_drift.conflict_count > 0) {
                            cui_badge(ui, "有冲突", .tone = CUI_TONE_DANGER, .size = CUI_SIZE_SM);
                        } else if (s->sync_drift.remote_ahead_count > 0 ||
                                   s->sync_drift.local_ahead_count > 0 ||
                                   s->sync_drift.remote_only_count > 0 ||
                                   s->sync_drift.local_only_count > 0) {
                            cui_badge(ui, "有差异", .tone = CUI_TONE_WARNING, .size = CUI_SIZE_SM);
                        } else {
                            cui_badge(ui, "已同步", .tone = CUI_TONE_SUCCESS, .size = CUI_SIZE_SM);
                        }
                    } else {
                        cui_badge(ui, "待检测", .tone = CUI_TONE_NEUTRAL, .size = CUI_SIZE_SM);
                    }
                }
                if (s->sync_drift_loaded && s->sync_drift.last_checked_at[0]) {
                    cui_textf(ui, "上次: %s", s->sync_drift.last_checked_at);
                } else {
                    cui_text(ui, "-", .size = 10, .color = CUI_COLOR_TEXT_MUTED);
                }
            }

            /* 4. 待处理项 */
            cui_card(ui, .width = CUI_GROW(1), .pad = 12, .gap = 4) {
                cui_row(ui, .gap = 6, .align = CUI_ALIGN_CENTER) {
                    cui_icon(ui, CUI_ICON_GIT_BRANCH, 14, CUI_COLOR_ACCENT);
                    cui_text(ui, "待处理项", .size = 11, .color = CUI_COLOR_TEXT_MUTED);
                }
                cui_row(ui, .gap = 8, .align = CUI_ALIGN_END) {
                    if (s->hub_matrix_loaded) {
                        char pnd_txt[32];
                        snprintf(pnd_txt, sizeof(pnd_txt), "%d", pending_total);
                        cui_text(ui, pnd_txt, .font = CUI_FONT_DISPLAY, .size = 20);
                        if (conflict_count > 0) {
                            cui_textf(ui, "含 %d 冲突", conflict_count);
                        } else if (drifted_count > 0) {
                            cui_textf(ui, "含 %d 差异", drifted_count);
                        }
                    } else {
                        cui_text(ui, "-", .font = CUI_FONT_DISPLAY, .size = 20, .color = CUI_COLOR_TEXT_MUTED);
                    }
                }
            }

            /* 5. 历史快照 */
            int snap_count = count_snapshots(s->setting_backup_dir);
            cui_card(ui, .width = CUI_GROW(1), .pad = 12, .gap = 4) {
                cui_row(ui, .gap = 6, .align = CUI_ALIGN_CENTER) {
                    cui_icon(ui, CUI_ICON_CLOCK, 14, CUI_COLOR_ACCENT);
                    cui_text(ui, "历史快照", .size = 11, .color = CUI_COLOR_TEXT_MUTED);
                }
                cui_row(ui, .gap = 8, .align = CUI_ALIGN_END) {
                    char snap_txt[32];
                    snprintf(snap_txt, sizeof(snap_txt), "%d", snap_count);
                    cui_text(ui, snap_txt, .font = CUI_FONT_DISPLAY, .size = 20);
                    if (snap_count > 0) {
                        cui_textf(ui, "基准: %s", g_ai_space_version);
                    }
                }
            }

            /* 6. 远端仓库 */
            cui_card(ui, .width = CUI_GROW(1), .pad = 12, .gap = 4) {
                cui_row(ui, .gap = 6, .align = CUI_ALIGN_CENTER) {
                    cui_icon(ui, CUI_ICON_CLOUD, 14, CUI_COLOR_ACCENT);
                    cui_text(ui, "远端仓库", .size = 11, .color = CUI_COLOR_TEXT_MUTED);
                }
                cui_row(ui, .gap = 6, .align = CUI_ALIGN_CENTER) {
                    if (s->sync_cfg.type == SYNC_BACKEND_NONE) {
                        cui_badge(ui, "未配置", .tone = CUI_TONE_NEUTRAL, .size = CUI_SIZE_SM);
                    } else {
                        const char *b_short = "Git";
                        if (s->sync_cfg.type == SYNC_BACKEND_WEBDAV) b_short = "WebDAV";
                        else if (s->sync_cfg.type == SYNC_BACKEND_NFS) b_short = "NFS";
                        char repo_badge[64];
                        bool is_connected = (s->sync_backend != NULL);
                        snprintf(repo_badge, sizeof(repo_badge), "%s (%s)", b_short, is_connected ? "已连接" : "已配置");
                        cui_badge(ui, repo_badge, .tone = is_connected ? CUI_TONE_SUCCESS : CUI_TONE_INFO, .size = CUI_SIZE_SM);
                    }
                }
            }
        }
    }
}
