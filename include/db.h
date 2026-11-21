#ifndef DB_H
#define DB_H

#include <sqlite3.h>
#include "models.h"

/*
 * Initialize the SQLite database.
 * Creates the `local_tools` table and indices if they do not exist.
 */
int db_init(const char *db_path, sqlite3 **out_db);

/*
 * Close the SQLite database connection.
 */
void db_close(sqlite3 *db);

/*
 * Scan local directory ~/.xxx/skills and synchronize into the `local_tools` table:
 * - Insert newly discovered tools
 * - Update existing tools (path, skill_count, updated_at)
 * - Remove tools from DB if they no longer exist on disk
 *
 * Transactional and thread-safe.
 */
int db_scan_and_sync(sqlite3 *db, int *out_inserted, int *out_updated, int *out_deleted);

/*
 * Query records from the `local_tools` table with optional search keyword and column sorting.
 * search_kw: optional substring match against tool name or path (NULL to ignore)
 * sort_col: 0=id, 1=name, 2=skill_count, 3=path, 4=created_at, 5=updated_at
 * sort_desc: true for descending, false for ascending
 *
 * Allocates an array of tool_record_t in *out_records with *out_count items.
 * Caller must free(*out_records) when finished.
 */
int db_query_tools(sqlite3 *db, tool_record_t **out_records, int *out_count,
                   const char *search_kw, int sort_col, bool sort_desc);

/*
 * Query aggregate statistical summary from the `local_tools` table.
 */
int db_query_stats(sqlite3 *db, tool_stats_t *out_stats);

#endif /* DB_H */
