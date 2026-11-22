#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cui/cui.h>
#include <cui/cui_app.h>
#include "app_state.h"
#include "db.h"
#include "view_common.h"
#include "view_tools.h"
#include "view_tool_skills.h"
#include "view_hub_matrix.h"
#include "view_hub_diff.h"
#include "view_master_hub.h"

/* -------------------- Main Frame Dispatcher -------------------- */

static void frame(cui_ctx *ui, void *user) {
    app_state_t *s = (app_state_t *)user;

    cui_column(ui, .width = CUI_FILL, .height = CUI_FILL, .pad = 20, .gap = 14,
               .bg = CUI_COLOR_BACKGROUND) {
        if (s->current_page == PAGE_TOOL_LIST) {
            render_tools_list_page(ui, s);
        } else if (s->current_page == PAGE_HUB_MATRIX) {
            render_hub_matrix_page(ui, s);
        } else if (s->current_page == PAGE_TOOL_SKILLS) {
            render_tool_skills_page(ui, s);
        }
    }

    /* Global Operation Progress Modal (single source of truth for the app) */
    render_op_progress_modal(ui, s);
}

int main(void) {
    static app_state_t s = {0};
    strncpy(s.db_path, "data/ai_space.db", sizeof(s.db_path) - 1);

    if (db_init(s.db_path, &s.db) != 0) {
        fprintf(stderr, "Failed to initialize database at %s\n", s.db_path);
        return 1;
    }

    /* Initial synchronization on application launch */
    do_scan_sync(&s);

    const char *start_page = getenv("CUI_START_PAGE");
    if (start_page && strcmp(start_page, "hub") == 0) {
        s.current_page = PAGE_HUB_MATRIX;
        s.hub_subview = HUB_SUBVIEW_MASTER_SKILLS;
        const char *subv = getenv("CUI_START_SUBVIEW");
        if (subv && strcmp(subv, "scan") == 0) {
            s.hub_subview = HUB_SUBVIEW_SCAN_MATRIX;
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

    cui_app *app = cui_app_create(&(cui_app_desc){
        .title = "AI-Space 本地工具与技能管理器",
        .width = 1260,
        .height = 780,
        .min_width = 960,
        .min_height = 620,
        .accent = CUI_ACCENT_BLUE,
    });

    if (!app) {
        db_close(s.db);
        if (s.records) free(s.records);
        return 1;
    }

    while (cui_app_frame(app)) {
        cui_ctx *ui = cui_app_ui(app);
        static cui_font cjk;
        static bool font_done = false;

        /* Load CJK fallback font for Chinese characters */
        if (!font_done) {
            cjk = cui_font_load_file(ui, "fonts/NotoSansSC-Regular.otf");
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
