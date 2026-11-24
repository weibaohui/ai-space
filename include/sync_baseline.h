#ifndef AI_SYNC_BASELINE_H
#define AI_SYNC_BASELINE_H

#include "sync_backend.h"
#include <sqlite3.h>

#define SYNC_SCOPE_CAP 2600

/* 按主仓路径、后端及远端范围隔离记录。 */
int sync_baseline_scope(const sync_config_t *cfg, const char *local, char *out, size_t cap);
/* 调用方拥有加载后的条目数组，使用完毕后释放。 */
int sync_baseline_load(sqlite3 *db, const char *scope, sync_baseline_t *baseline);
int sync_baseline_record(sqlite3 *db, const char *scope, const sync_drift_summary_t *drift);

#endif
