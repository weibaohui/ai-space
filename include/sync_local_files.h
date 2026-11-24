#ifndef AI_SYNC_LOCAL_FILES_H
#define AI_SYNC_LOCAL_FILES_H

#include "sync_backend.h"

/* 空指纹表示目标必须不存在，读取错误和非技能目录均视为校验失败。 */
bool sync_skill_matches(const char *path, const char *expected_hash);

/* 发布前检查暂存副本和现有目标，调用方负责清理暂存目录。 */
int sync_publish_local_skill(const char *stage, const char *replacement, const char *target,
                             const sync_drift_item_t *expected, bool pull);

/* 用同一套校验和替换流程处理 Git 缓存及挂载目录之间的复制。 */
int sync_copy_local_skill(const char *local, const char *remote, const char *name,
                          const sync_drift_item_t *expected, bool pull);

#endif
