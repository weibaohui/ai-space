#ifndef AI_SYNC_NETWORK_SHARE_H
#define AI_SYNC_NETWORK_SHARE_H

#include "sync_backend.h"

int sync_network_check(const sync_config_t *cfg, char *error, size_t cap);
int sync_network_fetch(const sync_config_t *cfg, sync_remote_manifest_t *manifest,
                       char *error, size_t cap);
int sync_network_transfer(const sync_config_t *cfg, const char *name, const char *local,
                          bool pull, const sync_drift_item_t *expected, sync_progress_cb cb, void *user);

#endif
