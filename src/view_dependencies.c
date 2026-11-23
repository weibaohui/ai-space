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
                        "请帮我安装并配置 AI-Space 所缺失的本地运行环境依赖：\n\n"
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
                        "【AI-Space 环境检测健康报告】\n\n"
                        "当前操作系统：%s\n"
                        "AI-Space 运行所需的所有核心依赖与推荐环境已全部就绪：\n\n", os_name);

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

void render_dependencies_page(cui_ctx *ui, app_state_t *s) {
    if (!ui || !s) return;

    if (!s->dep_state.loaded) {
        app_check_dependencies(s);
    }

    dep_check_state_t *st = &s->dep_state;

    int total_req = 0;
    int ok_req = 0;
    int total_rec = 0;
    int ok_rec = 0;

    for (int i = 0; i < st->count; i++) {
        if (st->items[i].severity == DEP_SEVERITY_REQUIRED) {
            total_req++;
            if (st->items[i].installed) ok_req++;
        } else {
            total_rec++;
            if (st->items[i].installed) ok_rec++;
        }
    }

    bool all_good = (st->required_missing_count == 0 && st->recommended_missing_count == 0);
    bool req_good = (st->required_missing_count == 0);

    cui_column(ui, .width = CUI_FILL, .gap = 16) {

        /* 1. Header Row */
        cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
            cui_row(ui, .gap = 12, .align = CUI_ALIGN_CENTER) {
                cui_box(ui, .width = CUI_PX(40), .height = CUI_PX(40), .radius = 8,
                        .bg = cui_color_of(ui, CUI_COLOR_ACCENT),
                        .dir = CUI_STACK, .align = CUI_ALIGN_CENTER, .justify = CUI_JUSTIFY_CENTER) {
                    cui_icon(ui, CUI_ICON_SHIELD, 20, CUI_COLOR_BACKGROUND);
                }
                cui_column(ui, .gap = 2) {
                    cui_heading(ui, "运行环境与依赖检测", 2);
                    cui_text(ui, "检测系统底层运行依赖与 AI 运行时环境，保障技能调度与多端同步正常运行",
                             .size = 13, .color = CUI_COLOR_TEXT_SECONDARY);
                }
            }

            cui_spacer(ui);

            cui_row(ui, .gap = 10, .align = CUI_ALIGN_CENTER) {
                if (cui_button(ui, "重新检测", .variant = CUI_BUTTON_SECONDARY, .icon = CUI_ICON_REFRESH_CW)) {
                    app_check_dependencies(s);
                    app_set_toast(s, "依赖检测已更新");
                }

                if (cui_button(ui, "复制提示", .variant = CUI_BUTTON_PRIMARY, .icon = CUI_ICON_COPY)) {
                    cui_set_clipboard_text(ui, st->generated_prompt);
                    app_set_toast(s, "AI 安装提示词已复制到剪贴板");
                }
            }
        }

        /* 2. Top Summary Metrics Row (3 Cards) */
        cui_row(ui, .width = CUI_FILL, .gap = 12) {
            /* Card 1: Core Required */
            cui_card(ui, .width = CUI_GROW(1), .pad = 16, .gap = 8) {
                cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                    cui_row(ui, .gap = 6, .align = CUI_ALIGN_CENTER) {
                        cui_icon(ui, req_good ? CUI_ICON_CHECK : CUI_ICON_TRIANGLE_ALERT,
                                 16, req_good ? CUI_COLOR_SUCCESS : CUI_COLOR_DANGER);
                        cui_text(ui, "核心必须依赖", .size = 12, .color = CUI_COLOR_TEXT_MUTED);
                    }
                    cui_spacer(ui);
                    cui_badge(ui, req_good ? "全部就绪" : "存在缺失",
                              .tone = req_good ? CUI_TONE_SUCCESS : CUI_TONE_DANGER,
                              .size = CUI_SIZE_SM);
                }
                char req_txt[32];
                snprintf(req_txt, sizeof(req_txt), "%d / %d 就绪", ok_req, total_req);
                cui_text(ui, req_txt, .font = CUI_FONT_DISPLAY, .size = 24);
                cui_text(ui, "Git、cURL 核心协同与网络通信", .size = 11, .color = CUI_COLOR_TEXT_MUTED);
            }

            /* Card 2: Recommended Environments */
            cui_card(ui, .width = CUI_GROW(1), .pad = 16, .gap = 8) {
                cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                    cui_row(ui, .gap = 6, .align = CUI_ALIGN_CENTER) {
                        cui_icon(ui, CUI_ICON_PACKAGE, 16, CUI_COLOR_ACCENT);
                        cui_text(ui, "推荐运行环境", .size = 12, .color = CUI_COLOR_TEXT_MUTED);
                    }
                    cui_spacer(ui);
                    cui_badge(ui, (ok_rec == total_rec) ? "全部就绪" : "建议配置",
                              .tone = (ok_rec == total_rec) ? CUI_TONE_SUCCESS : CUI_TONE_NEUTRAL,
                              .size = CUI_SIZE_SM);
                }
                char rec_txt[32];
                snprintf(rec_txt, sizeof(rec_txt), "%d / %d 就绪", ok_rec, total_rec);
                cui_text(ui, rec_txt, .font = CUI_FONT_DISPLAY, .size = 24);
                cui_text(ui, "Node.js、Python 等技能运行时", .size = 11, .color = CUI_COLOR_TEXT_MUTED);
            }

            /* Card 3: Overall Status */
            cui_card(ui, .width = CUI_GROW(1), .pad = 16, .gap = 8) {
                cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                    cui_row(ui, .gap = 6, .align = CUI_ALIGN_CENTER) {
                        cui_icon(ui, CUI_ICON_ACTIVITY, 16, CUI_COLOR_ACCENT);
                        cui_text(ui, "环境健康评估", .size = 12, .color = CUI_COLOR_TEXT_MUTED);
                    }
                    cui_spacer(ui);
                    cui_badge(ui, all_good ? "优良" : (req_good ? "基本正常" : "需修复"),
                              .tone = all_good ? CUI_TONE_SUCCESS : (req_good ? CUI_TONE_WARNING : CUI_TONE_DANGER),
                              .size = CUI_SIZE_SM);
                }
                int pct = (st->count > 0) ? ((ok_req + ok_rec) * 100 / st->count) : 0;
                char pct_txt[32];
                snprintf(pct_txt, sizeof(pct_txt), "%d%%", pct);
                cui_text(ui, pct_txt, .font = CUI_FONT_DISPLAY, .size = 24);
                char time_lbl[64];
                snprintf(time_lbl, sizeof(time_lbl), "检测于: %s", st->last_checked_at);
                cui_text(ui, time_lbl, .size = 11, .color = CUI_COLOR_TEXT_MUTED);
            }
        }

        /* 3. Section: AI Installation Prompt Card */
        cui_box(ui, .width = CUI_FILL, .radius = 8, .pad = 16, .gap = 12,
                .bg = cui_color_of(ui, CUI_COLOR_SURFACE_RAISED),
                .border = 1, .border_color = cui_color_of(ui, CUI_COLOR_BORDER)) {

            cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                    cui_icon(ui, CUI_ICON_SPARKLES, 18, CUI_COLOR_ACCENT);
                    cui_heading(ui, "AI 工具安装提示词", 4);
                }
                cui_spacer(ui);
                cui_badge(ui, (st->required_missing_count > 0 || st->recommended_missing_count > 0)
                                  ? "一键修复提示词已生成" : "依赖已就绪",
                          .tone = (st->required_missing_count > 0) ? CUI_TONE_DANGER : CUI_TONE_SUCCESS,
                          .size = CUI_SIZE_SM);
            }

            cui_text(ui, "当检测到依赖缺失时，系统已自动组装好针对当前操作系统的配置安装提示词。点击下方按钮一键复制，直接发送给 AI 助手（如 Claude Code、Cursor、ChatGPT 等）即可自动完成配置安装。",
                     .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);

            /* Action row */
            cui_row(ui, .gap = 10, .align = CUI_ALIGN_CENTER) {
                if (cui_button(ui, "复制提示", .variant = CUI_BUTTON_PRIMARY, .icon = CUI_ICON_COPY)) {
                    cui_set_clipboard_text(ui, st->generated_prompt);
                    app_set_toast(s, "AI 安装提示词已成功复制到剪贴板！");
                }
            }

            /* Prompt Text Box Preview */
            cui_box(ui, .width = CUI_FILL, .radius = 6, .pad = 12, .gap = 6,
                    .bg = cui_alpha(cui_color_of(ui, CUI_COLOR_SURFACE), 0.7f),
                    .border = 1, .border_color = cui_color_of(ui, CUI_COLOR_BORDER)) {
                cui_row(ui, .gap = 6, .align = CUI_ALIGN_CENTER) {
                    cui_icon(ui, CUI_ICON_TERMINAL, 13, CUI_COLOR_TEXT_MUTED);
                    cui_text(ui, "生成的提示词内容预览 (可直接点击上方按钮复制):",
                             .size = 11, .color = CUI_COLOR_TEXT_MUTED);
                }
                cui_text(ui, st->generated_prompt,
                         .font = CUI_FONT_MONO, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);
            }
        }

        /* 4. Section: Dependency Items Detailed Table */
        cui_box(ui, .width = CUI_FILL, .radius = 8, .pad = 16, .gap = 12,
                .bg = cui_color_of(ui, CUI_COLOR_SURFACE_RAISED),
                .border = 1, .border_color = cui_color_of(ui, CUI_COLOR_BORDER)) {

            cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                    cui_icon(ui, CUI_ICON_TERMINAL, 18, CUI_COLOR_ACCENT);
                    cui_heading(ui, "底层依赖项状态明细", 4);
                }
                cui_spacer(ui);
                char cnt_lbl[64];
                snprintf(cnt_lbl, sizeof(cnt_lbl), "共检测 %d 项软件", st->count);
                cui_text(ui, cnt_lbl, .size = 12, .color = CUI_COLOR_TEXT_MUTED);
            }

            for (int i = 0; i < st->count; i++) {
                dep_item_t *item = &st->items[i];

                cui_box(ui, .width = CUI_FILL, .radius = 6, .pad = 12,
                        .bg = cui_color_of(ui, CUI_COLOR_SURFACE),
                        .border = 1,
                        .border_color = item->installed ? cui_color_of(ui, CUI_COLOR_BORDER)
                                                        : cui_alpha(cui_color_of(ui, CUI_COLOR_DANGER), 0.4f)) {

                    cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                        /* Status Icon */
                        cui_icon(ui, item->installed ? CUI_ICON_CHECK : CUI_ICON_TRIANGLE_ALERT,
                                 18, item->installed ? CUI_COLOR_SUCCESS : CUI_COLOR_DANGER);

                        /* Name & Tag */
                        cui_column(ui, .gap = 3, .width = CUI_PX(240)) {
                            cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                                cui_text(ui, item->display_name, .size = 14, .font = CUI_FONT_MEDIUM);
                                cui_badge(ui, (item->severity == DEP_SEVERITY_REQUIRED) ? "必须" : "推荐",
                                          .tone = (item->severity == DEP_SEVERITY_REQUIRED) ? CUI_TONE_ACCENT : CUI_TONE_NEUTRAL,
                                          .size = CUI_SIZE_SM);
                            }
                            cui_text(ui, item->purpose, .size = 11, .color = CUI_COLOR_TEXT_MUTED);
                        }

                        /* Path / Version / Hint */
                        cui_column(ui, .gap = 3, .width = CUI_GROW(1)) {
                            if (item->installed) {
                                cui_row(ui, .gap = 6, .align = CUI_ALIGN_CENTER) {
                                    cui_text(ui, "路径:", .size = 11, .color = CUI_COLOR_TEXT_MUTED);
                                    cui_text(ui, item->path, .size = 12, .font = CUI_FONT_MONO, .color = CUI_COLOR_FOREGROUND);
                                }
                                if (item->version[0] != '\0') {
                                    cui_row(ui, .gap = 6, .align = CUI_ALIGN_CENTER) {
                                        cui_text(ui, "版本:", .size = 11, .color = CUI_COLOR_TEXT_MUTED);
                                        cui_text(ui, item->version, .size = 11, .color = CUI_COLOR_TEXT_SECONDARY);
                                    }
                                }
                            } else {
                                cui_row(ui, .gap = 6, .align = CUI_ALIGN_CENTER) {
                                    cui_text(ui, "未检测到此软件，安装建议:", .size = 11, .color = CUI_COLOR_DANGER);
                                    cui_text(ui, item->install_hint, .size = 12, .font = CUI_FONT_MONO, .color = CUI_COLOR_FOREGROUND);
                                }
                            }
                        }

                        cui_spacer(ui);

                        /* Right Status Badge & Copy Hint */
                        cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                            if (item->installed) {
                                cui_badge(ui, "已就绪", .tone = CUI_TONE_SUCCESS, .size = CUI_SIZE_SM);
                            } else {
                                cui_badge(ui, (item->severity == DEP_SEVERITY_REQUIRED) ? "缺失 (必须)" : "未安装",
                                          .tone = (item->severity == DEP_SEVERITY_REQUIRED) ? CUI_TONE_DANGER : CUI_TONE_WARNING,
                                          .size = CUI_SIZE_SM);
                            }
                        }
                    }
                }
            }
        }
    }
}
