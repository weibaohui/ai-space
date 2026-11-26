#ifndef HUB_FILES_H
#define HUB_FILES_H

/* 备份目录与技能目录不在同一文件系统，条目无法移入快照。 */
enum { HUB_CLEAR_CROSS_DEVICE = -2 };

int hub_promote_to_master(const char *tool_skills_path, const char *skill_name);
int hub_sync_master_to_tool(const char *tool_skills_path, const char *skill_name, const char *tool_name);
int hub_uninstall_skill_from_tool(const char *tool_skills_path, const char *skill_name, const char *tool_name);
int hub_delete_master_skill(const char *skill_name);

/*
 * 清空工具的技能目录：目录本身保留，目录内的全部条目移入一份整目录快照。
 * 返回 0 表示已全部移出；负值表示未能全部移出，*out_cleared 记录已移出的条目数。
 */
int hub_clear_tool_skills(const char *tool_skills_path, const char *tool_name, int *out_cleared);

#endif
