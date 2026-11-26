#include "app_sync.h"
#include "app_settings.h"
#include "app_hub_tasks.h"
#include "app_task.h"
#include "sync_baseline.h"
#include "sync_directory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    SYNC_CHECK, SYNC_PROBE, SYNC_PULL, SYNC_PUSH,
    SYNC_MERGE_PREPARE, SYNC_MERGE_STAGE, SYNC_MERGE_PUBLISH
} sync_action_t;

/* 主线程复制配置和已显示的指纹，工作线程只处理这份快照。 */
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
    sync_git_draft_t draft;
    sync_baseline_item_t confirmed_merge;
    char previous_workspace[SYNC_PATH_CAP];
    char scope[SYNC_SCOPE_CAP];
    char error[256];
} app_sync_job_t;

static bool merge_action(sync_action_t action) {
    return action == SYNC_MERGE_PREPARE || action == SYNC_MERGE_STAGE || action == SYNC_MERGE_PUBLISH;
}

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
    if (merge_action(job->action)) {
        char root[SYNC_PATH_CAP];
        if (sync_expand(job->local, root, sizeof(root)) != 0) {
            job->result = -1;
            snprintf(job->error, sizeof(job->error), "主仓路径无效");
        } else if (job->action == SYNC_MERGE_PREPARE) {
            app_task_progress(0.3f, "正在查找共同版本并生成合并草稿", &job->task);
            job->result = sync_git_merge_prepare(backend, root, &job->expected, &job->draft,
                                                job->error, sizeof(job->error));
        } else if (job->action == SYNC_MERGE_STAGE) {
            job->result = sync_git_merge_stage(backend, job->skill, &job->draft, job->error, sizeof(job->error));
        } else {
            app_task_progress(0.3f, "正在校验本地和远端版本", &job->task);
            job->result = sync_git_merge_publish(backend, root, job->skill, &job->draft,
                                                &job->confirmed_merge,
                                                job->error, sizeof(job->error));
        }
        if (job->action == SYNC_MERGE_PUBLISH && job->result == 0) {
            /* 远端可能已继续前进，后续检查以刚确认的合并为基线。 */
            for (size_t i = 0; i < job->baseline.count; i++) {
                sync_baseline_item_t *base = &job->baseline.items[i];
                if (strcmp(base->skill, job->skill) == 0) {
                    *base = job->confirmed_merge;
                    break;
                }
            }
            job->refreshed = collect_drift(backend, job);
        }
        sync_backend_destroy(backend);
        return;
    }
    job->result = job->action == SYNC_CHECK ? 0 : transfer(backend, job);
    /* 无写入时复用已完成的检查，避免再次下载远端清单。 */
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
    if (merge_action(action) && s->sync_cfg.type != SYNC_BACKEND_GIT) return;
    bool existing_draft = action == SYNC_MERGE_STAGE || action == SYNC_MERGE_PUBLISH;
    if (skill && !existing_draft && !s->sync_drift_loaded) {
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
    if (skill) snprintf(job->skill, sizeof(job->skill), "%s", skill);
    if (skill && !existing_draft) {
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
    }
    snprintf(job->local, sizeof(job->local), "%s", s->setting_hub_dir);
    if (action != SYNC_PROBE && load_baseline(s, job) != 0) {
        free(job);
        app_set_toast(s, "无法读取同步基线");
        return;
    }
    if (merge_action(action)) {
        if (sync_git_draft_load(s->db, job->scope, job->skill, &job->draft) != 0 ||
            existing_draft != (job->draft.workspace[0] != '\0')) {
            free(job->baseline.items);
            free(job);
            app_set_toast(s, "草稿已变化，请重新打开冲突详情");
            return;
        }
        snprintf(job->previous_workspace, sizeof(job->previous_workspace), "%s", job->draft.workspace);
    }

    const char *title = action == SYNC_PROBE ? "正在测试远端连接"
                      : action == SYNC_CHECK ? "正在检查远端变动"
                      : action == SYNC_MERGE_PREPARE ? "正在准备 Git 合并"
                      : action == SYNC_MERGE_STAGE ? "正在暂存解决结果"
                      : action == SYNC_MERGE_PUBLISH ? "正在发布 Git 合并"
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

/* 收回工作线程结果的所有权，并仅记录两端已验证一致的指纹。 */
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
    s->sync_drafts_loaded = false;
    s->show_sync_conflict_modal = false;
    s->sync_status_msg[0] = '\0';
    return 0;
}

static int current_git_scope(app_state_t *s, char scope[SYNC_SCOPE_CAP]) {
    if (s->sync_cfg.type != SYNC_BACKEND_GIT) return -1;
    return sync_baseline_scope(&s->sync_cfg, s->setting_hub_dir, scope, SYNC_SCOPE_CAP);
}

void app_sync_refresh_git_drafts(app_state_t *s) {
    char scope[SYNC_SCOPE_CAP];
    s->sync_pending_merge_skill[0] = '\0';
    if (current_git_scope(s, scope) == 0)
        sync_git_draft_first(s->db, scope, s->sync_pending_merge_skill);
    s->sync_drafts_loaded = true;
}

void app_sync_open_conflict(app_state_t *s, const char *skill) {
    char scope[SYNC_SCOPE_CAP];
    s->sync_conflict_draft = (sync_git_draft_t){0};
    if (current_git_scope(s, scope) == 0 &&
        sync_git_draft_load(s->db, scope, skill, &s->sync_conflict_draft) != 0) {
        app_set_toast(s, "无法读取 Git 合并草稿");
        return;
    }
    snprintf(s->sync_conflict_skill, sizeof(s->sync_conflict_skill), "%s", skill);
    s->show_sync_conflict_modal = true;
}

void app_sync_discard_git_merge(app_state_t *s, const char *skill) {
    char scope[SYNC_SCOPE_CAP];
    const sync_git_draft_t empty = {0};
    if (s->sync_job || !s->sync_conflict_draft.workspace[0] || current_git_scope(s, scope) != 0 ||
        sync_git_draft_save(s->db, scope, skill, s->sync_conflict_draft.workspace, &empty) != 0) {
        app_set_toast(s, "无法解除草稿关联");
        return;
    }
    s->show_sync_conflict_modal = false;
    s->sync_conflict_draft = empty;
    app_sync_refresh_git_drafts(s);
    app_set_toast(s, "已解除关联，原工作区保留");
}

static int persist_merge(app_state_t *s, const app_sync_job_t *job) {
    int rc = 0;
    if (job->confirmed_merge.revision[0]) {
        sync_drift_item_t item = {.status = SYNC_DRIFT_IN_SYNC};
        sync_drift_summary_t drift = {.items = &item, .count = 1, .in_sync_count = 1};
        snprintf(item.skill_name, sizeof(item.skill_name), "%s", job->confirmed_merge.skill);
        snprintf(item.local_hash, sizeof(item.local_hash), "%s", job->confirmed_merge.hash);
        snprintf(drift.remote_revision, sizeof(drift.remote_revision), "%s", job->confirmed_merge.revision);
        rc = sync_baseline_record(s->db, job->scope, &drift);
    }
    int saved = sync_git_draft_save(s->db, job->scope, job->skill, job->previous_workspace, &job->draft);
    return rc == 0 ? saved : rc;
}

static void finish_merge(app_state_t *s, app_sync_job_t *job) {
    int saved = persist_merge(s, job);
    app_sync_refresh_git_drafts(s);
    if (job->action == SYNC_MERGE_PUBLISH && job->result == 0) {
        apply_drift(s, job);
        app_storage_invalidate(s);
        start_hub_scan_thread(s);
    }
    if (job->draft.workspace[0]) {
        s->sync_conflict_draft = job->draft;
        snprintf(s->sync_conflict_skill, sizeof(s->sync_conflict_skill), "%s", job->skill);
        s->show_sync_conflict_modal = true;
    } else if (job->result == 0) {
        s->show_sync_conflict_modal = false;
    }
    const char *message = saved != 0 ? "合并状态未能写入数据库，请检查当前版本"
        : job->result != 0 ? (job->error[0] ? job->error : "Git 合并未完成")
        : job->action == SYNC_MERGE_PUBLISH ? (job->refreshed == 0 ? "已发布合并结果并同步到本地"
                                                                : "已发布并同步到本地，远端状态暂未刷新")
        : job->action == SYNC_MERGE_STAGE ? "解决结果已暂存，可以发布"
        : job->draft.conflicts[0] ? "请解决列出的文件冲突，再发布" : "合并草稿已准备，确认后发布";
    snprintf(s->sync_status_msg, sizeof(s->sync_status_msg), "%s", job->result != 0 ? message : "");
    app_set_toast(s, message);
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
    } else if (merge_action(job->action)) {
        finish_merge(s, job);
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
        if (merge_action(s->sync_job->action)) {
            persist_merge(s, s->sync_job);
            if (s->sync_job->action == SYNC_MERGE_PUBLISH && s->sync_job->result == 0 &&
                s->sync_job->refreshed == 0)
                sync_baseline_record(s->db, s->sync_job->scope, &s->sync_job->drift);
        }
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
void start_sync_git_merge_thread(app_state_t *s, const char *skill, bool publish) {
    if (skill && skill[0]) start_sync(s, publish ? SYNC_MERGE_PUBLISH : SYNC_MERGE_PREPARE, skill, false);
}
void start_sync_git_merge_stage_thread(app_state_t *s, const char *skill) {
    if (skill && skill[0]) start_sync(s, SYNC_MERGE_STAGE, skill, false);
}
void start_sync_pull_all_thread(app_state_t *s) { start_sync(s, SYNC_PULL, NULL, false); }
void start_sync_push_all_thread(app_state_t *s) { start_sync(s, SYNC_PUSH, NULL, false); }
