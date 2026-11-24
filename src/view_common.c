#include "app_sync.h"
#include "app_hub_tasks.h"
#include "view_common.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

bool ci_contains(const char *haystack, const char *needle) {
    if (!haystack || !needle || needle[0] == '\0') return true;
    size_t nlen = strlen(needle);
    size_t hlen = strlen(haystack);
    if (nlen > hlen) return false;
    for (size_t i = 0; i <= hlen - nlen; i++) {
        size_t j = 0;
        while (j < nlen && tolower((unsigned char)haystack[i + j]) == tolower((unsigned char)needle[j])) {
            j++;
        }
        if (j == nlen) return true;
    }
    return false;
}

void format_size(size_t bytes, char *buf, size_t cap) {
    if (bytes < 1024) {
        snprintf(buf, cap, "%zu B", bytes);
    } else if (bytes < 1024 * 1024) {
        snprintf(buf, cap, "%.1f KB", (double)bytes / 1024.0);
    } else {
        snprintf(buf, cap, "%.2f MB", (double)bytes / (1024.0 * 1024.0));
    }
}

void app_set_toast(app_state_t *s, const char *msg) {
    if (!s || !msg) return;
    strncpy(s->toast_msg, msg, sizeof(s->toast_msg) - 1);
    s->toast_msg[sizeof(s->toast_msg) - 1] = '\0';
    s->toast_frames = 180; /* ~3 seconds at 60fps */
}

void apply_workspace_theme(cui_ctx *ui) {
    static const struct {
        cui_token token;
        unsigned light, dark;
    } palette[] = {
        { CUI_TOKEN_BACKGROUND,      0xeff3f9, 0x0d1626 },
        { CUI_TOKEN_SURFACE,         0xffffff, 0x152238 },
        { CUI_TOKEN_SURFACE_RAISED,  0xf8faff, 0x1b2c46 },
        { CUI_TOKEN_SURFACE_MUTED,   0xe8eff9, 0x111e31 },
        { CUI_TOKEN_FOREGROUND,      0x162841, 0xf2f6fd },
        { CUI_TOKEN_TEXT_SECONDARY,  0x3f5675, 0xb6c6de },
        { CUI_TOKEN_TEXT_MUTED,      0x566e8c, 0x8fa6c5 },
        { CUI_TOKEN_BORDER,          0xd3deee, 0x2d415f },
        { CUI_TOKEN_BORDER_SUBTLE,   0xe3ebf5, 0x243753 },
        { CUI_TOKEN_BORDER_STRONG,   0x93a9c5, 0x617998 },
        { CUI_TOKEN_CONTROL_TRACK,   0xdbe6f5, 0x3b5374 },
        { CUI_TOKEN_CONTROL_TRACK_HOVER, 0xcdddf2, 0x4d678a },
    };
    cui_theme *theme = cui_theme_get(ui);
    bool dark = cui_get_theme_mode(ui) == CUI_THEME_DARK;
    for (size_t i = 0; i < sizeof(palette) / sizeof(palette[0]); i++) {
        theme->colors[palette[i].token] = CUI_RGB(dark ? palette[i].dark : palette[i].light);
    }
    cui_color accent = theme->colors[CUI_TOKEN_ACCENT];
    theme->colors[CUI_TOKEN_HOVER] = cui_alpha(accent, dark ? 0.11f : 0.07f);
    theme->colors[CUI_TOKEN_ACTIVE] = cui_alpha(accent, dark ? 0.24f : 0.15f);
    theme->heading[2] = 24;
}

void render_workspace_heading(cui_ctx *ui, const char *icon, const char *title,
                              const char *subtitle) {
    cui_color accent = cui_color_of(ui, CUI_COLOR_ACCENT);
    cui_color surface = cui_color_of(ui, CUI_COLOR_SURFACE);
    bool dark = cui_get_theme_mode(ui) == CUI_THEME_DARK;
    cui_row(ui, .width = CUI_FILL, .gap = 12, .align = CUI_ALIGN_CENTER) {
        cui_box(ui, .width = CUI_PX(42), .height = CUI_PX(42), .radius = 12,
                .bg = cui_mix(surface, accent, dark ? 0.23f : 0.11f),
                .border = 1, .border_color = cui_mix(surface, accent, dark ? 0.42f : 0.23f),
                .dir = CUI_STACK, .align = CUI_ALIGN_CENTER, .justify = CUI_JUSTIFY_CENTER) {
            cui_icon(ui, icon, 21, CUI_COLOR_ACCENT_STRONG);
        }
        cui_column(ui, .width = CUI_FILL, .gap = 2) {
            cui_heading(ui, title, 3);
            if (subtitle && subtitle[0]) {
                cui_text(ui, subtitle, .width = CUI_FILL, .nowrap = true,
                         .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);
                cui_tooltip(ui, subtitle);
            }
        }
    }
}

void render_app_sidebar(cui_ctx *ui, app_state_t *s) {
    cui_column(ui, .width = CUI_PX(220), .height = CUI_FILL,
               .bg = CUI_COLOR_SURFACE_MUTED, .pad = 12, .gap = 2) {

        /* Navigation items */
        cui_scroll(ui, .id = "sidebar_nav", .height = CUI_FILL, .gap = 2) {
            bool is_dash = (s->current_page == PAGE_DASHBOARD);
            if (cui_nav_item(ui, "首页", .icon = CUI_ICON_LAYOUT_DASHBOARD, .selected = is_dash, .group = "pages")) {
                s->current_page = PAGE_DASHBOARD;
            }

            bool is_tools = (s->current_page == PAGE_TOOL_LIST || s->current_page == PAGE_TOOL_SKILLS);
            if (cui_nav_item(ui, "工具列表", .icon = ICON_TOOLS, .selected = is_tools, .group = "pages")) {
                s->current_page = PAGE_TOOL_LIST;
            }

            bool is_skills = (s->current_page == PAGE_ALL_SKILLS);
            if (cui_nav_item(ui, "技能列表", .icon = ICON_SKILLS, .selected = is_skills, .group = "pages")) {
                s->current_page = PAGE_ALL_SKILLS;
                s->hub_subview = HUB_SUBVIEW_SCAN_MATRIX;
                if (!s->hub_matrix_loaded && !s->hub_scan.is_scanning) {
                    start_hub_scan_thread(s);
                }
            }

            bool is_hub = (s->current_page == PAGE_HUB_MATRIX);
            if (cui_nav_item(ui, "主仓管理", .icon = ICON_MASTER_HUB, .selected = is_hub, .group = "pages")) {
                s->current_page = PAGE_HUB_MATRIX;
                s->hub_subview = HUB_SUBVIEW_MASTER_SKILLS;
                if (!s->hub_matrix_loaded && !s->hub_scan.is_scanning) {
                    start_hub_scan_thread(s);
                }
            }

            bool is_sync = (s->current_page == PAGE_SYNC_MANAGER);
            if (cui_nav_item(ui, "同步管理", .icon = ICON_SYNC, .selected = is_sync, .group = "pages")) {
                s->current_page = PAGE_SYNC_MANAGER;
                if (s->sync_cfg.type != SYNC_BACKEND_NONE && !s->sync_drift_loaded && !s->sync_is_busy) {
                    start_sync_check_thread(s);
                }
            }
        }
    }
}
