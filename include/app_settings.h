#ifndef APP_SETTINGS_H
#define APP_SETTINGS_H

#include "app_state.h"

void app_init_settings(app_state_t *s);
void app_save_settings(app_state_t *s);
void app_save_storage_settings(app_state_t *s);
int app_store_sync_settings(sqlite3 *db, const sync_config_t *cfg);

#endif
