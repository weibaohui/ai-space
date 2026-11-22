#include "view_sync.h"
#include "view_common.h"
#include "sync_backend.h"
#include "hub_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void render_sync_manager_page(cui_ctx *ui, app_state_t *s) {
    if (!ui || !s) return;

    /* 0. Global Navigation Bar */
    render_global_navbar(ui, s);

    /* 1. Page Header */
    cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
        cui_row(ui, .gap = 12, .align = CUI_ALIGN_CENTER) {
            cui_box(ui, .width = CUI_PX(42), .height = CUI_PX(42), .radius = 10,
                    .bg = cui_alpha(cui_color_of(ui, CUI_COLOR_ACCENT), 0.15f),
                    .dir = CUI_STACK, .align = CUI_ALIGN_CENTER, .justify = CUI_JUSTIFY_CENTER) {
                cui_icon(ui, CUI_ICON_GLOBE, 22, CUI_COLOR_ACCENT);
            }
            cui_column(ui, .gap = 3) {
                cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                    cui_heading(ui, "跨主机远端同步管理 (Remote Synchronization)", 3);
                    cui_badge(ui, sync_backend_display_name(s->sync_cfg.type),
                              .tone = (s->sync_cfg.type != SYNC_BACKEND_NONE) ? CUI_TONE_ACCENT : CUI_TONE_NEUTRAL,
                              .size = CUI_SIZE_SM);
                }
                cui_text(ui, "主机无关架构：多台主机配置相同后端即可共享云端主仓。一台主机变更后，其他主机即时感知并同步更新。",
                         .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);
            }
        }

        cui_spacer(ui);

        cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
            if (s->sync_is_busy) {
                cui_spinner(ui, 16, .style = CUI_SPINNER_COMET, .color = CUI_COLOR_ACCENT);
                cui_text(ui, "正在与远端通信...", .size = 12, .color = CUI_COLOR_TEXT_MUTED);
            } else if (s->sync_drift.last_checked_at[0]) {
                cui_icon(ui, CUI_ICON_CLOCK, 13, CUI_COLOR_TEXT_MUTED);
                cui_text(ui, s->sync_drift.last_checked_at, .size = 12, .color = CUI_COLOR_TEXT_MUTED);
            }
        }
    }

    /* 2. Metric Overview Cards */
    cui_row(ui, .width = CUI_FILL, .gap = 14) {
        char pull_cnt[32], push_cnt[32], sync_cnt[32];
        int to_pull = s->sync_drift.remote_ahead_count + s->sync_drift.remote_only_count;
        int to_push = s->sync_drift.local_ahead_count + s->sync_drift.local_only_count;

        snprintf(pull_cnt, sizeof(pull_cnt), "%d 个", to_pull);
        snprintf(push_cnt, sizeof(push_cnt), "%d 个", to_push);
        snprintf(sync_cnt, sizeof(sync_cnt), "%d 个", s->sync_drift.in_sync_count);

        cui_card(ui, .width = CUI_GROW(1), .pad = 14) {
            cui_metric(ui, "激活后端", sync_backend_name(s->sync_cfg.type), .icon = CUI_ICON_GLOBE);
        }
        cui_card(ui, .width = CUI_GROW(1), .pad = 14) {
            cui_metric(ui, "待从远端拉取", pull_cnt, .icon = CUI_ICON_DOWNLOAD);
        }
        cui_card(ui, .width = CUI_GROW(1), .pad = 14) {
            cui_metric(ui, "待推送到远端", push_cnt, .icon = CUI_ICON_UPLOAD);
        }
        cui_card(ui, .width = CUI_GROW(1), .pad = 14) {
            cui_metric(ui, "两端完全一致", sync_cnt, .icon = CUI_ICON_CHECK);
        }
    }

    /* 3. Action Toolbar */
    cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
        cui_row(ui, .gap = 10, .align = CUI_ALIGN_CENTER) {
            if (cui_button(ui, " 检查远端变动", .variant = CUI_BUTTON_OUTLINE, .icon = CUI_ICON_REFRESH_CW)) {
                start_sync_check_thread(s);
            }

            int to_pull = s->sync_drift.remote_ahead_count + s->sync_drift.remote_only_count;
            if (cui_button(ui, " 全量从远端拉取 (Pull All)",
                           .variant = (to_pull > 0) ? CUI_BUTTON_PRIMARY : CUI_BUTTON_SECONDARY,
                           .icon = CUI_ICON_DOWNLOAD)) {
                start_sync_pull_all_thread(s);
            }

            int to_push = s->sync_drift.local_ahead_count + s->sync_drift.local_only_count;
            if (cui_button(ui, " 全量推送到远端 (Push All)",
                           .variant = (to_push > 0) ? CUI_BUTTON_PRIMARY : CUI_BUTTON_SECONDARY,
                           .icon = CUI_ICON_UPLOAD)) {
                start_sync_push_all_thread(s);
            }
        }

        cui_spacer(ui);

        if (cui_button(ui, " 切换/配置后端参数", .variant = CUI_BUTTON_GHOST, .icon = CUI_ICON_SETTINGS)) {
            s->current_page = PAGE_SETTINGS;
            s->settings_active_tab = 1; /* Switch directly to Remote Sync Tab */
        }
    }

    /* 4. Main Content Area / Drift Table */
    cui_box(ui, .width = CUI_FILL, .height = CUI_FILL, .radius = 8,
            .bg = cui_color_of(ui, CUI_COLOR_SURFACE),
            .pad = 16, .gap = 12,
            .border = 1, .border_color = cui_color_of(ui, CUI_COLOR_BORDER),
            .scroll_y = true) {

        if (s->sync_cfg.type == SYNC_BACKEND_NONE) {
            cui_column(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER, .pad = 40, .gap = 14) {
                cui_icon(ui, CUI_ICON_GLOBE, 48, CUI_COLOR_TEXT_MUTED);
                cui_heading(ui, "当前处于单机离线模式 (未启用远端同步)", 4);
                cui_text(ui, "若需在多台主机间无缝共享和同步技能，请前往设置页面启用 WebDAV、NFS 共享或 Git 远端后端。",
                         .size = 13, .color = CUI_COLOR_TEXT_SECONDARY);
                if (cui_button(ui, " 立即前往配置远端同步后端", .variant = CUI_BUTTON_PRIMARY, .icon = CUI_ICON_SETTINGS)) {
                    s->current_page = PAGE_SETTINGS;
                    s->settings_active_tab = 1;
                }
            }
        } else if (!s->sync_drift_loaded) {
            cui_column(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER, .pad = 40, .gap = 14) {
                cui_icon(ui, CUI_ICON_REFRESH_CW, 40, CUI_COLOR_ACCENT);
                cui_heading(ui, "尚未检测远端版本状态", 4);
                cui_text(ui, "点击下方按钮连接远端后端并探测版本差异。",
                         .size = 13, .color = CUI_COLOR_TEXT_SECONDARY);
                if (cui_button(ui, " 立即开始检查远端变动", .variant = CUI_BUTTON_PRIMARY, .icon = CUI_ICON_REFRESH_CW)) {
                    start_sync_check_thread(s);
                }
            }
        } else if (s->sync_drift.count == 0) {
            cui_column(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER, .pad = 40, .gap = 12) {
                cui_icon(ui, CUI_ICON_CHECK, 40, CUI_COLOR_SUCCESS);
                cui_heading(ui, "两端技能库已完全同步", 4);
                cui_text(ui, "本地主仓与远端共享库没有任何分化或缺失。",
                         .size = 13, .color = CUI_COLOR_TEXT_SECONDARY);
            }
        } else {
            /* Drift Table */
            cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER, .pad = 4) {
                cui_text(ui, "技能差异清单", .size = 13, .color = CUI_COLOR_TEXT_MUTED);
                cui_spacer(ui);
                char count_buf[64];
                snprintf(count_buf, sizeof(count_buf), "共对比 %d 个技能项", s->sync_drift.count);
                cui_text(ui, count_buf, .size = 12, .color = CUI_COLOR_TEXT_MUTED);
            }

            for (int i = 0; i < s->sync_drift.count; i++) {
                sync_drift_item_t *item = &s->sync_drift.items[i];

                cui_box(ui, .width = CUI_FILL, .radius = 6, .pad = 12,
                        .bg = cui_color_of(ui, CUI_COLOR_SURFACE_RAISED),
                        .border = 1, .border_color = cui_color_of(ui, CUI_COLOR_BORDER)) {
                    cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                        /* Skill Name & Fingerprints */
                        cui_row(ui, .gap = 10, .align = CUI_ALIGN_CENTER, .width = CUI_PX(280)) {
                            cui_icon(ui, CUI_ICON_FOLDER, 16, CUI_COLOR_ACCENT);
                            cui_column(ui, .gap = 2) {
                                cui_text(ui, item->skill_name, .size = 14, .color = CUI_COLOR_FOREGROUND);
                                char sub[64];
                                snprintf(sub, sizeof(sub), "指纹: %s",
                                         item->local_hash[0] ? item->local_hash : "(本地缺失)");
                                cui_text(ui, sub, .size = 11, .color = CUI_COLOR_TEXT_MUTED);
                            }
                        }

                        /* Status Badge */
                        cui_box(ui, .width = CUI_PX(180), .align = CUI_ALIGN_START) {
                            if (item->status == SYNC_DRIFT_IN_SYNC) {
                                cui_badge(ui, "两端完全一致", .tone = CUI_TONE_SUCCESS, .size = CUI_SIZE_SM);
                            } else if (item->status == SYNC_DRIFT_REMOTE_AHEAD) {
                                cui_badge(ui, "远端更新 (待拉取)", .tone = CUI_TONE_INFO, .size = CUI_SIZE_SM);
                            } else if (item->status == SYNC_DRIFT_LOCAL_AHEAD) {
                                cui_badge(ui, "本地更新 (待推送)", .tone = CUI_TONE_WARNING, .size = CUI_SIZE_SM);
                            } else if (item->status == SYNC_DRIFT_REMOTE_ONLY) {
                                cui_badge(ui, "远端独有 (待拉取)", .tone = CUI_TONE_VIOLET, .size = CUI_SIZE_SM);
                            } else if (item->status == SYNC_DRIFT_LOCAL_ONLY) {
                                cui_badge(ui, "本地独有 (待推送)", .tone = CUI_TONE_WARNING, .size = CUI_SIZE_SM);
                            } else {
                                cui_badge(ui, "版本冲突", .tone = CUI_TONE_DANGER, .size = CUI_SIZE_SM);
                            }
                        }

                        /* Description text */
                        cui_text(ui, item->status_desc, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);

                        cui_spacer(ui);

                        /* Action Buttons */
                        cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                            if (item->status == SYNC_DRIFT_REMOTE_AHEAD ||
                                item->status == SYNC_DRIFT_REMOTE_ONLY) {
                                if (cui_button(ui, " ⬇️ 拉取到本地主仓", .variant = CUI_BUTTON_PRIMARY, .size = CUI_SIZE_SM)) {
                                    start_sync_pull_skill_thread(s, item->skill_name);
                                }
                            } else if (item->status == SYNC_DRIFT_LOCAL_AHEAD ||
                                       item->status == SYNC_DRIFT_LOCAL_ONLY) {
                                if (cui_button(ui, " ⬆️ 推送到远端共享", .variant = CUI_BUTTON_PRIMARY, .size = CUI_SIZE_SM)) {
                                    start_sync_push_skill_thread(s, item->skill_name);
                                }
                            } else if (item->status == SYNC_DRIFT_IN_SYNC) {
                                cui_badge(ui, "无需操作", .tone = CUI_TONE_NEUTRAL, .size = CUI_SIZE_SM);
                            }
                        }
                    }
                }
            }
        }
    }
}
