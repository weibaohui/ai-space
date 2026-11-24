#ifndef AI_SYNC_BASELINE_H
#define AI_SYNC_BASELINE_H

#include "sync_backend.h"
#include <sqlite3.h>

#define SYNC_SCOPE_CAP 2600

int sync_baseline_scope(const sync_config_t *cfg, const char *local, char *out, size_t cap);
int sync_baseline_load(sqlite3 *db, const char *scope, sync_baseline_t *baseline);
int sync_baseline_record(sqlite3 *db, const char *scope, const sync_drift_summary_t *drift);

#endif
