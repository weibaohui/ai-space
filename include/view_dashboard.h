#ifndef VIEW_DASHBOARD_H
#define VIEW_DASHBOARD_H

#include <cui/cui.h>
#include "app_state.h"

/* Render global top status bar (Window Title, App Info, Node status, Clock) */
void render_app_top_bar(cui_ctx *ui, app_state_t *s);

/* Render full Dashboard Page (Hero Banner, Topology Diagram, Tasks, Actions, KPIs) */
void render_dashboard_page(cui_ctx *ui, app_state_t *s);

#endif /* VIEW_DASHBOARD_H */
