#ifndef HUB_FILES_H
#define HUB_FILES_H


int hub_promote_to_master(const char *tool_skills_path, const char *skill_name);
int hub_sync_master_to_tool(const char *tool_skills_path, const char *skill_name, const char *tool_name);
int hub_uninstall_skill_from_tool(const char *tool_skills_path, const char *skill_name, const char *tool_name);
int hub_delete_master_skill(const char *skill_name);

#endif
