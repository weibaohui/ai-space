#include "view_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include "db.h"

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

void clear_diff_texts(app_state_t *s) {
    if (!s) return;
    if (s->diff_old_text) {
        free(s->diff_old_text);
        s->diff_old_text = NULL;
    }
    if (s->diff_new_text) {
        free(s->diff_new_text);
        s->diff_new_text = NULL;
    }
}


void load_hub_diff(app_state_t *s, const char *skill_name, int tool_idx, const char *rel_file) {
    if (!s || !skill_name) return;

    clear_diff_texts(s);
    strncpy(s->selected_hub_skill, skill_name, sizeof(s->selected_hub_skill) - 1);
    s->selected_hub_tool_idx = tool_idx;

    if (rel_file && rel_file[0] != '\0') {
        strncpy(s->diff_file_rel, rel_file, sizeof(s->diff_file_rel) - 1);
    } else {
        strncpy(s->diff_file_rel, "SKILL.md", sizeof(s->diff_file_rel) - 1);
    }
    s->diff_current_change = 0;

    hub_skill_entry_t *entry = NULL;
    for (int i = 0; i < s->hub_matrix.skill_count; i++) {
        if (strcmp(s->hub_matrix.skills[i].skill_name, skill_name) == 0) {
            entry = &s->hub_matrix.skills[i];
            break;
        }
    }
    if (!entry) return;

    /* 1. Master Hub text */
    if (entry->in_hub) {
        char m_path[1200];
        snprintf(m_path, sizeof(m_path), "%s/%s", entry->hub_path, s->diff_file_rel);
        size_t m_sz = 0;
        int m_lines = 0;
        bool m_bin = false;
        s->diff_old_text = read_file_text_content(m_path, 1024 * 1024, &m_sz, &m_lines, &m_bin);
    }
    if (!s->diff_old_text) {
        s->diff_old_text = strdup("");
    }

    /* 2. Tool copy text */
    if (tool_idx >= 0 && tool_idx < entry->tool_count && entry->tool_copies[tool_idx].exists) {
        char t_path[1200];
        snprintf(t_path, sizeof(t_path), "%s/%s", entry->tool_copies[tool_idx].skill_path, s->diff_file_rel);
        size_t t_sz = 0;
        int t_lines = 0;
        bool t_bin = false;
        s->diff_new_text = read_file_text_content(t_path, 1024 * 1024, &t_sz, &t_lines, &t_bin);
    }
    if (!s->diff_new_text) {
        s->diff_new_text = strdup("");
    }
}

void open_hub_diff_view(app_state_t *s, const char *skill_name) {
    if (!s) return;
    if (!s->hub_matrix_loaded && !s->hub_scan.is_scanning) {
        start_hub_scan_thread(s);
    }

    hub_skill_entry_t *entry = NULL;
    for (int i = 0; i < s->hub_matrix.skill_count; i++) {
        if (strcmp(s->hub_matrix.skills[i].skill_name, skill_name) == 0) {
            entry = &s->hub_matrix.skills[i];
            break;
        }
    }
    if (!entry) return;

    /* Find first drifted tool copy, or first existing copy */
    int chosen_idx = -1;
    for (int t = 0; t < entry->tool_count; t++) {
        if (entry->tool_copies[t].exists) {
            if (entry->tool_copies[t].status == HUB_STATUS_LOCAL_MODIFIED ||
                entry->tool_copies[t].status == HUB_STATUS_OUTDATED) {
                chosen_idx = t;
                break;
            }
            if (chosen_idx == -1) chosen_idx = t;
        }
    }
    if (chosen_idx == -1 && entry->tool_count > 0) chosen_idx = 0;

    s->hub_subview = 1;
    load_hub_diff(s, skill_name, chosen_idx, "SKILL.md");
}

/* Background scanning thread worker */
static void scan_progress_callback(float percent, const char *status_msg, void *user_data) {
    app_state_t *s = (app_state_t *)user_data;
    if (!s) return;
    s->hub_scan.progress = percent;
    if (status_msg) {
        strncpy(s->hub_scan.status_text, status_msg, sizeof(s->hub_scan.status_text) - 1);
        s->hub_scan.status_text[sizeof(s->hub_scan.status_text) - 1] = '\0';
    }
}

static void *hub_scan_worker_thread(void *arg) {
    app_state_t *s = (app_state_t *)arg;
    hub_matrix_t new_matrix;
    hub_scan_matrix_ex(s->db, &new_matrix, scan_progress_callback, s);

    /* Swap matrix cleanly */
    hub_free_matrix(&s->hub_matrix);
    s->hub_matrix = new_matrix;

    s->hub_matrix_loaded = true;
    s->hub_scan.is_scanning = false;
    s->hub_scan.thread_running = false;
    return NULL;
}

void start_hub_scan_thread(app_state_t *s) {
    if (!s || s->hub_scan.is_scanning) return;

    s->hub_scan.is_scanning = true;
    s->hub_scan.progress = 0.05f;
    strncpy(s->hub_scan.status_text, "准备比对全局技能分化关系...", sizeof(s->hub_scan.status_text) - 1);
    s->hub_scan.thread_running = true;

    pthread_create(&s->hub_scan.thread, NULL, hub_scan_worker_thread, s);
    pthread_detach(s->hub_scan.thread);
}

/* Background adopt all worker thread */
static void adopt_progress_callback(float percent, const char *status_msg, void *user_data) {
    app_state_t *s = (app_state_t *)user_data;
    if (!s) return;
    s->op_progress.percent = percent;
    if (status_msg) {
        strncpy(s->op_progress.message, status_msg, sizeof(s->op_progress.message) - 1);
        s->op_progress.message[sizeof(s->op_progress.message) - 1] = '\0';
    }
}

static void *adopt_all_worker_thread(void *arg) {
    app_state_t *s = (app_state_t *)arg;
    int adopted = 0;
    hub_adopt_all_to_master_ex(&s->hub_matrix, &adopted, adopt_progress_callback, s);

    /* Rescan matrix after adoption */
    hub_matrix_t new_matrix;
    hub_scan_matrix_ex(s->db, &new_matrix, NULL, NULL);
    hub_free_matrix(&s->hub_matrix);
    s->hub_matrix = new_matrix;

    s->op_progress.active = false;
    s->op_progress.thread_running = false;

    char toast[256];
    snprintf(toast, sizeof(toast), "全量纳管完成！成功将 %d 个技能收录至主版本库", adopted);
    app_set_toast(s, toast);
    return NULL;
}

void start_adopt_all_thread(app_state_t *s) {
    if (!s || s->op_progress.active) return;

    s->op_progress.active = true;
    s->op_progress.percent = 0.05f;
    strncpy(s->op_progress.title, "正在全量纳管本地技能至主版本库...", sizeof(s->op_progress.title) - 1);
    strncpy(s->op_progress.message, "开始物理复制并建立主版本...", sizeof(s->op_progress.message) - 1);
    s->op_progress.thread_running = true;

    pthread_create(&s->op_progress.thread, NULL, adopt_all_worker_thread, s);
    pthread_detach(s->op_progress.thread);
}

/* Generic single-skill operation worker thread */
typedef struct {
    app_state_t *s;
    char tool_skills_path[512];
    char skill_name[128];
    char tool_name[128];
    int tool_idx;
    int op_type; /* 0: copy install, 1: overwrite tool, 2: promote tool, 3: broadcast */
} hub_op_thread_arg_t;

static void *hub_op_worker_thread(void *data) {
    hub_op_thread_arg_t *arg = (hub_op_thread_arg_t *)data;
    app_state_t *s = arg->s;

    if (arg->op_type == 0) {
        /* Copy Install */
        s->op_progress.percent = 0.15f;
        snprintf(s->op_progress.message, sizeof(s->op_progress.message), "正在创建目标目录与备份快照...");
        usleep(80000);

        s->op_progress.percent = 0.45f;
        snprintf(s->op_progress.message, sizeof(s->op_progress.message), "正在执行纯物理文件拷贝...");
        hub_sync_master_to_tool(arg->tool_skills_path, arg->skill_name, arg->tool_name);
        usleep(80000);

        s->op_progress.percent = 0.75f;
        snprintf(s->op_progress.message, sizeof(s->op_progress.message), "正在重新校验内容指纹与分化矩阵...");
        hub_matrix_t new_matrix;
        hub_scan_matrix(s->db, &new_matrix);
        hub_free_matrix(&s->hub_matrix);
        s->hub_matrix = new_matrix;

        s->op_progress.percent = 1.0f;
        snprintf(s->op_progress.message, sizeof(s->op_progress.message), "物理复制安装成功！");
        usleep(150000);

        load_hub_diff(s, arg->skill_name, arg->tool_idx, s->diff_file_rel);
        char toast[256];
        snprintf(toast, sizeof(toast), "已将主版本物理复制安装至 [%s]", arg->tool_name);
        app_set_toast(s, toast);

    } else if (arg->op_type == 1) {
        /* Overwrite tool with master */
        s->op_progress.percent = 0.15f;
        snprintf(s->op_progress.message, sizeof(s->op_progress.message), "正在生成 ~/.ai-space/backups/ 安全备份快照...");
        usleep(80000);

        s->op_progress.percent = 0.45f;
        snprintf(s->op_progress.message, sizeof(s->op_progress.message), "正在下发主版本覆盖工具副本...");
        hub_sync_master_to_tool(arg->tool_skills_path, arg->skill_name, arg->tool_name);
        usleep(80000);

        s->op_progress.percent = 0.75f;
        snprintf(s->op_progress.message, sizeof(s->op_progress.message), "正在重新比对内容指纹与矩阵状态...");
        hub_matrix_t new_matrix;
        hub_scan_matrix(s->db, &new_matrix);
        hub_free_matrix(&s->hub_matrix);
        s->hub_matrix = new_matrix;

        s->op_progress.percent = 1.0f;
        snprintf(s->op_progress.message, sizeof(s->op_progress.message), "覆盖同步成功！");
        usleep(150000);

        load_hub_diff(s, arg->skill_name, arg->tool_idx, s->diff_file_rel);
        char toast[256];
        snprintf(toast, sizeof(toast), "已同步主版本到 [%s]（原副本已安全备份）", arg->tool_name);
        app_set_toast(s, toast);

    } else if (arg->op_type == 2) {
        /* Promote tool copy to master */
        s->op_progress.percent = 0.15f;
        snprintf(s->op_progress.message, sizeof(s->op_progress.message), "正在为旧主版本生成安全备份快照...");
        usleep(80000);

        s->op_progress.percent = 0.45f;
        snprintf(s->op_progress.message, sizeof(s->op_progress.message), "正在采纳 [%s] 副本为主版本基准...", arg->tool_name);
        hub_promote_to_master(arg->tool_skills_path, arg->skill_name);
        usleep(80000);

        s->op_progress.percent = 0.75f;
        snprintf(s->op_progress.message, sizeof(s->op_progress.message), "正在重新计算指纹并更新矩阵关系...");
        hub_matrix_t new_matrix;
        hub_scan_matrix(s->db, &new_matrix);
        hub_free_matrix(&s->hub_matrix);
        s->hub_matrix = new_matrix;

        s->op_progress.percent = 1.0f;
        snprintf(s->op_progress.message, sizeof(s->op_progress.message), "采纳成功！");
        usleep(150000);

        load_hub_diff(s, arg->skill_name, arg->tool_idx, s->diff_file_rel);
        char toast[256];
        snprintf(toast, sizeof(toast), "已将 [%s] 副本采纳为技能主版本", arg->tool_name);
        app_set_toast(s, toast);

    } else if (arg->op_type == 3) {
        /* Broadcast to all tools */
        s->op_progress.percent = 0.15f;
        snprintf(s->op_progress.message, sizeof(s->op_progress.message), "正在为受影响的各工具副本建立安全备份...");
        usleep(80000);

        s->op_progress.percent = 0.45f;
        snprintf(s->op_progress.message, sizeof(s->op_progress.message), "正在广播主版本并覆盖各工具副本...");
        int synced = 0;
        hub_broadcast_skill(&s->hub_matrix, arg->skill_name, &synced);
        usleep(80000);

        s->op_progress.percent = 0.75f;
        snprintf(s->op_progress.message, sizeof(s->op_progress.message), "正在更新全景关系矩阵...");
        hub_matrix_t new_matrix;
        hub_scan_matrix(s->db, &new_matrix);
        hub_free_matrix(&s->hub_matrix);
        s->hub_matrix = new_matrix;

        s->op_progress.percent = 1.0f;
        snprintf(s->op_progress.message, sizeof(s->op_progress.message), "广播覆盖成功！");
        usleep(150000);

        if (s->hub_subview == 1) {
            load_hub_diff(s, arg->skill_name, arg->tool_idx, s->diff_file_rel);
        }
        char toast[256];
        snprintf(toast, sizeof(toast), "已全量广播主版本覆盖至 %d 个工具副本", synced);
        app_set_toast(s, toast);
    }

    s->op_progress.active = false;
    s->op_progress.thread_running = false;
    free(arg);
    return NULL;
}

void start_copy_install_thread(app_state_t *s, const char *tool_skills_path,
                               const char *skill_name, const char *tool_name, int tool_idx) {
    if (!s || s->op_progress.active) return;

    hub_op_thread_arg_t *arg = (hub_op_thread_arg_t *)malloc(sizeof(hub_op_thread_arg_t));
    if (!arg) return;
    arg->s = s;
    strncpy(arg->tool_skills_path, tool_skills_path ? tool_skills_path : "", sizeof(arg->tool_skills_path) - 1);
    strncpy(arg->skill_name, skill_name ? skill_name : "", sizeof(arg->skill_name) - 1);
    strncpy(arg->tool_name, tool_name ? tool_name : "", sizeof(arg->tool_name) - 1);
    arg->tool_idx = tool_idx;
    arg->op_type = 0;

    s->op_progress.active = true;
    s->op_progress.percent = 0.05f;
    snprintf(s->op_progress.title, sizeof(s->op_progress.title), "正在安装技能 [%s] 至 [%s]...", skill_name, tool_name);
    strncpy(s->op_progress.message, "准备物理复制并初始化安全快照...", sizeof(s->op_progress.message) - 1);
    s->op_progress.thread_running = true;

    pthread_create(&s->op_progress.thread, NULL, hub_op_worker_thread, arg);
    pthread_detach(s->op_progress.thread);
}

void start_sync_master_to_tool_thread(app_state_t *s, const char *tool_skills_path,
                                     const char *skill_name, const char *tool_name, int tool_idx) {
    if (!s || s->op_progress.active) return;

    hub_op_thread_arg_t *arg = (hub_op_thread_arg_t *)malloc(sizeof(hub_op_thread_arg_t));
    if (!arg) return;
    arg->s = s;
    strncpy(arg->tool_skills_path, tool_skills_path ? tool_skills_path : "", sizeof(arg->tool_skills_path) - 1);
    strncpy(arg->skill_name, skill_name ? skill_name : "", sizeof(arg->skill_name) - 1);
    strncpy(arg->tool_name, tool_name ? tool_name : "", sizeof(arg->tool_name) - 1);
    arg->tool_idx = tool_idx;
    arg->op_type = 1;

    s->op_progress.active = true;
    s->op_progress.percent = 0.05f;
    snprintf(s->op_progress.title, sizeof(s->op_progress.title), "正在同步覆盖 [%s] 的技能副本...", tool_name);
    strncpy(s->op_progress.message, "准备下发主版本并生成自动快照备份...", sizeof(s->op_progress.message) - 1);
    s->op_progress.thread_running = true;

    pthread_create(&s->op_progress.thread, NULL, hub_op_worker_thread, arg);
    pthread_detach(s->op_progress.thread);
}

void start_promote_to_master_thread(app_state_t *s, const char *tool_skills_path,
                                    const char *skill_name, const char *tool_name, int tool_idx) {
    if (!s || s->op_progress.active) return;

    hub_op_thread_arg_t *arg = (hub_op_thread_arg_t *)malloc(sizeof(hub_op_thread_arg_t));
    if (!arg) return;
    arg->s = s;
    strncpy(arg->tool_skills_path, tool_skills_path ? tool_skills_path : "", sizeof(arg->tool_skills_path) - 1);
    strncpy(arg->skill_name, skill_name ? skill_name : "", sizeof(arg->skill_name) - 1);
    strncpy(arg->tool_name, tool_name ? tool_name : "", sizeof(arg->tool_name) - 1);
    arg->tool_idx = tool_idx;
    arg->op_type = 2;

    s->op_progress.active = true;
    s->op_progress.percent = 0.05f;
    snprintf(s->op_progress.title, sizeof(s->op_progress.title), "正在采纳 [%s] 副本为全局主版本基准...", tool_name);
    strncpy(s->op_progress.message, "准备物理复制至 ~/.ai-space/skills/...", sizeof(s->op_progress.message) - 1);
    s->op_progress.thread_running = true;

    pthread_create(&s->op_progress.thread, NULL, hub_op_worker_thread, arg);
    pthread_detach(s->op_progress.thread);
}

void start_broadcast_thread(app_state_t *s, const char *skill_name, int tool_idx) {
    if (!s || s->op_progress.active) return;

    hub_op_thread_arg_t *arg = (hub_op_thread_arg_t *)malloc(sizeof(hub_op_thread_arg_t));
    if (!arg) return;
    arg->s = s;
    arg->tool_skills_path[0] = '\0';
    strncpy(arg->skill_name, skill_name ? skill_name : "", sizeof(arg->skill_name) - 1);
    arg->tool_name[0] = '\0';
    arg->tool_idx = tool_idx;
    arg->op_type = 3;

    s->op_progress.active = true;
    s->op_progress.percent = 0.05f;
    snprintf(s->op_progress.title, sizeof(s->op_progress.title), "正在全量广播主版本 [%s] 到所有工具...", skill_name);
    strncpy(s->op_progress.message, "准备自动备份并物理分发至各工具目录...", sizeof(s->op_progress.message) - 1);
    s->op_progress.thread_running = true;

    pthread_create(&s->op_progress.thread, NULL, hub_op_worker_thread, arg);
    pthread_detach(s->op_progress.thread);
}

void render_global_navbar(cui_ctx *ui, app_state_t *s) {
    render_global_nav_bar(ui, s);
}

void render_global_nav_bar(cui_ctx *ui, app_state_t *s) {
    cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
        cui_row(ui, .gap = 10, .align = CUI_ALIGN_CENTER) {
            cui_box(ui, .width = CUI_PX(30), .height = CUI_PX(30), .radius = 8,
                    .bg = cui_alpha(cui_color_of(ui, CUI_COLOR_ACCENT), 0.18f),
                    .dir = CUI_STACK, .align = CUI_ALIGN_CENTER, .justify = CUI_JUSTIFY_CENTER) {
                cui_icon(ui, CUI_ICON_LAYERS, 16, CUI_COLOR_ACCENT);
            }
            cui_text(ui, "AI-Space", .font = CUI_FONT_DISPLAY, .size = 17, .color = CUI_COLOR_ACCENT);
            cui_text(ui, " · ", .color = CUI_COLOR_TEXT_MUTED);

            /* Tab 1: 本地工具目录 */
            bool is_tools = (s->current_page == PAGE_TOOL_LIST);
            if (cui_button(ui, "  本地工具目录",
                           .variant = is_tools ? CUI_BUTTON_PRIMARY : CUI_BUTTON_SECONDARY,
                           .size = CUI_SIZE_SM,
                           .icon = CUI_ICON_FOLDER)) {
                s->current_page = PAGE_TOOL_LIST;
            }

            /* Tab 2: 技能中心与全景关系 */
            bool is_hub = (s->current_page == PAGE_HUB_MATRIX);
            if (cui_button(ui, "  统一技能中心与全景关系",
                           .variant = is_hub ? CUI_BUTTON_PRIMARY : CUI_BUTTON_SECONDARY,
                           .size = CUI_SIZE_SM,
                           .icon = CUI_ICON_GRID_3X3)) {
                if (!s->hub_matrix_loaded && !s->hub_scan.is_scanning) {
                    start_hub_scan_thread(s);
                }
                s->current_page = PAGE_HUB_MATRIX;
                s->hub_subview = 0;
            }
        }

        cui_spacer(ui);

        if (s->toast_frames > 0 && s->toast_msg[0] != '\0') {
            cui_row(ui, .gap = 6, .align = CUI_ALIGN_CENTER) {
                cui_icon(ui, CUI_ICON_INFO, 14, CUI_COLOR_ACCENT);
                cui_text(ui, s->toast_msg, .size = 13, .color = CUI_COLOR_ACCENT);
            }
        }
    }
}

void render_op_progress_modal(cui_ctx *ui, app_state_t *s) {
    if (!s || !s->op_progress.active) return;

    /* Overlay modal card */
    cui_column(ui, .width = CUI_FILL, .height = CUI_FILL,
               .align = CUI_ALIGN_CENTER, .justify = CUI_JUSTIFY_CENTER) {
        cui_card(ui, .width = CUI_PX(480), .pad = 22, .gap = 14) {
            cui_row(ui, .gap = 10, .align = CUI_ALIGN_CENTER) {
                cui_spinner(ui, 24, .style = CUI_SPINNER_COMET, .color = CUI_COLOR_ACCENT);
                cui_heading(ui, s->op_progress.title[0] ? s->op_progress.title : "正在处理中...", 4);
            }

            cui_progress(ui, s->op_progress.percent,
                         .width = CUI_FILL,
                         .label = s->op_progress.message,
                         .show_value = true,
                         .tone = CUI_TONE_ACCENT);

            cui_row(ui, .gap = 6, .align = CUI_ALIGN_CENTER) {
                cui_icon(ui, CUI_ICON_SHIELD, 13, CUI_COLOR_TEXT_MUTED);
                cui_text(ui, "正在进行纯物理复制，覆盖前已自动在 ~/.ai-space/backups/ 保存备份快照",
                         .size = 11, .color = CUI_COLOR_TEXT_MUTED);
            }
        }
    }
}
