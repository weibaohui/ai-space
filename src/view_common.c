#include "view_common.h"
#include "view_skill_tree.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
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


typedef struct {
    char rel_path[256];
} rel_file_t;

static void collect_rel_files(const char *root, const char *sub, rel_file_t **arr, int *cnt, int *cap) {
    char dir_path[1024];
    if (sub && sub[0] != '\0') {
        snprintf(dir_path, sizeof(dir_path), "%s/%s", root, sub);
    } else {
        snprintf(dir_path, sizeof(dir_path), "%s", root);
    }

    DIR *d = opendir(dir_path);
    if (!d) return;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        if (strcmp(ent->d_name, "__pycache__") == 0) continue;

        char next_sub[256];
        if (sub && sub[0] != '\0') {
            snprintf(next_sub, sizeof(next_sub), "%s/%s", sub, ent->d_name);
        } else {
            snprintf(next_sub, sizeof(next_sub), "%s", ent->d_name);
        }

        char abs_path[1024];
        snprintf(abs_path, sizeof(abs_path), "%s/%s", root, next_sub);

        struct stat st;
        if (stat(abs_path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                collect_rel_files(root, next_sub, arr, cnt, cap);
            } else if (S_ISREG(st.st_mode)) {
                if (*cnt >= *cap) {
                    *cap *= 2;
                    *arr = (rel_file_t *)realloc(*arr, sizeof(rel_file_t) * (*cap));
                }
                strncpy((*arr)[*cnt].rel_path, next_sub, sizeof((*arr)[*cnt].rel_path) - 1);
                (*arr)[*cnt].rel_path[sizeof((*arr)[*cnt].rel_path) - 1] = '\0';
                (*cnt)++;
            }
        }
    }
    closedir(d);
}

static int compare_manifest_items(const void *a, const void *b) {
    const hub_diff_file_item_t *ia = (const hub_diff_file_item_t *)a;
    const hub_diff_file_item_t *ib = (const hub_diff_file_item_t *)b;

    /* SKILL.md always first */
    if (strcmp(ia->rel_path, "SKILL.md") == 0) return -1;
    if (strcmp(ib->rel_path, "SKILL.md") == 0) return 1;

    /* Changed before in-sync */
    bool ia_changed = (ia->status != DIFF_FILE_IN_SYNC);
    bool ib_changed = (ib->status != DIFF_FILE_IN_SYNC);
    if (ia_changed != ib_changed) {
        return ia_changed ? -1 : 1;
    }

    return strcmp(ia->rel_path, ib->rel_path);
}

void free_skill_diff_manifest(hub_skill_diff_manifest_t *m) {
    if (!m) return;
    if (m->items) {
        free(m->items);
        m->items = NULL;
    }
    m->count = 0;
    m->modified_count = 0;
    m->added_count = 0;
    m->deleted_count = 0;
    m->in_sync_count = 0;
    m->total_added_lines = 0;
    m->total_removed_lines = 0;
}

void build_skill_diff_manifest(app_state_t *s, const char *skill_name, int tool_idx) {
    if (!s || !skill_name) return;

    free_skill_diff_manifest(&s->diff_manifest);
    if (s->hub_diff_file_tree) {
        free_skill_file_tree(s->hub_diff_file_tree);
        free(s->hub_diff_file_tree);
        s->hub_diff_file_tree = NULL;
    }
    strncpy(s->diff_manifest.skill_name, skill_name, sizeof(s->diff_manifest.skill_name) - 1);

    hub_skill_entry_t *entry = NULL;
    for (int i = 0; i < s->hub_matrix.skill_count; i++) {
        if (strcmp(s->hub_matrix.skills[i].skill_name, skill_name) == 0) {
            entry = &s->hub_matrix.skills[i];
            break;
        }
    }
    if (!entry) return;

    const char *tool_skills_path = NULL;
    if (tool_idx >= 0 && tool_idx < entry->tool_count) {
        strncpy(s->diff_manifest.tool_name, entry->tool_copies[tool_idx].tool_name, sizeof(s->diff_manifest.tool_name) - 1);
        if (entry->tool_copies[tool_idx].exists) {
            tool_skills_path = entry->tool_copies[tool_idx].skill_path;
        }
    }

    int rel_cap = 64;
    int rel_count = 0;
    rel_file_t *rel_files = (rel_file_t *)malloc(sizeof(rel_file_t) * rel_cap);

    /* 1. Collect from master hub */
    if (entry->in_hub) {
        collect_rel_files(entry->hub_path, "", &rel_files, &rel_count, &rel_cap);
    }

    /* 2. Collect from tool copy */
    if (tool_skills_path) {
        int tool_cap = 64;
        int tool_count = 0;
        rel_file_t *tool_files = (rel_file_t *)malloc(sizeof(rel_file_t) * tool_cap);
        collect_rel_files(tool_skills_path, "", &tool_files, &tool_count, &tool_cap);

        for (int i = 0; i < tool_count; i++) {
            bool found = false;
            for (int j = 0; j < rel_count; j++) {
                if (strcmp(rel_files[j].rel_path, tool_files[i].rel_path) == 0) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                if (rel_count >= rel_cap) {
                    rel_cap *= 2;
                    rel_files = (rel_file_t *)realloc(rel_files, sizeof(rel_file_t) * rel_cap);
                }
                strncpy(rel_files[rel_count].rel_path, tool_files[i].rel_path, sizeof(rel_files[rel_count].rel_path) - 1);
                rel_files[rel_count].rel_path[sizeof(rel_files[rel_count].rel_path) - 1] = '\0';
                rel_count++;
            }
        }
        free(tool_files);
    }

    /* 3. Build manifest items */
    s->diff_manifest.items = (hub_diff_file_item_t *)calloc(rel_count, sizeof(hub_diff_file_item_t));
    s->diff_manifest.count = rel_count;

    for (int i = 0; i < rel_count; i++) {
        hub_diff_file_item_t *item = &s->diff_manifest.items[i];
        strncpy(item->rel_path, rel_files[i].rel_path, sizeof(item->rel_path) - 1);

        const char *fname = strrchr(item->rel_path, '/');
        strncpy(item->file_name, fname ? (fname + 1) : item->rel_path, sizeof(item->file_name) - 1);

        char m_full[1200];
        char t_full[1200];
        snprintf(m_full, sizeof(m_full), "%s/%s", entry->hub_path, item->rel_path);
        snprintf(t_full, sizeof(t_full), "%s/%s", tool_skills_path ? tool_skills_path : "", item->rel_path);

        struct stat m_st, t_st;
        bool has_m = (entry->in_hub && stat(m_full, &m_st) == 0 && S_ISREG(m_st.st_mode));
        bool has_t = (tool_skills_path && stat(t_full, &t_st) == 0 && S_ISREG(t_st.st_mode));

        if (has_m) item->master_size = m_st.st_size;
        if (has_t) item->tool_size = t_st.st_size;

        if (has_m && !has_t) {
            item->status = DIFF_FILE_DELETED;
            s->diff_manifest.deleted_count++;
            size_t m_sz = 0;
            int m_ln = 0;
            bool m_bin = false;
            char *m_text = read_file_text_content(m_full, 1024 * 1024, &m_sz, &m_ln, &m_bin);
            item->is_binary = m_bin;
            item->removed_lines = (m_ln > 0) ? m_ln : 1;
            item->added_lines = 0;
            s->diff_manifest.total_removed_lines += item->removed_lines;
            if (m_text) free(m_text);
        } else if (!has_m && has_t) {
            item->status = DIFF_FILE_ADDED;
            s->diff_manifest.added_count++;
            size_t t_sz = 0;
            int t_ln = 0;
            bool t_bin = false;
            char *t_text = read_file_text_content(t_full, 1024 * 1024, &t_sz, &t_ln, &t_bin);
            item->is_binary = t_bin;
            item->added_lines = (t_ln > 0) ? t_ln : 1;
            item->removed_lines = 0;
            s->diff_manifest.total_added_lines += item->added_lines;
            if (t_text) free(t_text);
        } else if (has_m && has_t) {
            /* Check if identical bytes */
            bool same = false;
            if (m_st.st_size == t_st.st_size) {
                FILE *fm = fopen(m_full, "rb");
                FILE *ft = fopen(t_full, "rb");
                same = true;
                if (fm && ft) {
                    char b1[4096], b2[4096];
                    size_t n1, n2;
                    while ((n1 = fread(b1, 1, sizeof(b1), fm)) > 0) {
                        n2 = fread(b2, 1, sizeof(b2), ft);
                        if (n1 != n2 || memcmp(b1, b2, n1) != 0) {
                            same = false;
                            break;
                        }
                    }
                } else {
                    same = false;
                }
                if (fm) fclose(fm);
                if (ft) fclose(ft);
            }

            if (same) {
                item->status = DIFF_FILE_IN_SYNC;
                item->added_lines = 0;
                item->removed_lines = 0;
                s->diff_manifest.in_sync_count++;
            } else {
                item->status = DIFF_FILE_MODIFIED;
                s->diff_manifest.modified_count++;

                size_t m_sz = 0, t_sz = 0;
                int m_ln = 0, t_ln = 0;
                bool m_bin = false, t_bin = false;
                char *m_text = read_file_text_content(m_full, 1024 * 1024, &m_sz, &m_ln, &m_bin);
                char *t_text = read_file_text_content(t_full, 1024 * 1024, &t_sz, &t_ln, &t_bin);

                if (m_bin || t_bin) {
                    item->is_binary = true;
                    item->added_lines = 0;
                    item->removed_lines = 0;
                } else if (m_text && t_text) {
                    cui_diff_stats stats;
                    memset(&stats, 0, sizeof(stats));
                    cui_diff_compute(m_text, m_sz, t_text, t_sz, 0, NULL, 0, &stats);
                    item->added_lines = stats.added;
                    item->removed_lines = stats.removed;
                    s->diff_manifest.total_added_lines += stats.added;
                    s->diff_manifest.total_removed_lines += stats.removed;
                } else {
                    item->added_lines = t_ln;
                    item->removed_lines = m_ln;
                    s->diff_manifest.total_added_lines += t_ln;
                    s->diff_manifest.total_removed_lines += m_ln;
                }

                if (m_text) free(m_text);
                if (t_text) free(t_text);
            }
        }
    }
    free(rel_files);

    /* Sort manifest: SKILL.md first, then modified, then added/deleted, then in_sync */
    if (s->diff_manifest.count > 1) {
        qsort(s->diff_manifest.items, s->diff_manifest.count, sizeof(hub_diff_file_item_t), compare_manifest_items);
    }

    s->diff_manifest_loaded = true;

    /* Ensure current active file is valid */
    bool current_found = false;
    for (int i = 0; i < s->diff_manifest.count; i++) {
        if (strcmp(s->diff_manifest.items[i].rel_path, s->diff_file_rel) == 0) {
            current_found = true;
            break;
        }
    }
    if (!current_found) {
        if (s->diff_manifest.count > 0) {
            int pick = 0;
            for (int i = 0; i < s->diff_manifest.count; i++) {
                if (s->diff_manifest.items[i].status != DIFF_FILE_IN_SYNC) {
                    pick = i;
                    break;
                }
            }
            strncpy(s->diff_file_rel, s->diff_manifest.items[pick].rel_path, sizeof(s->diff_file_rel) - 1);
        } else {
            strncpy(s->diff_file_rel, "SKILL.md", sizeof(s->diff_file_rel) - 1);
        }
    }

    /* Build hierarchical diff file tree for Central Hub view */
    s->hub_diff_file_tree = build_tree_from_diff_manifest(&s->diff_manifest, entry->hub_path, tool_skills_path);
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
    build_skill_diff_manifest(s, skill_name, chosen_idx);
    load_hub_diff(s, skill_name, chosen_idx, s->diff_file_rel);
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
    snprintf(toast, sizeof(toast), "全量收录完成！成功将 %d 个技能收录至主库", adopted);
    app_set_toast(s, toast);
    return NULL;
}

void start_adopt_all_thread(app_state_t *s) {
    if (!s || s->op_progress.active) return;

    s->op_progress.active = true;
    s->op_progress.percent = 0.05f;
    strncpy(s->op_progress.title, "正在全量收录本地技能至主库...", sizeof(s->op_progress.title) - 1);
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

        build_skill_diff_manifest(s, arg->skill_name, arg->tool_idx);
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

        build_skill_diff_manifest(s, arg->skill_name, arg->tool_idx);
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

        build_skill_diff_manifest(s, arg->skill_name, arg->tool_idx);
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
            build_skill_diff_manifest(s, arg->skill_name, arg->tool_idx);
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

            /* Tab 1: 工具列表 */
            bool is_tools = (s->current_page == PAGE_TOOL_LIST);
            if (cui_button(ui, "  工具列表",
                           .variant = is_tools ? CUI_BUTTON_PRIMARY : CUI_BUTTON_SECONDARY,
                           .size = CUI_SIZE_SM,
                           .icon = CUI_ICON_FOLDER)) {
                s->current_page = PAGE_TOOL_LIST;
            }

            /* Tab 2: 主仓管理 */
            bool is_hub = (s->current_page == PAGE_HUB_MATRIX);
            if (cui_button(ui, "  主仓管理",
                           .variant = is_hub ? CUI_BUTTON_PRIMARY : CUI_BUTTON_SECONDARY,
                           .size = CUI_SIZE_SM,
                           .icon = CUI_ICON_ARCHIVE)) {
                if (!s->hub_matrix_loaded && !s->hub_scan.is_scanning) {
                    start_hub_scan_thread(s);
                }
                s->current_page = PAGE_HUB_MATRIX;
                if (s->hub_subview == HUB_SUBVIEW_DIFF || s->hub_subview == HUB_SUBVIEW_EXPLORER) {
                    s->hub_subview = HUB_SUBVIEW_MASTER_SKILLS;
                }
            }

            /* Tab 3: 设置 */
            bool is_settings = (s->current_page == PAGE_SETTINGS);
            if (cui_button(ui, "  设置",
                           .variant = is_settings ? CUI_BUTTON_PRIMARY : CUI_BUTTON_SECONDARY,
                           .size = CUI_SIZE_SM,
                           .icon = CUI_ICON_SETTINGS)) {
                s->current_page = PAGE_SETTINGS;
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

    const char *title = s->op_progress.title[0] ? s->op_progress.title : "操作处理中";
    cui_dialog(ui, title, &s->op_progress.active,
               .width = 480, .persistent = true, .no_close_button = true) {
        cui_column(ui, .width = CUI_FILL, .gap = 14, .pad = 6) {
            cui_row(ui, .gap = 10, .align = CUI_ALIGN_CENTER) {
                cui_spinner(ui, 22, .style = CUI_SPINNER_COMET, .color = CUI_COLOR_ACCENT);
                cui_text(ui, s->op_progress.message[0] ? s->op_progress.message : "正在执行...",
                         .size = 13, .color = CUI_COLOR_TEXT_SECONDARY);
            }

            cui_progress(ui, s->op_progress.percent,
                         .width = CUI_FILL,
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

void app_init_settings(app_state_t *s) {
    if (!s || !s->db) return;

    db_get_setting(s->db, "master_hub_dir", s->setting_hub_dir, sizeof(s->setting_hub_dir), "~/.ai-space/skills");
    db_get_setting(s->db, "backup_dir", s->setting_backup_dir, sizeof(s->setting_backup_dir), "~/.ai-space/backups");

    char val[64];
    db_get_setting(s->db, "auto_backup", val, sizeof(val), "1");
    s->setting_auto_backup = (strcmp(val, "0") != 0);

    db_get_setting(s->db, "auto_scan", val, sizeof(val), "1");
    s->setting_auto_scan = (strcmp(val, "0") != 0);

    db_get_setting(s->db, "default_view", val, sizeof(val), "0");
    s->setting_default_view = atoi(val);

    db_get_setting(s->db, "scan_depth", val, sizeof(val), "5");
    s->setting_scan_depth = atoi(val);
    if (s->setting_scan_depth <= 0) s->setting_scan_depth = 5;

    /* Apply custom dirs to hub manager */
    hub_set_custom_dirs(s->setting_hub_dir, s->setting_backup_dir);
}

void app_save_settings(app_state_t *s) {
    if (!s || !s->db) return;

    db_set_setting(s->db, "master_hub_dir", s->setting_hub_dir);
    db_set_setting(s->db, "backup_dir", s->setting_backup_dir);
    db_set_setting(s->db, "auto_backup", s->setting_auto_backup ? "1" : "0");
    db_set_setting(s->db, "auto_scan", s->setting_auto_scan ? "1" : "0");

    char val[32];
    snprintf(val, sizeof(val), "%d", s->setting_default_view);
    db_set_setting(s->db, "default_view", val);

    snprintf(val, sizeof(val), "%d", s->setting_scan_depth);
    db_set_setting(s->db, "scan_depth", val);

    /* Apply custom dirs to hub manager */
    hub_set_custom_dirs(s->setting_hub_dir, s->setting_backup_dir);
}
