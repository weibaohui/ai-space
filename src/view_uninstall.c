#include "hub_bulk.h"
#include "hub_files.h"
#include "skill_file_tree.h"
#include "view_uninstall.h"
#include "view_common.h"
#include "app_hub_tasks.h"
#include "view_tool_skills.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "portability.h"

void render_uninstall_all_modal(cui_ctx *ui, app_state_t *s) {
    if (!s || !s->show_uninstall_all_modal) return;

    const char *dlg_title = s->uninstall_all_is_batch ? "从所有工具批量卸载" : "从所有工具卸载技能";

    cui_dialog(ui, dlg_title, &s->show_uninstall_all_modal, .width = 540, .persistent = false) {
        cui_column(ui, .width = CUI_FILL, .gap = 14) {
            /* 1. Header warning box */
            cui_box(ui, .width = CUI_FILL, .radius = 6, .pad = 12, .gap = 8,
                    .bg = cui_alpha(cui_color_of(ui, CUI_COLOR_DANGER), 0.08f),
                    .border = 1, .border_color = cui_alpha(cui_color_of(ui, CUI_COLOR_DANGER), 0.35f)) {
                cui_row(ui, .gap = 10, .align = CUI_ALIGN_CENTER) {
                    cui_icon(ui, CUI_ICON_TRIANGLE_ALERT, 22, CUI_COLOR_DANGER);
                    cui_column(ui, .gap = 2) {
                        if (!s->uninstall_all_is_batch) {
                            char prompt_h[160];
                            snprintf(prompt_h, sizeof(prompt_h), "确认从所有工具中卸载技能【%s】？", s->uninstall_all_skill_name);
                            cui_heading(ui, prompt_h, 4);
                        } else {
                            int sel_cnt = 0;
                            for (int i = 0; i < s->hub_matrix.skill_count; i++) {
                                if (s->hub_matrix.skills[i].selected) sel_cnt++;
                            }
                            char prompt_h[160];
                            snprintf(prompt_h, sizeof(prompt_h), "确认从所有工具批量卸载 %d 个技能？", sel_cnt);
                            cui_heading(ui, prompt_h, 4);
                        }
                        cui_text(ui, "注意：此操作将从所有已安装这些技能的本地工具目录中删除该技能目录！",
                                 .size = 12, .color = CUI_COLOR_DANGER);
                    }
                }
            }

            /* 单技能展示涉及的工具，批量卸载展示选中的技能。 */
            if (!s->uninstall_all_is_batch) {
                const hub_skill_entry_t *entry = hub_find_skill(&s->hub_matrix, s->uninstall_all_skill_name);

                cui_column(ui, .gap = 6) {
                    cui_text(ui, "涉及已安装的本地工具：", .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);
                    int ins_cnt = 0;
                    if (entry) {
                        cui_row(ui, .gap = 6, .wrap = true) {
                            for (int t = 0; t < entry->tool_count; t++) {
                                if (entry->tool_copies[t].exists) {
                                    cui_badge(ui, entry->tool_copies[t].tool_name, .tone = CUI_TONE_WARNING, .size = CUI_SIZE_SM);
                                    ins_cnt++;
                                }
                            }
                        }
                    }
                    if (ins_cnt == 0) {
                        cui_text(ui, "（当前尚未在任何本地工具中安装此技能）", .size = 12, .color = CUI_COLOR_TEXT_MUTED);
                    }
                }
            } else {
                cui_column(ui, .gap = 6) {
                    cui_text(ui, "将从所有工具中卸载以下技能：", .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);
                    cui_scroll(ui, .id = "batch_uninst_scroll", .width = CUI_FILL, .height = CUI_PX(110)) {
                        cui_row(ui, .wrap = true, .gap = 6) {
                            for (int i = 0; i < s->hub_matrix.skill_count; i++) {
                                if (s->hub_matrix.skills[i].selected) {
                                    cui_badge(ui, s->hub_matrix.skills[i].skill_name, .tone = CUI_TONE_NEUTRAL, .size = CUI_SIZE_SM);
                                }
                            }
                        }
                    }
                }
            }

            /* 3. Safety Notice */
            cui_row(ui, .gap = 6, .align = CUI_ALIGN_CENTER) {
                cui_icon(ui, CUI_ICON_SHIELD, 14, CUI_COLOR_TEXT_MUTED);
                cui_text(ui, "安全保障：卸载前系统已自动在 ~/.skillboard/backups/ 保存备份快照",
                         .size = 11, .color = CUI_COLOR_TEXT_MUTED);
            }

            cui_separator(ui);

            /* 4. Action Buttons (<= 4 chars each) */
            cui_row(ui, .width = CUI_FILL, .justify = CUI_JUSTIFY_END, .gap = 8) {
                if (cui_button(ui, "取消", .variant = CUI_BUTTON_SECONDARY)) {
                    s->show_uninstall_all_modal = false;
                }

                if (cui_button(ui, "确认卸载", .variant = CUI_BUTTON_DANGER, .icon = CUI_ICON_TRASH_2,
                               .disabled = s->op_progress.active || s->hub_scan.is_scanning)) {
                    if (!s->uninstall_all_is_batch) {
                        int rc = hub_uninstall_skill_from_all_tools(&s->hub_matrix, s->uninstall_all_skill_name);
                        app_set_toast(s, rc < 0 ? "部分工具副本卸载失败" : "已从所有工具中卸载技能");
                    } else {
                        int count = 0, failed = 0;
                        for (int i = 0; i < s->hub_matrix.skill_count; i++) {
                            if (s->hub_matrix.skills[i].selected) {
                                int rc = hub_uninstall_skill_from_all_tools(&s->hub_matrix, s->hub_matrix.skills[i].skill_name);
                                if (rc < 0) failed++;
                                else count++;
                                s->hub_matrix.skills[i].selected = false;
                            }
                        }
                        char tst[128];
                        snprintf(tst, sizeof(tst), "已卸载 %d 个技能，%d 个未完全卸载", count, failed);
                        app_set_toast(s, tst);
                    }
                    s->show_uninstall_all_modal = false;
                    do_scan_sync(s);
                    app_storage_invalidate(s);
                    start_hub_scan_thread(s);
                }
            }
        }
    }
}

void render_uninstall_tool_modal(cui_ctx *ui, app_state_t *s) {
    if (!s || !s->show_uninstall_tool_modal || s->uninstall_tool_skill_name[0] == '\0') return;

    cui_dialog(ui, "卸载当前工具技能", &s->show_uninstall_tool_modal, .width = 500, .persistent = false) {
        cui_column(ui, .width = CUI_FILL, .gap = 14) {
            /* 1. Warning Box */
            cui_box(ui, .width = CUI_FILL, .radius = 6, .pad = 12, .gap = 8,
                    .bg = cui_alpha(cui_color_of(ui, CUI_COLOR_DANGER), 0.08f),
                    .border = 1, .border_color = cui_alpha(cui_color_of(ui, CUI_COLOR_DANGER), 0.35f)) {
                cui_row(ui, .gap = 10, .align = CUI_ALIGN_CENTER) {
                    cui_icon(ui, CUI_ICON_TRIANGLE_ALERT, 22, CUI_COLOR_DANGER);
                    cui_column(ui, .gap = 2) {
                        char prompt_h[160];
                        snprintf(prompt_h, sizeof(prompt_h), "确认从【%s】卸载技能【%s】？",
                                 s->active_tool_name, s->uninstall_tool_skill_name);
                        cui_heading(ui, prompt_h, 4);
                        cui_text(ui, "此操作仅从当前工具目录中删除该技能，不影响其他工具或主仓基准库。",
                                 .size = 12, .color = CUI_COLOR_DANGER);
                    }
                }
            }

            /* 2. Path details */
            cui_column(ui, .gap = 4) {
                cui_textf(ui, "目标工具: %s", s->active_tool_name);
                char skl_dir[1024];
                snprintf(skl_dir, sizeof(skl_dir), "%s/%s", s->active_tool_path, s->uninstall_tool_skill_name);
                cui_textf(ui, "删除路径: %s", skl_dir);
            }

            /* 3. Safety Notice */
            cui_row(ui, .gap = 6, .align = CUI_ALIGN_CENTER) {
                cui_icon(ui, CUI_ICON_SHIELD, 14, CUI_COLOR_TEXT_MUTED);
                cui_text(ui, "安全保障：卸载前系统已自动在 ~/.skillboard/backups/ 保存备份快照",
                         .size = 11, .color = CUI_COLOR_TEXT_MUTED);
            }

            cui_separator(ui);

            /* 4. Action Buttons (<= 4 chars each) */
            cui_row(ui, .width = CUI_FILL, .justify = CUI_JUSTIFY_END, .gap = 8) {
                if (cui_button(ui, "取消", .variant = CUI_BUTTON_SECONDARY)) {
                    s->show_uninstall_tool_modal = false;
                }

                if (cui_button(ui, "确认卸载", .variant = CUI_BUTTON_DANGER, .icon = CUI_ICON_TRASH_2,
                               .disabled = s->op_progress.active || s->hub_scan.is_scanning)) {
                    s->show_uninstall_tool_modal = false;
                    if (hub_uninstall_skill_from_tool(s->active_tool_path, s->uninstall_tool_skill_name,
                                                       s->active_tool_name) != 0) {
                        app_set_toast(s, "技能卸载未完成，请检查快照目录和文件权限");
                    } else {
                        app_set_toast(s, "已成功从当前工具卸载该技能");

                        /* Rescan skills for current tool */
                        if (s->skill_items) {
                            free(s->skill_items);
                            s->skill_items = NULL;
                            s->skill_count = 0;
                        }
                        scan_tool_skills(s->active_tool_path, &s->skill_items, &s->skill_count);
                        if (s->skill_count > 0) {
                            select_active_skill(s, s->skill_items[0].name, s->skill_items[0].path);
                        } else {
                            s->active_skill_name[0] = '\0';
                            s->active_skill_path[0] = '\0';
                            if (s->skill_file_tree) {
                                free_skill_file_tree(s->skill_file_tree);
                                free(s->skill_file_tree);
                                s->skill_file_tree = NULL;
                            }
                            if (s->selected_file_content) {
                                free(s->selected_file_content);
                                s->selected_file_content = NULL;
                            }
                            s->selected_file_path[0] = '\0';
                            s->selected_file_name[0] = '\0';
                        }

                        /* Sync database tool count and hub matrix */
                        do_scan_sync(s);
                        app_storage_invalidate(s);
                        start_hub_scan_thread(s);
                    }
                }
            }
        }
    }
}

void request_clear_tool(app_state_t *s, const char *tool_name, const char *tool_path) {
    snprintf(s->clear_tool_name, sizeof(s->clear_tool_name), "%s", tool_name);
    snprintf(s->clear_tool_path, sizeof(s->clear_tool_path), "%s", tool_path);
    s->show_clear_tool_modal = true;
}

void render_clear_tool_modal(cui_ctx *ui, app_state_t *s) {
    if (!s || !s->show_clear_tool_modal || s->clear_tool_name[0] == '\0') return;

    /* 目录情况实时读取，不依赖列表里可能过期的技能数。 */
    const char *path = s->clear_tool_path;
    const storage_directory_info_t *info = app_storage_directory(s, STORAGE_PATH_TOOL_SKILLS, path);
    bool busy = s->hub_scan.is_scanning || s->sync_is_busy || s->op_progress.active;
    const char *backup = s->setting_backup_dir[0] ? s->setting_backup_dir : STORAGE_DEFAULT_BACKUP;

    cui_dialog(ui, "清空工具技能目录", &s->show_clear_tool_modal, .width = 540, .persistent = false) {
        cui_column(ui, .width = CUI_FILL, .gap = 14) {
            /* 1. Warning Box */
            cui_box(ui, .width = CUI_FILL, .radius = 6, .pad = 12, .gap = 8,
                    .bg = cui_alpha(cui_color_of(ui, CUI_COLOR_DANGER), 0.08f),
                    .border = 1, .border_color = cui_alpha(cui_color_of(ui, CUI_COLOR_DANGER), 0.35f)) {
                cui_row(ui, .gap = 10, .align = CUI_ALIGN_CENTER) {
                    cui_icon(ui, CUI_ICON_TRIANGLE_ALERT, 22, CUI_COLOR_DANGER);
                    cui_column(ui, .gap = 2) {
                        char prompt_h[192];
                        snprintf(prompt_h, sizeof(prompt_h), "确认清空工具【%s】的全部技能？", s->clear_tool_name);
                        cui_heading(ui, prompt_h, 4);
                        cui_text(ui, "该目录下的全部技能目录都会被移除，主仓基准库和其他工具不受影响。",
                                 .size = 12, .color = CUI_COLOR_DANGER);
                    }
                }
            }

            /* 2. Path details */
            cui_column(ui, .gap = 4) {
                cui_textf(ui, "目标工具: %s", s->clear_tool_name);
                cui_textf(ui, "目录路径: %s", path);
                cui_tooltip(ui, path);
                if (info->exists) {
                    cui_textf(ui, "技能目录: %d 个", info->subdirs);
                } else {
                    cui_text(ui, "技能目录: 已不存在", .size = 12, .color = CUI_COLOR_WARNING);
                }
                cui_textf(ui, "快照目录: %s", backup);
            }

            /* 3. Safety Notice */
            cui_row(ui, .gap = 6, .align = CUI_ALIGN_CENTER) {
                cui_icon(ui, CUI_ICON_SHIELD, 14, CUI_COLOR_TEXT_MUTED);
                cui_text(ui, "安全保障：清空前目录内的全部内容会整体移入快照，可随时找回",
                         .size = 11, .color = CUI_COLOR_TEXT_MUTED);
            }

            cui_separator(ui);

            /* 4. Action Buttons (<= 4 chars each) */
            cui_row(ui, .width = CUI_FILL, .justify = CUI_JUSTIFY_END, .gap = 8) {
                if (cui_button(ui, "取消", .variant = CUI_BUTTON_SECONDARY)) {
                    s->show_clear_tool_modal = false;
                }

                if (cui_button(ui, "确认清空", .variant = CUI_BUTTON_DANGER, .icon = CUI_ICON_TRASH_2,
                               .disabled = busy || !info->exists)) {
                    s->show_clear_tool_modal = false;
                    int cleared = 0;
                    int rc = hub_clear_tool_skills(path, s->clear_tool_name, &cleared);

                    char message[256];
                    cui_tone tone = CUI_TONE_SUCCESS;
                    if (cleared == 0 && rc == 0) {
                        snprintf(message, sizeof(message), "【%s】的技能目录已经是空的", s->clear_tool_name);
                    } else if (rc == 0) {
                        /* 技能数不含隐藏条目和悬空链接，这里不报条目数以免与弹窗对不上。 */
                        snprintf(message, sizeof(message), "已清空【%s】的技能目录，内容已移入快照",
                                 s->clear_tool_name);
                    } else {
                        tone = CUI_TONE_WARNING;
                        if (rc == HUB_CLEAR_CROSS_DEVICE) {
                            snprintf(message, sizeof(message), "备份目录与技能目录不在同一磁盘，未清空");
                        } else if (cleared > 0) {
                            snprintf(message, sizeof(message), "已清空 %d 项，其余条目未能移出，请检查目录权限", cleared);
                        } else {
                            snprintf(message, sizeof(message), "清空未完成，请检查技能目录和备份目录的权限");
                        }
                    }
                    cui_toast(ui, message, .tone = tone);

                    do_scan_sync(s);
                    app_storage_invalidate(s);
                    start_hub_scan_thread(s);
                    /* 正在浏览该工具时重新读取技能列表。 */
                    if (s->current_page == PAGE_TOOL_SKILLS && strcmp(s->active_tool_path, path) == 0) {
                        open_tool_skills_page(s, s->clear_tool_name, path);
                    }
                }
            }
        }
    }
}
