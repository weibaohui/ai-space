#include "hub_bulk.h"
#include "hub_files.h"
#include "app_hub_tasks.h"
#include "app_diff.h"
#include "app_task.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    HUB_SCAN, HUB_INSTALL, HUB_OVERWRITE, HUB_PROMOTE, HUB_BROADCAST,
    HUB_ADOPT_ALL, HUB_DISTRIBUTE, HUB_DISTRIBUTE_ALL
} hub_action_t;

typedef struct app_hub_job {
    app_task_t task;
    hub_action_t action;
    sqlite3 *db;
    const hub_matrix_t *current_matrix;
    hub_matrix_t matrix;
    char path[512];
    char skill[128];
    char tool[128];
    int tool_idx;
    int result;
    int scans_ok;
    int count;
} app_hub_job_t;

static int run_hub_operation(app_hub_job_t *job) {
    switch (job->action) {
    case HUB_INSTALL:
    case HUB_OVERWRITE:
        return hub_sync_master_to_tool(job->path, job->skill, job->tool);
    case HUB_PROMOTE:
        return hub_promote_to_master(job->path, job->skill);
    case HUB_BROADCAST:
        return hub_broadcast_skill(job->current_matrix, job->skill, &job->count);
    case HUB_ADOPT_ALL:
        return hub_adopt_all_to_master_ex(job->current_matrix, &job->count,
                                          app_task_progress, &job->task);
    case HUB_DISTRIBUTE:
        return hub_distribute_skill_to_all_tools(job->current_matrix, job->skill, &job->count);
    case HUB_DISTRIBUTE_ALL:
        return hub_distribute_all_to_all_tools(job->current_matrix, &job->count,
                                               app_task_progress, &job->task);
    case HUB_SCAN:
        return 0;
    }
    return -1;
}

/* 文件操作可能部分成功，结束后也要重新扫描已写入的磁盘状态。 */
static void *hub_worker(void *arg) {
    app_hub_job_t *job = arg;
    if (job->action != HUB_SCAN) {
        job->result = run_hub_operation(job);
        app_task_progress(0.85f, "正在刷新主仓和工具副本关系", &job->task);
    }
    job->scans_ok = hub_scan_matrix_ex(job->db, &job->matrix,
                                      job->action == HUB_SCAN ? app_task_progress : NULL,
                                      &job->task) == 0;
    app_task_complete(&job->task);
    return NULL;
}

static void start_hub_job(app_state_t *s, hub_action_t action, const char *path,
                          const char *skill, const char *tool, int tool_idx) {
    if (!s || s->hub_job || s->sync_is_busy || s->op_progress.active) return;
    app_hub_job_t *job = calloc(1, sizeof(*job));
    if (!job) {
        app_set_toast(s, "无法创建主仓任务");
        return;
    }
    job->action = action;
    job->db = s->db;
    /* 任务运行期间保留当前矩阵，工作线程另建扫描结果。 */
    job->current_matrix = &s->hub_matrix;
    job->tool_idx = tool_idx;
    snprintf(job->path, sizeof(job->path), "%s", path ? path : "");
    snprintf(job->skill, sizeof(job->skill), "%s", skill ? skill : "");
    snprintf(job->tool, sizeof(job->tool), "%s", tool ? tool : "");
    job->task.percent = 0.05f;
    if (app_task_start(&job->task, hub_worker, job) != 0) {
        free(job);
        app_set_toast(s, "无法启动主仓任务");
        return;
    }
    s->hub_job = job;
    if (action == HUB_SCAN) {
        s->hub_scan.is_scanning = true;
        s->hub_scan.progress = 0.05f;
        snprintf(s->hub_scan.status_text, sizeof(s->hub_scan.status_text), "准备比对技能和工具副本");
    } else {
        static const char *const titles[] = {
            [HUB_INSTALL] = "正在安装技能", [HUB_OVERWRITE] = "正在更新工具副本",
            [HUB_PROMOTE] = "正在采纳技能", [HUB_BROADCAST] = "正在广播主版本",
            [HUB_ADOPT_ALL] = "正在全量收录", [HUB_DISTRIBUTE] = "正在分发技能",
            [HUB_DISTRIBUTE_ALL] = "正在全量分发"
        };
        s->op_progress.active = true;
        s->op_progress.percent = 0.05f;
        snprintf(s->op_progress.title, sizeof(s->op_progress.title), "%s", titles[action]);
        snprintf(s->op_progress.message, sizeof(s->op_progress.message), "正在准备文件和备份");
        s->op_progress.sub_message[0] = '\0';
    }
}

static void finish_hub_job(app_state_t *s, const app_hub_job_t *job) {
    if (job->result != 0) {
        char message[128];
        snprintf(message, sizeof(message), "主仓操作未全部完成，已更新 %d 项", job->count);
        app_set_toast(s, message);
        return;
    }

    if (job->action <= HUB_BROADCAST && job->action != HUB_SCAN &&
        s->hub_subview == HUB_SUBVIEW_DIFF && job->skill[0] &&
        strcmp(s->selected_hub_skill, job->skill) == 0) {
        build_skill_diff_manifest(s, job->skill, job->tool_idx);
        load_hub_diff(s, job->skill, job->tool_idx, s->diff_file_rel);
    }

    char toast[256];
    switch (job->action) {
    case HUB_SCAN:
        if (!job->scans_ok) app_set_toast(s, "主仓扫描失败");
        return;
    case HUB_ADOPT_ALL:
        snprintf(toast, sizeof(toast), "已收录 %d 个技能", job->count);
        break;
    case HUB_BROADCAST:
    case HUB_DISTRIBUTE:
    case HUB_DISTRIBUTE_ALL:
        snprintf(toast, sizeof(toast), "已更新 %d 个工具副本", job->count);
        break;
    default:
        snprintf(toast, sizeof(toast), "已完成 [%s] 的文件更新", job->skill);
        break;
    }
    app_set_toast(s, job->scans_ok ? toast : "文件操作已完成，主仓状态刷新失败");
}

void app_hub_tasks_poll(app_state_t *s) {
    app_hub_job_t *job = s->hub_job;
    if (!job) return;
    bool scan = job->action == HUB_SCAN;
    bool done = scan
        ? app_task_poll(&job->task, &s->hub_scan.progress, s->hub_scan.status_text,
                        sizeof(s->hub_scan.status_text))
        : app_task_poll(&job->task, &s->op_progress.percent, s->op_progress.message,
                        sizeof(s->op_progress.message));
    if (!done) return;

    /* 等待线程结束后才移交矩阵，旧矩阵由主线程释放。 */
    app_task_join(&job->task);
    s->hub_job = NULL;
    if (scan) s->hub_scan.is_scanning = false;
    else s->op_progress.active = false;
    s->hub_scan.failed = !job->scans_ok;
    if (job->scans_ok) {
        hub_free_matrix(&s->hub_matrix);
        s->hub_matrix = job->matrix;
        job->matrix = (hub_matrix_t){0};
        s->hub_matrix_loaded = true;
    }
    app_storage_invalidate(s);
    finish_hub_job(s, job);
    hub_free_matrix(&job->matrix);
    free(job);
}

void app_hub_tasks_shutdown(app_state_t *s) {
    if (!s->hub_job) return;
    app_task_join(&s->hub_job->task);
    hub_free_matrix(&s->hub_job->matrix);
    free(s->hub_job);
    s->hub_job = NULL;
}

void start_hub_scan_thread(app_state_t *s) { start_hub_job(s, HUB_SCAN, NULL, NULL, NULL, -1); }
void start_adopt_all_thread(app_state_t *s) { start_hub_job(s, HUB_ADOPT_ALL, NULL, NULL, NULL, -1); }
void start_distribute_all_thread(app_state_t *s) { start_hub_job(s, HUB_DISTRIBUTE_ALL, NULL, NULL, NULL, -1); }

void start_copy_install_thread(app_state_t *s, const char *path,
                               const char *skill, const char *tool, int tool_idx) {
    start_hub_job(s, HUB_INSTALL, path, skill, tool, tool_idx);
}

void start_sync_master_to_tool_thread(app_state_t *s, const char *path,
                                     const char *skill, const char *tool, int tool_idx) {
    start_hub_job(s, HUB_OVERWRITE, path, skill, tool, tool_idx);
}

void start_promote_to_master_thread(app_state_t *s, const char *path,
                                    const char *skill, const char *tool, int tool_idx) {
    start_hub_job(s, HUB_PROMOTE, path, skill, tool, tool_idx);
}

void start_broadcast_thread(app_state_t *s, const char *skill, int tool_idx) {
    start_hub_job(s, HUB_BROADCAST, NULL, skill, NULL, tool_idx);
}

void start_distribute_skill_to_all_thread(app_state_t *s, const char *skill) {
    start_hub_job(s, HUB_DISTRIBUTE, NULL, skill, NULL, -1);
}
