#include "app_sync.h"
#include "app_settings.h"
#include "app_hub_tasks.h"
#include "app_task.h"
#include "sync_baseline.h"
#include "sync_directory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum { SYNC_CHECK, SYNC_PROBE, SYNC_PULL, SYNC_PUSH } sync_action_t;

typedef struct app_sync_job {
    app_task_t task;
    sync_action_t action;
    sync_config_t cfg;
    char local[512];
    char skill[128];
    bool resolve_conflict;
    bool activate;
    bool unchanged;
    int result;
    int refreshed;
    sync_drift_summary_t drift;
    sync_baseline_t baseline;
    sync_drift_item_t expected;
    char scope[SYNC_SCOPE_CAP];
    char error[256];
} app_sync_job_t;

static int collect_drift(sync_backend_t *backend, app_sync_job_t *job) {
    int rc = backend->ops->check_drift(backend, job->local, &job->drift,
                                        job->error, sizeof(job->error));
    if (rc != 0 && !job->error[0])
        snprintf(job->error, sizeof(job->error), "无法检查远端同步状态");
    return rc;
}

static int transfer(sync_backend_t *backend, app_sync_job_t *job) {
    const sync_driver_ops_t *ops = backend->ops;
    bool pull = job->action == SYNC_PULL;
    if (job->skill[0]) {
        if (!sync_drift_is_candidate(job->expected.status, pull) &&
            !(job->resolve_conflict && job->expected.status == SYNC_DRIFT_CONFLICT)) return -1;
        backend->expected_skill = &job->expected;
        return pull ? ops->pull_skill(backend, job->skill, job->local, app_task_progress, &job->task)
                    : ops->push_skill(backend, job->skill, job->local, app_task_progress, &job->task);
    }
    if (ops->transfer_batch)
        return ops->transfer_batch(backend, job->local, pull, app_task_progress, &job->task, &job->drift);
    return pull ? ops->pull_all(backend, job->local, app_task_progress, &job->task)
                : ops->push_all(backend, job->local, app_task_progress, &job->task);
}

static void run_sync(app_sync_job_t *job) {
    if (job->action == SYNC_PROBE) {
        job->result = sync_backend_check_available(&job->cfg, job->error, sizeof(job->error));
        return;
    }

    sync_backend_t *backend = NULL;
    if (sync_backend_create(&job->cfg, &backend) != 0) {
        job->result = -1;
        job->refreshed = -1;
        snprintf(job->error, sizeof(job->error), "无法初始化远端同步后端");
        return;
    }
    backend->baseline = &job->baseline;
    job->result = job->action == SYNC_CHECK ? 0 : transfer(backend, job);
    if (job->result == SYNC_BATCH_UNCHANGED) {
        job->unchanged = true;
        job->result = 0;
        job->refreshed = 0;
    } else {
        job->refreshed = collect_drift(backend, job);
    }
    sync_backend_destroy(backend);
}

static void *sync_worker(void *arg) {
    app_sync_job_t *job = arg;
    run_sync(job);
    app_task_complete(&job->task);
    return NULL;
}

static int load_baseline(app_state_t *s, app_sync_job_t *job) {
    if (sync_baseline_scope(&job->cfg, job->local, job->scope, sizeof(job->scope)) != 0) return -1;
    return sync_baseline_load(s->db, job->scope, &job->baseline);
}

static void start_sync(app_state_t *s, sync_action_t action, const char *skill, bool option) {
    if (!s || s->sync_job || s->op_progress.active || s->hub_scan.is_scanning) return;
    if (action != SYNC_PROBE && s->sync_cfg.type == SYNC_BACKEND_NONE) return;
    if (skill && !s->sync_drift_loaded) {
        app_set_toast(s, "请先检查远端状态");
        return;
    }
    app_sync_job_t *job = calloc(1, sizeof(*job));
    if (!job) {
        app_set_toast(s, "无法创建同步任务");
        return;
    }

    job->action = action;
    job->cfg = action == SYNC_PROBE ? s->sync_pending_cfg : s->sync_cfg;
    if (action == SYNC_PROBE) job->cfg.type = s->setting_sync_candidate_type;
    job->activate = action == SYNC_PROBE && option;
    job->resolve_conflict = skill && option;
    if (skill) {
        bool found = false;
        for (int i = 0; i < s->sync_drift.count; i++) {
            if (strcmp(s->sync_drift.items[i].skill_name, skill) != 0) continue;
            job->expected = s->sync_drift.items[i];
            found = true;
            break;
        }
        if (!found) {
            free(job);
            app_set_toast(s, "请先检查远端状态");
            return;
        }
        snprintf(job->skill, sizeof(job->skill), "%s", skill);
    }
    snprintf(job->local, sizeof(job->local), "%s", s->setting_hub_dir);
    if (action != SYNC_PROBE && load_baseline(s, job) != 0) {
        free(job);
        app_set_toast(s, "无法读取同步基线");
        return;
    }

    const char *title = action == SYNC_PROBE ? "正在测试远端连接"
                      : action == SYNC_CHECK ? "正在检查远端变动"
                      : action == SYNC_PUSH ? "正在推送技能" : "正在拉取技能";
    job->task.percent = 0.1f;
    if (app_task_start(&job->task, sync_worker, job) != 0) {
        free(job->baseline.items);
        free(job);
        app_set_toast(s, "无法启动同步任务");
        return;
    }
    s->sync_job = job;
    s->sync_is_busy = true;
    s->op_progress.active = true;
    s->op_progress.percent = 0.1f;
    snprintf(s->op_progress.title, sizeof(s->op_progress.title), "%s", title);
    snprintf(s->op_progress.message, sizeof(s->op_progress.message), "正在连接远端后端");
    snprintf(s->op_progress.sub_message, sizeof(s->op_progress.sub_message), "%s",
             sync_backend_display_name(job->cfg.type));
}

static void apply_drift(app_state_t *s, app_sync_job_t *job) {
    sync_free_drift_summary(&s->sync_drift);
    s->sync_drift_loaded = job->refreshed == 0;
    if (s->sync_drift_loaded) {
        if (sync_baseline_record(s->db, job->scope, &job->drift) != 0)
            app_set_toast(s, "同步状态已刷新，基线保存失败");
        s->sync_drift = job->drift;
        job->drift = (sync_drift_summary_t){0};
    }
}

static int activate_config(app_state_t *s, const sync_config_t *cfg) {
    if (app_store_sync_settings(s->db, cfg) != 0) return -1;
    s->sync_cfg = *cfg;
    sync_free_drift_summary(&s->sync_drift);
    s->sync_drift_loaded = false;
    s->sync_status_msg[0] = '\0';
    return 0;
}

static void finish_probe(app_state_t *s, app_sync_job_t *job) {
    if (job->result == 0 && job->activate && activate_config(s, &job->cfg) != 0) {
        job->result = -1;
        snprintf(job->error, sizeof(job->error), "远端连接正常，配置保存失败");
    }
    s->sync_has_checked_candidate = true;
    s->sync_candidate_checked_ok = job->result == 0;
    snprintf(s->sync_candidate_check_msg, sizeof(s->sync_candidate_check_msg), "%s", job->error);
    app_set_toast(s, job->result == 0 && job->activate ? "已启用远端后端" : job->error);
}

static void finish_transfer(app_state_t *s, app_sync_job_t *job) {
    const char *action = job->action == SYNC_PUSH ? "推送" : "拉取";
    s->sync_status_msg[0] = '\0';
    if (job->result != 0) {
        const char *detail = job->task.message[0] ? job->task.message : job->error;
        snprintf(s->sync_status_msg, sizeof(s->sync_status_msg), "%s未完成%s%.210s",
                 action, detail[0] ? " · " : "", detail);
    } else if (job->refreshed != 0) {
        snprintf(s->sync_status_msg, sizeof(s->sync_status_msg), "%s完成，未能刷新状态 · %.200s",
                 action, job->error);
    }
    if (!job->unchanged) {
        app_storage_invalidate(s);
        if (job->action == SYNC_PULL) start_hub_scan_thread(s);
    }
    if (s->sync_status_msg[0]) app_set_toast(s, s->sync_status_msg);
}

void app_sync_poll(app_state_t *s) {
    app_sync_job_t *job = s->sync_job;
    if (!job || !app_task_poll(&job->task, &s->op_progress.percent, s->op_progress.message,
                              sizeof(s->op_progress.message))) return;

    app_task_join(&job->task);
    s->sync_job = NULL;
    s->sync_is_busy = false;
    s->op_progress.active = false;

    if (job->action == SYNC_PROBE) {
        finish_probe(s, job);
    } else {
        apply_drift(s, job);
        if (job->action == SYNC_CHECK)
            snprintf(s->sync_status_msg, sizeof(s->sync_status_msg), "%s",
                     job->refreshed == 0 ? "" : job->error);
        else
            finish_transfer(s, job);
    }
    sync_free_drift_summary(&job->drift);
    free(job->baseline.items);
    free(job);
}

void app_sync_shutdown(app_state_t *s) {
    if (s->sync_job) {
        app_task_join(&s->sync_job->task);
        sync_free_drift_summary(&s->sync_job->drift);
        free(s->sync_job->baseline.items);
        free(s->sync_job);
        s->sync_job = NULL;
    }
    sync_free_drift_summary(&s->sync_drift);
}

void app_sync_check_now(app_state_t *s) {
    if (s->sync_cfg.type == SYNC_BACKEND_NONE) return;
    app_sync_job_t job = {.action = SYNC_CHECK, .cfg = s->sync_cfg};
    snprintf(job.local, sizeof(job.local), "%s", s->setting_hub_dir);
    if (load_baseline(s, &job) != 0) {
        snprintf(s->sync_status_msg, sizeof(s->sync_status_msg), "无法读取同步基线");
        return;
    }
    run_sync(&job);
    apply_drift(s, &job);
    snprintf(s->sync_status_msg, sizeof(s->sync_status_msg), "%s",
             job.refreshed == 0 ? "" : job.error);
    sync_free_drift_summary(&job.drift);
    free(job.baseline.items);
}

void app_sync_use_local_mode(app_state_t *s) {
    if (s->sync_job || s->op_progress.active || s->hub_scan.is_scanning) return;
    sync_config_t cfg = s->sync_cfg;
    cfg.type = SYNC_BACKEND_NONE;
    app_set_toast(s, activate_config(s, &cfg) == 0 ? "已成功激活单机本地模式" : "保存单机模式失败");
}

void start_sync_check_thread(app_state_t *s) { start_sync(s, SYNC_CHECK, NULL, false); }
void start_sync_check_available_thread(app_state_t *s, bool activate_on_success) {
    start_sync(s, SYNC_PROBE, NULL, activate_on_success);
}
void start_sync_pull_skill_thread(app_state_t *s, const char *skill_name) {
    if (skill_name && skill_name[0]) start_sync(s, SYNC_PULL, skill_name, false);
}
void start_sync_push_skill_thread(app_state_t *s, const char *skill_name) {
    if (skill_name && skill_name[0]) start_sync(s, SYNC_PUSH, skill_name, false);
}
void start_sync_resolve_conflict_thread(app_state_t *s, const char *skill, bool pull) {
    if (skill && skill[0]) start_sync(s, pull ? SYNC_PULL : SYNC_PUSH, skill, true);
}
void start_sync_pull_all_thread(app_state_t *s) { start_sync(s, SYNC_PULL, NULL, false); }
void start_sync_push_all_thread(app_state_t *s) { start_sync(s, SYNC_PUSH, NULL, false); }
