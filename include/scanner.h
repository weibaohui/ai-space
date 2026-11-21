#ifndef SCANNER_H
#define SCANNER_H

#include "models.h"

/*
 * Discovers local tools by scanning $HOME for directories matching ~/.xxx/skills
 *
 * Each directory matching ~/.xxx/skills corresponds to a tool where 'xxx' is the tool name.
 * Also inspects the skills/ directory to count how many skills are present.
 *
 * Returns 0 on success, negative on error.
 * Caller must free(*out_tools) when done.
 */
int scanner_scan(discovered_tool_t **out_tools, int *out_count);

#endif /* SCANNER_H */
