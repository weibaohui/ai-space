#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cui/cui.h>
#include <cui/cui_app.h>
#include "portability.h"
#include "app_state.h"
#include "db.h"
#include "view_common.h"
#include "view_tools.h"
#include "view_tool_skills.h"
#include "view_hub_matrix.h"
#include "view_hub_diff.h"
#include "view_master_hub.h"
#include "view_settings.h"
#include "view_sync.h"
#include "view_dashboard.h"

/* 全局发布的统一版本变量 */
const char *const g_ai_space_version = AI_SPACE_VERSION;

/* -------------------- Main Frame Dispatcher -------------------- */

static void frame(cui_ctx *ui, void *user) {
    app_state_t *s = (app_state_t *)user;

    /* Global keyboard shortcut: Ctrl+J (or Cmd+J) toggles Dark / Light theme */
    if (!cui_wants_keyboard(ui)) {
        if (cui_shortcut(ui, CUI_MOD_PRIMARY, CUI_KEY_J)) {
            s->dark_theme = !s->dark_theme;
            cui_set_theme(ui, s->dark_theme ? CUI_THEME_DARK : CUI_THEME_LIGHT);
            app_save_settings(s);
        }
    }

    cui_column(ui, .width = CUI_FILL, .height = CUI_FILL, .gap = 0, .bg = CUI_COLOR_BACKGROUND) {
        /* Global Top Status Bar */
        render_app_top_bar(ui, s);
        cui_separator(ui);

        /* Main Workspace Row */
        cui_row(ui, .width = CUI_FILL, .height = CUI_FILL, .align = CUI_ALIGN_STRETCH) {
            render_app_sidebar(ui, s);
            cui_separator(ui);

            cui_scroll(ui, .id = "main_content_scroll", .width = CUI_FILL, .height = CUI_FILL) {
                cui_column(ui, .width = CUI_FILL, .pad = (s->current_page == PAGE_DASHBOARD) ? 14 : 20,
                           .gap = (s->current_page == PAGE_DASHBOARD) ? 12 : 14) {
                    if (s->current_page == PAGE_DASHBOARD) {
                        render_dashboard_page(ui, s);
                    } else if (s->current_page == PAGE_TOOL_LIST) {
                        render_tools_list_page(ui, s);
                    } else if (s->current_page == PAGE_ALL_SKILLS) {
                        render_all_skills_page(ui, s);
                    } else if (s->current_page == PAGE_HUB_MATRIX) {
                        render_hub_matrix_page(ui, s);
                    } else if (s->current_page == PAGE_TOOL_SKILLS) {
                        render_tool_skills_page(ui, s);
                    } else if (s->current_page == PAGE_SETTINGS) {
                        render_settings_page(ui, s);
                    } else if (s->current_page == PAGE_SYNC_MANAGER) {
                        render_sync_manager_page(ui, s);
                    } else if (s->current_page == PAGE_ABOUT) {
                        render_about_page(ui, s);
                    }
                }
            }
        }
    }

    /* Global Operation Progress Modal (single source of truth for the app) */
    render_op_progress_modal(ui, s);

    /* Global Skill Distribution Modal (分发技能到本地各工具) */
    render_distribute_skill_modal(ui, s);

    /* Global Uninstall Confirmation Modals */
    render_uninstall_all_modal(ui, s);
    render_uninstall_tool_modal(ui, s);
}

int main(int argc, char **argv) {
    static app_state_t s = {0};

    /* data/ beside the working directory when writable (dev layout); otherwise
     * fall back to ~/.ai-space (e.g. Finder-launched .app has CWD "/") */
    if (AI_ACCESS("data", W_OK) == 0 || AI_MKDIR("data") == 0) {
        strncpy(s.db_path, "data/ai_space.db", sizeof(s.db_path) - 1);
    } else {
        snprintf(s.db_path, sizeof(s.db_path), "%s/.ai-space/ai_space.db", ai_home_dir());
    }

    const char *custom_hub = getenv("AI_SPACE_HUB_DIR");
    const char *custom_backup = getenv("AI_SPACE_BACKUP_DIR");
    const char *custom_title = getenv("AI_SPACE_TITLE");
    const char *custom_db = getenv("AI_SPACE_DB");

    if (custom_db && custom_db[0] != '\0') {
        strncpy(s.db_path, custom_db, sizeof(s.db_path) - 1);
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--db") == 0 && i + 1 < argc) {
            strncpy(s.db_path, argv[++i], sizeof(s.db_path) - 1);
        } else if (strcmp(argv[i], "--hub") == 0 && i + 1 < argc) {
            custom_hub = argv[++i];
        } else if (strcmp(argv[i], "--backup") == 0 && i + 1 < argc) {
            custom_backup = argv[++i];
        } else if (strcmp(argv[i], "--title") == 0 && i + 1 < argc) {
            custom_title = argv[++i];
        }
    }

    if (db_init(s.db_path, &s.db) != 0) {
        fprintf(stderr, "Failed to initialize database at %s\n", s.db_path);
        return 1;
    }

    /* Load user settings from SQLite */
    app_init_settings(&s);

    /* If CLI or env specified custom hub / backup dirs, override database settings */
    if (custom_hub && custom_hub[0] != '\0') {
        strncpy(s.setting_hub_dir, custom_hub, sizeof(s.setting_hub_dir) - 1);
        db_set_setting(s.db, "master_hub_dir", s.setting_hub_dir);
    }
    if (custom_backup && custom_backup[0] != '\0') {
        strncpy(s.setting_backup_dir, custom_backup, sizeof(s.setting_backup_dir) - 1);
        db_set_setting(s.db, "backup_dir", s.setting_backup_dir);
    }
    hub_set_custom_dirs(s.setting_hub_dir, s.setting_backup_dir);

    /* Initial synchronization on application launch */
    if (s.setting_auto_scan) {
        do_scan_sync(&s);
    } else {
        reload_db_data(&s);
    }

    /* Apply default view setting */
    if (s.setting_default_view == 1) {
        s.current_page = PAGE_ALL_SKILLS;
    } else if (s.setting_default_view == 2) {
        s.current_page = PAGE_HUB_MATRIX;
        s.hub_subview = HUB_SUBVIEW_MASTER_SKILLS;
    } else if (s.setting_default_view == 3) {
        s.current_page = PAGE_SYNC_MANAGER;
    }

    const char *start_page = getenv("CUI_START_PAGE");
    if (start_page && strcmp(start_page, "skills") == 0) {
        s.current_page = PAGE_ALL_SKILLS;
        if (getenv("CUI_SYNC_SCAN")) {
            hub_scan_matrix(s.db, &s.hub_matrix);
            s.hub_matrix_loaded = true;
        } else {
            start_hub_scan_thread(&s);
        }
    } else if (start_page && strcmp(start_page, "sync") == 0) {
        s.current_page = PAGE_SYNC_MANAGER;
        if (getenv("CUI_SYNC_CHECK")) {
            if (s.sync_backend && s.sync_backend->ops->check_drift) {
                s.sync_backend->ops->check_drift(s.sync_backend, s.setting_hub_dir, &s.sync_drift, NULL, 0);
                s.sync_drift_loaded = true;
            }
        }
    } else if (start_page && strcmp(start_page, "settings") == 0) {
        s.current_page = PAGE_SETTINGS;
        const char *start_tab = getenv("CUI_START_TAB");
        if (start_tab) {
            s.settings_active_tab = atoi(start_tab);
        }
    } else if (start_page && strcmp(start_page, "hub") == 0) {
        const char *subv = getenv("CUI_START_SUBVIEW");
        if (subv && strcmp(subv, "scan") == 0) {
            s.current_page = PAGE_ALL_SKILLS;
        } else {
            s.current_page = PAGE_HUB_MATRIX;
            s.hub_subview = HUB_SUBVIEW_MASTER_SKILLS;
        }

        const char *start_diff = getenv("CUI_START_DIFF");
        if (start_diff && start_diff[0] != '\0') {
            /* Synchronous scan for direct diff screenshot */
            hub_scan_matrix(s.db, &s.hub_matrix);
            s.hub_matrix_loaded = true;
            open_hub_diff_view(&s, start_diff);
            const char *start_file = getenv("CUI_START_DIFF_FILE");
            if (start_file && start_file[0] != '\0') {
                strncpy(s.diff_file_rel, start_file, sizeof(s.diff_file_rel) - 1);
                load_hub_diff(&s, start_diff, s.selected_hub_tool_idx, start_file);
            }
        } else if (getenv("CUI_SYNC_SCAN")) {
            hub_scan_matrix(s.db, &s.hub_matrix);
            s.hub_matrix_loaded = true;
            const char *start_exp = getenv("CUI_START_EXPLORER");
            if (start_exp && start_exp[0] != '\0') {
                open_master_skill_explorer(&s, start_exp);
            }
            const char *start_dist = getenv("CUI_START_DISTRIBUTE");
            if (start_dist && start_dist[0] != '\0') {
                s.show_distribute_modal = true;
                strncpy(s.distribute_skill_name, start_dist, sizeof(s.distribute_skill_name) - 1);
            }
        } else {
            start_hub_scan_thread(&s);
        }
    }

    if (getenv("CUI_START_ABOUT")) {
        s.current_page = PAGE_ABOUT;
    }

    if (s.current_page == PAGE_DASHBOARD) {
        if (getenv("CUI_SYNC_SCAN")) {
            hub_scan_matrix(s.db, &s.hub_matrix);
            s.hub_matrix_loaded = true;
        } else {
            start_hub_scan_thread(&s);
        }
    }

    const char *start_tool = getenv("CUI_START_TOOL");
    if (start_tool && start_tool[0] != '\0') {
        for (int i = 0; i < s.record_count; i++) {
            if (strcmp(s.records[i].name, start_tool) == 0) {
                open_tool_skills_page(&s, s.records[i].name, s.records[i].path);
                const char *start_sk = getenv("CUI_START_SKILL");
                if (start_sk && start_sk[0] != '\0') {
                    for (int j = 0; j < s.skill_count; j++) {
                        if (strcmp(s.skill_items[j].name, start_sk) == 0) {
                            select_active_skill(&s, s.skill_items[j].name, s.skill_items[j].path);
                            break;
                        }
                    }
                }
                break;
            }
        }
    }

    cui_accent accent = (cui_accent)s.accent_color;
    if (custom_title && (strstr(custom_title, "B") || strstr(custom_title, "node2") || strstr(custom_title, "节点 B"))) {
        accent = CUI_ACCENT_ORANGE;
    }

    cui_app *app = cui_app_create(&(cui_app_desc){
        .title = custom_title ? custom_title : "AI-Space 本地工具与技能管理器",
        .app_id = "com.weibaohui.ai-space",
        .width = 1260,
        .height = 780,
        .min_width = 960,
        .min_height = 620,
        .theme = s.dark_theme ? CUI_THEME_DARK : CUI_THEME_LIGHT,
        .accent = accent,
    });

    if (!app) {
        db_close(s.db);
        if (s.records) free(s.records);
        return 1;
    }

    /* Resolve the CJK fallback font: working directory first (dev layout), then
     * beside the executable, then .app-bundle Resources/, then ~/.ai-space */
    char font_file[1024] = "";
    if (AI_ACCESS("fonts/NotoSansSC-Regular.otf", R_OK) == 0) {
        strncpy(font_file, "fonts/NotoSansSC-Regular.otf", sizeof(font_file) - 1);
    } else {
        char exe_dir[1024];
        if (ai_exe_dir(exe_dir, sizeof(exe_dir)) == 0) {
            snprintf(font_file, sizeof(font_file), "%s/fonts/NotoSansSC-Regular.otf", exe_dir);
            if (AI_ACCESS(font_file, R_OK) != 0) {
                snprintf(font_file, sizeof(font_file),
                         "%s/../Resources/fonts/NotoSansSC-Regular.otf", exe_dir);
            }
        }
        if (AI_ACCESS(font_file, R_OK) != 0) {
            snprintf(font_file, sizeof(font_file),
                     "%s/.ai-space/fonts/NotoSansSC-Regular.otf", ai_home_dir());
        }
    }

    while (cui_app_frame(app)) {
        cui_ctx *ui = cui_app_ui(app);
        static cui_font cjk;
        static bool font_done = false;

        /* Load CJK fallback font for Chinese characters */
        if (!font_done) {
            cjk = cui_font_load_file(ui, font_file);
            if (cjk > 0) {
                cui_font_add_fallback(ui, CUI_FONT_BODY, cjk);
                cui_font_add_fallback(ui, CUI_FONT_MEDIUM, cjk);
                cui_font_add_fallback(ui, CUI_FONT_DISPLAY, cjk);
                cui_font_add_fallback(ui, CUI_FONT_MONO, cjk);
            }
            font_done = true;
        }

        frame(ui, &s);
    }

    cui_app_destroy(app);

    if (s.records) {
        free(s.records);
        s.records = NULL;
    }
    if (s.skill_items) {
        free(s.skill_items);
        s.skill_items = NULL;
    }
    if (s.skill_file_tree) {
        free_skill_file_tree(s.skill_file_tree);
        free(s.skill_file_tree);
        s.skill_file_tree = NULL;
    }
    if (s.master_skill_file_tree) {
        free_skill_file_tree(s.master_skill_file_tree);
        free(s.master_skill_file_tree);
        s.master_skill_file_tree = NULL;
    }
    if (s.hub_diff_file_tree) {
        free_skill_file_tree(s.hub_diff_file_tree);
        free(s.hub_diff_file_tree);
        s.hub_diff_file_tree = NULL;
    }
    if (s.selected_file_content) {
        free(s.selected_file_content);
        s.selected_file_content = NULL;
    }
    hub_free_matrix(&s.hub_matrix);
    free_skill_diff_manifest(&s.diff_manifest);
    clear_diff_texts(&s);
    db_close(s.db);

    return 0;
}
