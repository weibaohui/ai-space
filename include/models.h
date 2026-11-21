#ifndef MODELS_H
#define MODELS_H

#include <stdbool.h>

/* A single tool record stored in the database table `local_tools` */
typedef struct {
    int id;
    char name[128];             /* tool identifier 'xxx' from ~/.xxx/skills */
    char path[512];             /* directory path */
    int skill_count;            /* number of skills in skills/ directory */
    char created_at[32];        /* discovery timestamp */
    char updated_at[32];        /* last scan/update timestamp */
} tool_record_t;

/* Statistical summary queried from the database */
typedef struct {
    int total_tools;            /* total tools in DB */
    int total_skills;           /* sum of skill_count across all tools */
    char last_scanned_at[32];   /* latest scan time */
} tool_stats_t;

/* Intermediate structure during disk scanning */
typedef struct {
    char name[128];
    char path[512];
    int skill_count;
} discovered_tool_t;

#endif /* MODELS_H */
