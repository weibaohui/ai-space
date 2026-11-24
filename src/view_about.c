#include "view_about.h"
#include "view_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "portability.h"

static void render_about_path(cui_ctx *ui, const char *label, const char *path) {
    cui_push_id(ui, label);
    cui_row(ui, .width = CUI_FILL, .gap = 8, .align = CUI_ALIGN_CENTER) {
        cui_column(ui, .width = CUI_FILL, .gap = 3) {
            cui_text(ui, label, .size = 12, .font = CUI_FONT_MEDIUM);
            cui_text(ui, path[0] ? path : "—", .width = CUI_FILL, .nowrap = true,
                     .font = CUI_FONT_MONO, .color = CUI_COLOR_TEXT_SECONDARY, .size = 12);
        }
        if (cui_icon_button(ui, CUI_ICON_COPY, .size = CUI_SIZE_SM, .tooltip = "复制路径", .disabled = !path[0])) {
            cui_set_clipboard_text(ui, path);
            cui_toast(ui, "路径已复制", .tone = CUI_TONE_SUCCESS);
        }
    }
    cui_pop_id(ui);
}

static void render_about_value(cui_ctx *ui, const char *label, const char *value) {
    cui_row(ui, .width = CUI_FILL, .gap = 8, .align = CUI_ALIGN_CENTER) {
        cui_text(ui, label, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);
        cui_text(ui, value, .width = CUI_FILL, .nowrap = true, .align = CUI_TEXT_END,
                 .size = 12, .font = CUI_FONT_MEDIUM);
    }
}

void render_about_page(cui_ctx *ui, app_state_t *s) {
    cui_color accent = cui_color_of(ui, CUI_COLOR_ACCENT);
    cui_color surface = cui_color_of(ui, CUI_COLOR_SURFACE);
    bool dark = cui_get_theme_mode(ui) == CUI_THEME_DARK;
    char tools[24], skills[24];
    snprintf(tools, sizeof(tools), "%d 个", s->stats.total_tools);
    snprintf(skills, sizeof(skills), "%d 项", s->hub_matrix.hub_managed_skills);

    cui_column(ui, .width = CUI_FILL, .gap = 14) {
        cui_box(ui, .width = CUI_FILL, .radius = 16, .pad = 22,
                .bg = cui_mix(surface, accent, dark ? 0.17f : 0.085f),
                .border = 1, .border_color = cui_mix(cui_color_of(ui, CUI_COLOR_BORDER), accent, 0.48f)) {
            cui_row(ui, .width = CUI_FILL, .gap = 16, .align = CUI_ALIGN_CENTER) {
                cui_box(ui, .width = CUI_PX(58), .height = CUI_PX(58), .radius = 16,
                        .bg = accent, .dir = CUI_STACK, .align = CUI_ALIGN_CENTER,
                        .justify = CUI_JUSTIFY_CENTER) {
                    cui_icon(ui, CUI_ICON_BOX, 31, CUI_COLOR_ACCENT_FOREGROUND);
                }
                cui_column(ui, .width = CUI_FILL, .gap = 4) {
                    cui_row(ui, .width = CUI_FILL, .gap = 10, .align = CUI_ALIGN_CENTER) {
                        cui_heading(ui, "AI-Space", 2);
                        cui_badge(ui, g_ai_space_version, .tone = CUI_TONE_ACCENT, .size = CUI_SIZE_SM);
                    }
                    cui_text(ui, "在本机管理各 AI 工具的技能，并通过主仓统一分发和同步。",
                             .width = CUI_FILL, .size = 13, .color = CUI_COLOR_TEXT_SECONDARY);
                }
            }
        }

        cui_row(ui, .width = CUI_FILL, .gap = 14, .align = CUI_ALIGN_STRETCH) {
            cui_card(ui, .width = CUI_GROW(6), .pad = 16, .gap = 10) {
                cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                    cui_icon(ui, CUI_ICON_DATABASE, 17, CUI_COLOR_ACCENT_STRONG);
                    cui_heading(ui, "本机数据", 4);
                }
                render_about_path(ui, "主仓目录", s->setting_hub_dir);
                cui_separator(ui);
                render_about_path(ui, "快照备份目录", s->setting_backup_dir);
                cui_separator(ui);
                render_about_path(ui, "数据库", s->db_path);
            }

            cui_card(ui, .width = CUI_GROW(4), .pad = 16, .gap = 10) {
                cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                    cui_icon(ui, CUI_ICON_ACTIVITY, 17, CUI_COLOR_ACCENT_STRONG);
                    cui_heading(ui, "运行信息", 4);
                }
                render_about_value(ui, "已识别工具", tools);
                render_about_value(ui, "主仓技能", s->hub_matrix_loaded ? skills : "尚未扫描");
                render_about_value(ui, "同步后端", sync_backend_display_name(s->sync_cfg.type));
                cui_separator(ui);
                render_about_value(ui, "核心组件", "C99 · SQLite · CUI");
            }
        }

        static const struct {
            const char *title;
            const char *description;
            const char *icon;
        } principles[] = {
            { "独立的本地副本", "分发时把技能复制到各工具的本地目录。", CUI_ICON_COPY },
            { "覆盖前备份", "启用快照后，覆盖前会保存原有文件。", CUI_ICON_HISTORY },
            { "跨设备同步", "通过 Git、WebDAV 或 NFS 共享主仓版本。", ICON_SYNC },
        };

        cui_card(ui, .width = CUI_FILL, .pad = 16, .gap = 12) {
            cui_heading(ui, "设计原则", 4);
            cui_row(ui, .width = CUI_FILL, .gap = 14, .align = CUI_ALIGN_START) {
                for (size_t i = 0; i < sizeof principles / sizeof principles[0]; ++i) {
                    cui_column(ui, .width = CUI_GROW(1), .gap = 5) {
                        cui_row(ui, .gap = 7, .align = CUI_ALIGN_CENTER) {
                            cui_icon(ui, principles[i].icon, 15, CUI_COLOR_ACCENT_STRONG);
                            cui_label(ui, principles[i].title);
                        }
                        cui_text(ui, principles[i].description, .width = CUI_FILL, .size = 12,
                                 .color = CUI_COLOR_TEXT_SECONDARY);
                    }
                }
            }
        }
    }
}
