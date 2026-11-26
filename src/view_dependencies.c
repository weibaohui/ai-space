#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include "portability.h"
#include "view_dependencies.h"
#include "view_common.h"
#include "app_state.h"

/* Helper to strip trailing newlines and whitespace */
static void strip_trailing_whitespace(char *str) {
    if (!str) return;
    size_t len = strlen(str);
    while (len > 0 && (str[len - 1] == '\n' || str[len - 1] == '\r' || str[len - 1] == ' ' || str[len - 1] == '\t')) {
        str[--len] = '\0';
    }
}

/* Check a single binary using popen */
static bool probe_binary(const char *bin_name, char *out_path, size_t path_cap,
                         char *out_ver, size_t ver_cap) {
    if (!bin_name || !bin_name[0]) return false;
    if (out_path && path_cap > 0) out_path[0] = '\0';
    if (out_ver && ver_cap > 0) out_ver[0] = '\0';

    char check_cmd[256];
#ifdef _WIN32
    snprintf(check_cmd, sizeof(check_cmd), "where %s 2>nul", bin_name);
#else
    snprintf(check_cmd, sizeof(check_cmd), "which %s 2>/dev/null", bin_name);
#endif

    FILE *fp = AI_POPEN(check_cmd, "r");
    if (!fp) return false;

    char resolved_path[256] = "";
    if (!fgets(resolved_path, sizeof(resolved_path), fp)) {
        AI_PCLOSE(fp);
        return false;
    }
    int rc = AI_PCLOSE(fp);
    strip_trailing_whitespace(resolved_path);

    if (rc != 0 || resolved_path[0] == '\0') {
        return false;
    }

    if (out_path && path_cap > 0) {
        strncpy(out_path, resolved_path, path_cap - 1);
        out_path[path_cap - 1] = '\0';
    }

    /* Probe version */
    char ver_cmd[256];
    if (strcmp(bin_name, "node") == 0) {
        snprintf(ver_cmd, sizeof(ver_cmd), "%s -v 2>&1", resolved_path);
    } else {
        snprintf(ver_cmd, sizeof(ver_cmd), "%s --version 2>&1", resolved_path);
    }

    FILE *vp = AI_POPEN(ver_cmd, "r");
    if (vp) {
        char ver_str[256] = "";
        if (fgets(ver_str, sizeof(ver_str), vp)) {
            strip_trailing_whitespace(ver_str);
            if (out_ver && ver_cap > 0) {
                strncpy(out_ver, ver_str, ver_cap - 1);
                out_ver[ver_cap - 1] = '\0';
            }
        }
        AI_PCLOSE(vp);
    }

    return true;
}

/* Generate ready-to-copy AI installation prompt */
static void build_ai_prompt(dep_check_state_t *st) {
    if (!st) return;

    const char *os_name = "macOS (Darwin)";
#ifdef _WIN32
    os_name = "Windows";
#elif defined(__linux__)
    os_name = "Linux";
#elif defined(__APPLE__)
    os_name = "macOS (Darwin)";
#endif

    char buf[2048] = "";
    size_t off = 0;

    int total_missing = st->required_missing_count + st->recommended_missing_count;

    if (total_missing > 0) {
        off += snprintf(buf + off, sizeof(buf) - off,
                        "请帮我安装并配置 SkillBoard 所缺失的本地运行环境依赖：\n\n"
                        "【操作系统环境】\n"
                        "%s\n\n"
                        "【检测到缺失的软件清单】\n", os_name);

        for (int i = 0; i < st->count; i++) {
            dep_item_t *it = &st->items[i];
            if (!it->installed) {
                const char *sev_str = (it->severity == DEP_SEVERITY_REQUIRED) ? "核心必须" : "推荐环境";
                off += snprintf(buf + off, sizeof(buf) - off,
                                "- %s (%s) [%s]: %s\n  推荐安装方式: %s\n",
                                it->display_name, it->binary_name, sev_str, it->purpose, it->install_hint);
            }
        }

        off += snprintf(buf + off, sizeof(buf) - off,
                        "\n【配置指导要求】\n"
                        "1. 检查当前系统的包管理器与配置环境（例如 macOS 优先使用 Homebrew，Linux 使用 apt/yum，Windows 使用 winget 等）；\n"
                        "2. 给出安全、标准的终端安装命令并协助执行；\n"
                        "3. 确保安装后的二进制路径已正确配置到系统环境变量 PATH 中；\n"
                        "4. 安装完成后，分别输出工具的版本号（如 git --version、curl --version、node -v 等）以验证安装成功。");
    } else {
        off += snprintf(buf + off, sizeof(buf) - off,
                        "【SkillBoard 环境检测健康报告】\n\n"
                        "当前操作系统：%s\n"
                        "SkillBoard 运行所需的所有核心依赖与推荐环境已全部就绪：\n\n", os_name);

        for (int i = 0; i < st->count; i++) {
            dep_item_t *it = &st->items[i];
            off += snprintf(buf + off, sizeof(buf) - off,
                            "- %s (%s): 已安装 [%s] - %s\n",
                            it->display_name, it->binary_name,
                            it->version[0] ? it->version : "正常",
                            it->path);
        }

        off += snprintf(buf + off, sizeof(buf) - off,
                        "\n所有技能调度、主仓同步与远端管理功能均可正常无碍运行。无需额外安装依赖！");
    }

    strncpy(st->generated_prompt, buf, sizeof(st->generated_prompt) - 1);
    st->generated_prompt[sizeof(st->generated_prompt) - 1] = '\0';
}

/* Execute full dependency check */
void app_check_dependencies(app_state_t *s) {
    if (!s) return;
    dep_check_state_t *st = &s->dep_state;
    memset(st, 0, sizeof(dep_check_state_t));

    /* Define dependency items */
    dep_item_t defs[] = {
        {
            .binary_name = "git",
            .display_name = "Git 版本控制",
            .category = "核心版本协同",
            .purpose = "技能版本管理、主仓基准库协同、远端拉取/推送与差异对比",
            .severity = DEP_SEVERITY_REQUIRED,
            .install_hint = "brew install git (macOS) / apt install git (Linux)"
        },
        {
            .binary_name = "curl",
            .display_name = "cURL 传输工具",
            .category = "远端网络服务",
            .purpose = "WebDAV 远端同步服务连通性探测、HTTP 状态检测与数据通信",
            .severity = DEP_SEVERITY_REQUIRED,
            .install_hint = "brew install curl (macOS) / apt install curl (Linux)"
        },
        {
            .binary_name = "sqlite3",
            .display_name = "SQLite3 终端",
            .category = "数据存储诊断",
            .purpose = "本地数据引擎命令行终端，便于数据库日常维护与元数据调试",
            .severity = DEP_SEVERITY_RECOMMENDED,
            .install_hint = "brew install sqlite (macOS) / apt install sqlite3 (Linux)"
        },
        {
            .binary_name = "node",
            .display_name = "Node.js 运行时",
            .category = "技能运行环境",
            .purpose = "运行现代 AI 技能生态（如 MCP 插件、TypeScript/JavaScript 脚本工具）",
            .severity = DEP_SEVERITY_RECOMMENDED,
            .install_hint = "brew install node (macOS) / apt install nodejs (Linux)"
        },
        {
            .binary_name = "python3",
            .display_name = "Python 3 解释器",
            .category = "技能运行环境",
            .purpose = "运行 Python 编写的 AI 生成工具、自动化测试与数据处理技能",
            .severity = DEP_SEVERITY_RECOMMENDED,
            .install_hint = "brew install python (macOS) / apt install python3 (Linux)"
        },
        {
            .binary_name = "claude",
            .display_name = "Claude Code CLI",
            .category = "AI 宿主工具",
            .purpose = "Anthropic 官方终端 Agent 工具，可无缝读取并调度本地技能库",
            .severity = DEP_SEVERITY_RECOMMENDED,
            .install_hint = "npm install -g @anthropic-ai/claude-code"
        }
    };

    st->count = sizeof(defs) / sizeof(defs[0]);
    st->required_missing_count = 0;
    st->recommended_missing_count = 0;

    for (int i = 0; i < st->count; i++) {
        st->items[i] = defs[i];
        bool ok = probe_binary(st->items[i].binary_name,
                               st->items[i].path, sizeof(st->items[i].path),
                               st->items[i].version, sizeof(st->items[i].version));
        st->items[i].installed = ok;
        if (!ok) {
            if (st->items[i].severity == DEP_SEVERITY_REQUIRED) {
                st->required_missing_count++;
            } else {
                st->recommended_missing_count++;
            }
        }
    }

    build_ai_prompt(st);

    time_t now = time(NULL);
    struct tm tm_info;
    ai_localtime(&now, &tm_info);
    strftime(st->last_checked_at, sizeof(st->last_checked_at), "%Y-%m-%d %H:%M:%S", &tm_info);

    st->loaded = true;
}

/* -------------------- Page UI Rendering -------------------- */

static void render_dependency_row(cui_ctx *ui, const dep_item_t *item) {
    bool required = item->severity == DEP_SEVERITY_REQUIRED;
    const char *status = item->installed ? "就绪" : required ? "缺失" : "未安装";
    cui_tone tone = item->installed ? CUI_TONE_SUCCESS : required ? CUI_TONE_DANGER : CUI_TONE_WARNING;
    cui_color color = item->installed ? CUI_COLOR_SUCCESS : required ? CUI_COLOR_DANGER : CUI_COLOR_WARNING;

    cui_card(ui, .width = CUI_FILL, .pad = 12) {
        cui_row(ui, .width = CUI_FILL, .gap = 10, .align = CUI_ALIGN_CENTER) {
            cui_icon(ui, item->installed ? CUI_ICON_CIRCLE_CHECK : CUI_ICON_TRIANGLE_ALERT, 17, color);
            cui_column(ui, .width = CUI_FILL, .gap = 4) {
                cui_row(ui, .width = CUI_FILL, .gap = 8, .align = CUI_ALIGN_CENTER) {
                    cui_text(ui, item->display_name, .font = CUI_FONT_MEDIUM);
                    cui_badge(ui, required ? "必需" : "推荐", .tone = CUI_TONE_NEUTRAL, .size = CUI_SIZE_SM);
                }
                char detail[768];
                if (item->installed && item->version[0]) {
                    snprintf(detail, sizeof(detail), "%s · %s", item->path, item->version);
                } else {
                    snprintf(detail, sizeof(detail), "%s", item->installed ? item->path : item->install_hint);
                }
                cui_text(ui, detail, .width = CUI_FILL, .nowrap = true, .font = CUI_FONT_MONO,
                         .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);

                char tooltip[1280];
                snprintf(tooltip, sizeof(tooltip), "%s\n%s", item->purpose, detail);
                cui_tooltip(ui, tooltip);
            }
            cui_badge(ui, status, .tone = tone, .size = CUI_SIZE_SM);
        }
    }
}

void render_dependencies_page(cui_ctx *ui, app_state_t *s) {
    if (!s->dep_state.loaded) app_check_dependencies(s);
    const dep_check_state_t *st = &s->dep_state;

    int required = 0, recommended = 0;
    for (int i = 0; i < st->count; i++) {
        if (st->items[i].severity == DEP_SEVERITY_REQUIRED) required++;
        else recommended++;
    }

    int missing = st->required_missing_count + st->recommended_missing_count;
    char req_value[24], rec_value[24], health[24];
    snprintf(req_value, sizeof(req_value), "%d / %d", required - st->required_missing_count, required);
    snprintf(rec_value, sizeof(rec_value), "%d / %d", recommended - st->recommended_missing_count, recommended);
    snprintf(health, sizeof(health), "%d%%", st->count ? (st->count - missing) * 100 / st->count : 0);
    cui_column(ui, .width = CUI_FILL, .gap = 14) {
        cui_row(ui, .width = CUI_FILL, .gap = 12, .align = CUI_ALIGN_CENTER) {
            render_workspace_heading(ui, CUI_ICON_SHIELD, "依赖检测", "检查系统依赖和技能运行环境");
            if (cui_button(ui, "重新检测", .variant = CUI_BUTTON_SECONDARY, .icon = CUI_ICON_REFRESH_CW)) {
                app_check_dependencies(s);
                cui_toast(ui, "依赖检测已更新", .tone = CUI_TONE_SUCCESS);
            }
        }

        cui_row(ui, .width = CUI_FILL, .gap = 12) {
            cui_metric(ui, "必需依赖", req_value, .icon = CUI_ICON_SHIELD, .width = CUI_GROW(1));
            cui_metric(ui, "推荐环境", rec_value, .icon = CUI_ICON_PACKAGE, .width = CUI_GROW(1));
            cui_metric(ui, "就绪比例", health, .icon = CUI_ICON_ACTIVITY, .width = CUI_GROW(1));
        }

        static bool report_open;
        cui_accordion(ui, "诊断报告与安装提示", &report_open) {
            cui_column(ui, .width = CUI_FILL, .gap = 8, .pad = 12) {
                if (cui_button(ui, "复制报告", .variant = CUI_BUTTON_SECONDARY,
                               .icon = CUI_ICON_COPY, .size = CUI_SIZE_SM)) {
                    cui_set_clipboard_text(ui, st->generated_prompt);
                    cui_toast(ui, "诊断报告已复制", .tone = CUI_TONE_SUCCESS);
                }
                cui_code_block(ui, "dependency_report", st->generated_prompt, .lang = "text");
            }
        }

        cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
            cui_heading(ui, "依赖项", 4);
            cui_spacer(ui);
            char time[64];
            snprintf(time, sizeof(time), "上次检测  %s", st->last_checked_at);
            cui_text(ui, time, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);
        }
        for (int group = 0; group < 2; group++) {
            for (int i = 0; i < st->count; i++) {
                if (st->items[i].installed == (group == 1)) {
                    render_dependency_row(ui, &st->items[i]);
                }
            }
        }
    }
}
