#include "app_sync.h"
#include "app_hub_tasks.h"
#include "view_dashboard.h"
#include "view_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "portability.h"

void render_app_top_bar(cui_ctx *ui, app_state_t *s) {
    cui_row(ui, .width = CUI_FILL, .height = CUI_PX(44), .pad_x = 16,
            .align = CUI_ALIGN_CENTER, .bg = CUI_COLOR_SURFACE) {

        /* App identity */
        cui_row(ui, .gap = 10, .align = CUI_ALIGN_CENTER) {
            cui_box(ui, .width = CUI_PX(30), .height = CUI_PX(30), .radius = 9,
                    .bg = cui_color_of(ui, CUI_COLOR_ACCENT),
                    .elevation = CUI_ELEVATION_RESTING,
                    .dir = CUI_STACK, .align = CUI_ALIGN_CENTER, .justify = CUI_JUSTIFY_CENTER) {
                cui_icon(ui, CUI_ICON_BOX, 18, CUI_COLOR_ACCENT_FOREGROUND);
            }

            cui_text(ui, "SkillBoard", .font = CUI_FONT_DISPLAY, .size = 16);
        }

        cui_spacer(ui);

        /* Right: Running Status, Version, Hostname, Settings */
        cui_row(ui, .gap = 12, .align = CUI_ALIGN_CENTER) {
            /* 1. Status */
            cui_row(ui, .gap = 5, .align = CUI_ALIGN_CENTER) {
                cui_icon(ui, CUI_ICON_DOT, 14, CUI_COLOR_SUCCESS);
                cui_text(ui, "运行中", .size = 12, .color = CUI_COLOR_SUCCESS);
            }

            cui_separator(ui);

            /* 2. Version */
            cui_text(ui, g_skillboard_version, .size = 12, .color = CUI_COLOR_TEXT_MUTED);

            cui_separator(ui);

            /* 3. Hostname */
            char host[64] = "devbox";
            gethostname(host, sizeof(host));
            char *dot = strchr(host, '.');
            if (dot) *dot = '\0';

            cui_row(ui, .gap = 5, .align = CUI_ALIGN_CENTER) {
                cui_icon(ui, CUI_ICON_MONITOR, 13, CUI_COLOR_TEXT_MUTED);
                cui_text(ui, host[0] ? host : "devbox", .size = 12, .color = CUI_COLOR_TEXT_MUTED);
            }

            cui_separator(ui);
            if (cui_icon_button(ui, CUI_ICON_SETTINGS,
                                .variant = s->current_page == PAGE_SETTINGS ? CUI_BUTTON_SECONDARY : CUI_BUTTON_GHOST,
                                .tooltip = "系统设置")) {
                s->current_page = PAGE_SETTINGS;
            }
        }
    }
}

static bool dashboard_task(cui_ctx *ui, const char *label, const char *icon,
                           int count, cui_color color, cui_tone tone) {
    bool clicked = cui_card_begin(ui, &(cui_card_opts){
        .id = label, .width = CUI_FILL, .pad = 10, .hoverable = count > 0,
    });
    cui_row(ui, .width = CUI_FILL, .gap = 8, .align = CUI_ALIGN_CENTER) {
        cui_icon(ui, icon, 16, count > 0 ? color : CUI_COLOR_TEXT_MUTED);
        cui_text(ui, label, .font = CUI_FONT_MEDIUM,
                 .color = count > 0 ? CUI_COLOR_FOREGROUND : CUI_COLOR_TEXT_SECONDARY);
        cui_spacer(ui);
        char value[24];
        snprintf(value, sizeof(value), "%d", count);
        cui_badge(ui, value, .tone = count > 0 ? tone : CUI_TONE_NEUTRAL, .size = CUI_SIZE_SM);
        if (count > 0) {
            cui_icon(ui, CUI_ICON_CHEVRON_RIGHT, 14, CUI_COLOR_TEXT_SECONDARY);
        } else {
            cui_gap(ui, 14);
        }
    }
    cui_card_end(ui);
    return clicked;
}

static void open_dashboard_skill_filter(app_state_t *s, skill_filter_t filter) {
    s->current_page = PAGE_ALL_SKILLS;
    s->hub_subview = HUB_SUBVIEW_SCAN_MATRIX;
    s->hub_filter_mode = filter;
    s->hub_search_kw[0] = '\0';
}

static void paint_topology_board(cui_ctx *ui, cui_rect rect, void *data) {
    (void)data;
    cui_color surface = cui_color_of(ui, CUI_COLOR_SURFACE);
    cui_color accent = cui_color_of(ui, CUI_COLOR_ACCENT);
    bool dark = cui_get_theme_mode(ui) == CUI_THEME_DARK;
    cui_color top = cui_mix(surface, accent, dark ? 0.16f : 0.09f);
    cui_color border = cui_mix(cui_color_of(ui, CUI_COLOR_BORDER), accent, 0.26f);
    cui_color dot = cui_alpha(cui_color_of(ui, CUI_COLOR_TEXT_MUTED), dark ? 0.19f : 0.18f);
    cui_draw_rect_gradient(ui, rect, CUI_CORNERS(18), top, surface);
    for (float y = rect.y + 16; y < rect.y + rect.h - 15; y += 20) {
        for (float x = rect.x + 16; x < rect.x + rect.w - 15; x += 20) {
            cui_draw_circle(ui, (cui_vec2){x, y}, 0.8f, dot);
        }
    }
    cui_draw_rect_border(ui, rect, CUI_CORNERS(18), cui_alpha(surface, 0), border, 1);
}

static void render_dashboard_topology(cui_ctx *ui, app_state_t *s) {
    bool compact = cui_viewport_size(ui).x < COMPACT_WORKSPACE_WIDTH;
    cui_color surface = cui_color_of(ui, CUI_COLOR_SURFACE);
    cui_color accent = cui_color_of(ui, CUI_COLOR_ACCENT);
    bool dark = cui_get_theme_mode(ui) == CUI_THEME_DARK;
    cui_box(ui, .width = CUI_GROW(7), .dir = CUI_STACK, .align = CUI_ALIGN_START) {
        cui_canvas(ui, "topology_board", CUI_FILL, CUI_FILL, paint_topology_board, NULL, 0);
        cui_column(ui, .width = CUI_FILL, .pad = 16, .gap = 12) {
        cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
            cui_icon(ui, CUI_ICON_SHARE_2, 16, CUI_COLOR_ACCENT);
            cui_heading(ui, "系统运行拓扑与流程", 4);
        }

        /* Box 1: 远端同步 */
        cui_box(ui, .width = CUI_FILL, .radius = 6, .pad = 12, .gap = 8,
                .bg = cui_mix(surface, cui_color_of(ui, CUI_COLOR_INFO), dark ? 0.11f : 0.07f),
                .border = 1, .border_color = cui_mix(cui_color_of(ui, CUI_COLOR_BORDER), accent, 0.23f)) {
            cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                    cui_icon(ui, CUI_ICON_CLOUD, 20, CUI_COLOR_INFO);
                    cui_column(ui, .gap = 3) {
                        cui_heading(ui, "远端同步", 4);
                        cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                            cui_row(ui, .gap = 4, .align = CUI_ALIGN_CENTER) {
                                cui_icon(ui, CUI_ICON_GIT_BRANCH, 12,
                                         (s->sync_cfg.type == SYNC_BACKEND_GIT) ? CUI_COLOR_ACCENT : CUI_COLOR_TEXT_MUTED);
                                cui_text(ui, "Git 仓库", .size = 11,
                                         .color = (s->sync_cfg.type == SYNC_BACKEND_GIT) ? CUI_COLOR_FOREGROUND : CUI_COLOR_TEXT_MUTED);
                            }
                            cui_gap(ui, 2);
                            cui_row(ui, .gap = 4, .align = CUI_ALIGN_CENTER) {
                                cui_icon(ui, CUI_ICON_CLOUD, 12,
                                         (s->sync_cfg.type == SYNC_BACKEND_WEBDAV) ? CUI_COLOR_ACCENT : CUI_COLOR_TEXT_MUTED);
                                cui_text(ui, "WebDAV", .size = 11,
                                         .color = (s->sync_cfg.type == SYNC_BACKEND_WEBDAV) ? CUI_COLOR_FOREGROUND : CUI_COLOR_TEXT_MUTED);
                            }
                            cui_gap(ui, 2);
                            cui_row(ui, .gap = 4, .align = CUI_ALIGN_CENTER) {
                                cui_icon(ui, CUI_ICON_SERVER, 12,
                                         (s->sync_cfg.type == SYNC_BACKEND_NFS) ? CUI_COLOR_ACCENT : CUI_COLOR_TEXT_MUTED);
                                cui_text(ui, "NFS", .size = 11,
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
                        cui_badge(ui, "已检查", .tone = CUI_TONE_SUCCESS, .size = CUI_SIZE_SM);
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
        cui_box(ui, .width = CUI_FILL, .radius = 11, .pad = 14, .gap = 12,
                .bg = cui_mix(surface, accent, dark ? 0.24f : 0.10f),
                .border = 1, .border_color = cui_mix(cui_color_of(ui, CUI_COLOR_BORDER), accent, 0.62f),
                .elevation = CUI_ELEVATION_RESTING) {

            cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                cui_row(ui, .width = CUI_FILL, .gap = 10, .align = CUI_ALIGN_CENTER) {
                    cui_box(ui, .width = CUI_PX(35), .height = CUI_PX(35), .radius = 10,
                            .bg = accent, .dir = CUI_STACK, .align = CUI_ALIGN_CENTER,
                            .justify = CUI_JUSTIFY_CENTER) {
                        cui_icon(ui, ICON_MASTER_HUB, 21, CUI_COLOR_ACCENT_FOREGROUND);
                    }
                    cui_column(ui, .width = CUI_FILL, .gap = 2) {
                        cui_heading(ui, "主仓基准库", 4);
                        cui_text(ui, s->setting_hub_dir, .size = 11, .color = CUI_COLOR_TEXT_SECONDARY,
                                 .width = CUI_FILL, .nowrap = true);
                        cui_tooltip(ui, s->setting_hub_dir);
                    }
                }

                cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                    cui_icon(ui, CUI_ICON_LAYERS, 16, CUI_COLOR_TEXT_MUTED);
                    cui_column(ui, .align = CUI_ALIGN_END) {
                        cui_text(ui, "技能总数", .size = 10, .color = CUI_COLOR_TEXT_MUTED);
                        if (s->hub_matrix_loaded) {
                            cui_textf(ui, "%d", s->hub_matrix.hub_managed_skills);
                        } else {
                            cui_text(ui, "-", .size = 11, .color = CUI_COLOR_TEXT_MUTED);
                        }
                    }
                }
            }

            cui_row(ui, .width = CUI_FILL, .gap = 6, .wrap = true) {
                if (cui_button(ui, "主仓管理", .variant = CUI_BUTTON_GHOST,
                               .icon = ICON_MASTER_HUB, .size = CUI_SIZE_SM)) {
                    s->current_page = PAGE_HUB_MATRIX;
                    s->hub_subview = HUB_SUBVIEW_MASTER_SKILLS;
                }
                if (cui_button(ui, compact ? "收录" : "收录技能", .variant = CUI_BUTTON_GHOST,
                               .icon = ICON_COLLECT, .size = CUI_SIZE_SM, .tooltip = "查看待收录技能")) {
                    open_dashboard_skill_filter(s, SKILL_FILTER_UNCOLLECTED);
                }
                if (cui_button(ui, compact ? "对比" : "版本对比", .variant = CUI_BUTTON_GHOST,
                               .icon = ICON_COMPARE, .size = CUI_SIZE_SM, .tooltip = "查看有差异的技能")) {
                    open_dashboard_skill_filter(s, SKILL_FILTER_CHANGED);
                }
                if (cui_button(ui, "历史快照", .variant = CUI_BUTTON_GHOST,
                               .icon = CUI_ICON_HISTORY, .size = CUI_SIZE_SM)) {
                    s->current_page = PAGE_SETTINGS;
                    s->settings_active_tab = SETTINGS_TAB_LOCAL_STORAGE;
                    s->storage_stats.loaded = false;
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
                .bg = cui_color_of(ui, CUI_COLOR_SURFACE),
                .border = 1, .border_color = cui_color_of(ui, CUI_COLOR_BORDER)) {
            cui_row(ui, .width = CUI_FILL, .gap = 6, .align = CUI_ALIGN_CENTER) {
                cui_icon(ui, ICON_TOOLS, 14, CUI_COLOR_TEXT_SECONDARY);
                cui_text(ui, "本地工具层", .font = CUI_FONT_MEDIUM, .size = 12);
                cui_spacer(ui);
                if (cui_button(ui, "查看工具", .variant = CUI_BUTTON_GHOST, .size = CUI_SIZE_SM,
                               .icon_right = CUI_ICON_CHEVRON_RIGHT)) {
                    s->current_page = PAGE_TOOL_LIST;
                }
            }

            /* Tool Cards Row: dynamically show real tools */
            cui_row(ui, .width = CUI_FILL, .gap = 8, .align = CUI_ALIGN_STRETCH) {
                int limit = compact ? 3 : 4;
                int show_cnt = s->record_count > limit ? limit : s->record_count;
                if (show_cnt == 0) {
                    cui_text(ui, "扫描本机后，此处显示识别到的工具和技能数量。", .size = 11, .color = CUI_COLOR_TEXT_MUTED);
                } else {
                    for (int i = 0; i < show_cnt; i++) {
                        const tool_record_t *rec = &s->records[i];
                        cui_box(ui, .width = CUI_GROW(1), .radius = 5, .pad = 8, .gap = 4,
                                .bg = cui_color_of(ui, CUI_COLOR_SURFACE_RAISED),
                                .border = 1, .border_color = cui_color_of(ui, CUI_COLOR_BORDER)) {
                            cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                                cui_icon(ui, ICON_TOOLS, 13, CUI_COLOR_ACCENT);
                                cui_text(ui, rec->name, .font = CUI_FONT_MEDIUM, .size = 12);
                                cui_spacer(ui);
                                cui_icon(ui, (rec->skill_count > 0) ? CUI_ICON_CIRCLE_CHECK : CUI_ICON_CIRCLE,
                                         12, (rec->skill_count > 0) ? CUI_COLOR_SUCCESS : CUI_COLOR_TEXT_MUTED);
                            }
                            cui_text(ui, "skills/", .size = 10, .color = CUI_COLOR_TEXT_MUTED);
                            cui_textf(ui, "└─ %d 项技能", rec->skill_count);
                        }
                    }
                    if (s->record_count > show_cnt) {
                        cui_box(ui, .width = CUI_PX(48), .radius = 5, .pad = 8,
                                .bg = cui_alpha(cui_color_of(ui, CUI_COLOR_SURFACE_MUTED), 0.5f),
                                .border = 1, .border_color = cui_color_of(ui, CUI_COLOR_BORDER),
                                .dir = CUI_STACK, .align = CUI_ALIGN_CENTER, .justify = CUI_JUSTIFY_CENTER) {
                            cui_textf(ui, "+%d", s->record_count - show_cnt);
                        }
                    }
                }
            }
        }
    }
}
}

void render_dashboard_page(cui_ctx *ui, app_state_t *s) {
    if (s->toast_frames > 0) s->toast_frames--;

    if (!s->hub_matrix_loaded && !s->hub_scan.is_scanning && !s->hub_scan.failed) {
        start_hub_scan_thread(s);
    }

    int in_sync = 0, drifted = 0, uncollected = 0;
    int conflicts = s->sync_cfg.type != SYNC_BACKEND_NONE && s->sync_drift_loaded
                  ? s->sync_drift.conflict_count : 0;
    for (int i = 0; s->hub_matrix_loaded && i < s->hub_matrix.skill_count; i++) {
        const hub_skill_entry_t *skill = &s->hub_matrix.skills[i];
        if (!skill->in_hub) uncollected++;
        else if (skill->modified_count > 0 || skill->outdated_count > 0 ||
                 skill->hub_unreadable || skill->unreadable_count > 0) drifted++;
        else if (skill->installed_count > 0) in_sync++;
    }

    cui_row(ui, .width = CUI_FILL, .gap = 12, .align = CUI_ALIGN_CENTER) {
        render_workspace_heading(ui, CUI_ICON_LAYOUT_DASHBOARD, "首页", "本地技能、主仓与远端同步概览");
        if (s->sync_cfg.type != SYNC_BACKEND_NONE) {
            if (cui_button(ui, "检查同步", .variant = CUI_BUTTON_GHOST,
                           .icon = CUI_ICON_REFRESH_CW, .disabled = s->sync_is_busy || s->op_progress.active)) {
                s->current_page = PAGE_SYNC_MANAGER;
                start_sync_check_thread(s);
            }
        } else if (cui_button(ui, "配置远端", .variant = CUI_BUTTON_GHOST,
                              .icon = CUI_ICON_SETTINGS)) {
            s->current_page = PAGE_SETTINGS;
            s->settings_active_tab = SETTINGS_TAB_REMOTE_SYNC;
        }
    }

    cui_row(ui, .width = CUI_FILL, .gap = 14, .align = CUI_ALIGN_START) {
        render_dashboard_topology(ui, s);
        cui_card(ui, .width = CUI_GROW(4), .pad = 16, .gap = 10) {
            cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                cui_icon(ui, CUI_ICON_LIST_CHECKS, 18, CUI_COLOR_ACCENT_STRONG);
                cui_gap(ui, 8);
                cui_heading(ui, "待处理", 4);
                cui_spacer(ui);
                if (s->hub_matrix_loaded) {
                    char pending[32];
                    snprintf(pending, sizeof(pending), "%d 项", conflicts + drifted + uncollected);
                    cui_badge(ui, pending, .tone = CUI_TONE_NEUTRAL, .size = CUI_SIZE_SM);
                } else {
                    cui_badge(ui, "扫描中", .tone = CUI_TONE_INFO, .size = CUI_SIZE_SM);
                }
            }
            if (dashboard_task(ui, "远端冲突", CUI_ICON_TRIANGLE_ALERT, conflicts, CUI_COLOR_DANGER, CUI_TONE_DANGER)) {
                s->current_page = PAGE_SYNC_MANAGER;
            }
            if (dashboard_task(ui, "副本有差异", ICON_COMPARE, drifted, CUI_COLOR_WARNING, CUI_TONE_WARNING)) {
                open_dashboard_skill_filter(s, SKILL_FILTER_CHANGED);
            }
            if (dashboard_task(ui, "技能待收录", ICON_COLLECT, uncollected, CUI_COLOR_ACCENT, CUI_TONE_ACCENT)) {
                open_dashboard_skill_filter(s, SKILL_FILTER_UNCOLLECTED);
            }
        }
    }

    char tools_count[24], hub_count[24], synced_count[24], snapshots_count[24];
    const char *backup = s->setting_backup_dir[0] ? s->setting_backup_dir : STORAGE_DEFAULT_BACKUP;
    int snapshots = app_storage_directory(s, STORAGE_PATH_DASHBOARD_BACKUP, backup)->subdirs;
    snprintf(tools_count, sizeof(tools_count), "%d", s->record_count);
    snprintf(snapshots_count, sizeof(snapshots_count), "%d", snapshots);
    snprintf(hub_count, sizeof(hub_count), "%d", s->hub_matrix.hub_managed_skills);
    snprintf(synced_count, sizeof(synced_count), "%d", in_sync);

    cui_row(ui, .width = CUI_FILL, .gap = 12, .align = CUI_ALIGN_STRETCH) {
        cui_metric(ui, "本地工具", tools_count, .icon = ICON_TOOLS, .width = CUI_GROW(1));
        cui_metric(ui, "主仓技能", s->hub_matrix_loaded ? hub_count : "—",
                   .icon = ICON_MASTER_HUB, .width = CUI_GROW(1));
        cui_metric(ui, "副本已一致", s->hub_matrix_loaded ? synced_count : "—",
                   .icon = CUI_ICON_CIRCLE_CHECK, .width = CUI_GROW(1));
        cui_metric(ui, "历史快照", snapshots_count, .icon = CUI_ICON_HISTORY, .width = CUI_GROW(1));
    }
}
