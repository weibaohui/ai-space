#include "hub_paths.h"
#include "portability.h"

#include <string.h>

static const char *get_user_home(void) {
    return ai_home_dir();
}

/* 设置页更新自定义路径，空值表示由主仓模块选用默认路径。 */
static char g_custom_hub_dir[512] = "";
static char g_custom_backup_dir[512] = "";

static void expand_path(const char *in, char *out, size_t cap) {
    if (!in || in[0] == '\0') {
        if (out && cap > 0) out[0] = '\0';
        return;
    }
    if (in[0] == '~' && (in[1] == '/' || in[1] == '\0')) {
        const char *home = get_user_home();
        if (home) {
            snprintf(out, cap, "%s%s", home, in + 1);
            return;
        }
    }
    strncpy(out, in, cap - 1);
    out[cap - 1] = '\0';
}

void hub_set_custom_dirs(const char *hub_dir, const char *backup_dir) {
    if (hub_dir && hub_dir[0] != '\0') {
        expand_path(hub_dir, g_custom_hub_dir, sizeof(g_custom_hub_dir));
    } else {
        g_custom_hub_dir[0] = '\0';
    }
    if (backup_dir && backup_dir[0] != '\0') {
        expand_path(backup_dir, g_custom_backup_dir, sizeof(g_custom_backup_dir));
    } else {
        g_custom_backup_dir[0] = '\0';
    }
}

void hub_get_custom_dirs(char *out_hub_dir, size_t hub_cap, char *out_backup_dir, size_t backup_cap) {
    if (out_hub_dir && hub_cap > 0) {
        strncpy(out_hub_dir, g_custom_hub_dir, hub_cap - 1);
        out_hub_dir[hub_cap - 1] = '\0';
    }
    if (out_backup_dir && backup_cap > 0) {
        strncpy(out_backup_dir, g_custom_backup_dir, backup_cap - 1);
        out_backup_dir[backup_cap - 1] = '\0';
    }
}

int hub_init_dirs(char *out_hub_dir, size_t hub_cap, char *out_backup_dir, size_t backup_cap) {
    const char *home = get_user_home();
    if (!home) return -1;

    char base[512];
    snprintf(base, sizeof(base), "%s/.skillboard", home);
    AI_MKDIR(base);

    char hub[512];
    if (g_custom_hub_dir[0] != '\0') {
        strncpy(hub, g_custom_hub_dir, sizeof(hub) - 1);
        hub[sizeof(hub) - 1] = '\0';
    } else {
        snprintf(hub, sizeof(hub), "%s/.skillboard/skills", home);
    }
    AI_MKDIR(hub);

    char backup[512];
    if (g_custom_backup_dir[0] != '\0') {
        strncpy(backup, g_custom_backup_dir, sizeof(backup) - 1);
        backup[sizeof(backup) - 1] = '\0';
    } else {
        snprintf(backup, sizeof(backup), "%s/.skillboard/backups", home);
    }
    AI_MKDIR(backup);

    if (out_hub_dir && hub_cap > 0) {
        strncpy(out_hub_dir, hub, hub_cap - 1);
        out_hub_dir[hub_cap - 1] = '\0';
    }
    if (out_backup_dir && backup_cap > 0) {
        strncpy(out_backup_dir, backup, backup_cap - 1);
        out_backup_dir[backup_cap - 1] = '\0';
    }
    return 0;
}
