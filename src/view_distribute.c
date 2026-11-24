#include "view_distribute.h"
#include "view_common.h"
#include "app_hub_tasks.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "portability.h"

void render_distribute_skill_modal(cui_ctx *ui, app_state_t *s) {
    if (!s || !s->show_distribute_modal || s->distribute_skill_name[0] == '\0') return;

    /* Find entry in hub_matrix */
    hub_skill_entry_t *entry = NULL;
    for (int i = 0; i < s->hub_matrix.skill_count; i++) {
        if (strcmp(s->hub_matrix.skills[i].skill_name, s->distribute_skill_name) == 0) {
            entry = &s->hub_matrix.skills[i];
            break;
        }
    }

    cui_dialog(ui, "分发主仓技能到本地工具", &s->show_distribute_modal,
               .width = 720, .persistent = false) {
        cui_column(ui, .width = CUI_FILL, .gap = 14) {
            /* 1. Skill Header Card */
            cui_box(ui, .width = CUI_FILL, .radius = 6, .pad = 12, .gap = 8,
                    .bg = cui_color_of(ui, CUI_COLOR_SURFACE_RAISED),
                    .border = 1, .border_color = cui_color_of(ui, CUI_COLOR_BORDER)) {
                cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                    cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                        cui_icon(ui, CUI_ICON_ARCHIVE, 20, CUI_COLOR_ACCENT);
                        cui_heading(ui, s->distribute_skill_name, 3);
                    }
                    cui_spacer(ui);
                    if (entry) {
                        char cov_txt[64];
                        snprintf(cov_txt, sizeof(cov_txt), "当前覆盖: %d / %d 个本地工具",
                                 entry->installed_count, s->hub_matrix.tool_count);
                        cui_badge(ui, cov_txt,
                                  .tone = (entry->installed_count > 0) ? CUI_TONE_SUCCESS : CUI_TONE_NEUTRAL,
                                  .size = CUI_SIZE_MD);
                    }
                }

                if (entry && entry->description[0] != '\0') {
                    cui_text(ui, entry->description, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);
                }

                cui_row(ui, .gap = 16, .align = CUI_ALIGN_CENTER) {
                    if (entry && entry->hub_hash[0] != '\0') {
                        cui_textf(ui, "主仓指纹: %s", entry->hub_hash);
                    }
                    cui_spacer(ui);
                }
            }

            /* 2. Tools Distribution List */
            static bool s_dist_tool_sel[64] = {false};
            int sel_tools = 0;
            for (int i = 0; i < s->hub_matrix.tool_count && i < 64; i++) {
                if (s_dist_tool_sel[i]) sel_tools++;
            }

            cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                cui_text(ui, "各本地工具分发状态与操作:", .size = 12, .color = CUI_COLOR_TEXT_MUTED);
                cui_spacer(ui);
                if (sel_tools > 0) {
                    char sel_txt[32];
                    snprintf(sel_txt, sizeof(sel_txt), "已选 %d 个", sel_tools);
                    cui_badge(ui, sel_txt, .tone = CUI_TONE_ACCENT, .size = CUI_SIZE_SM);

                    if (cui_button(ui, "批量安装", .variant = CUI_BUTTON_PRIMARY, .icon = CUI_ICON_PLUS, .size = CUI_SIZE_SM)) {
                        for (int i = 0; i < s->hub_matrix.tool_count && i < 64; i++) {
                            if (s_dist_tool_sel[i]) {
                                start_copy_install_thread(s, s->hub_matrix.tool_paths[i], s->distribute_skill_name, s->hub_matrix.tools[i], i);
                                s_dist_tool_sel[i] = false;
                            }
                        }
                    }
                    if (cui_button(ui, "取消选择", .variant = CUI_BUTTON_GHOST, .size = CUI_SIZE_SM)) {
                        memset(s_dist_tool_sel, 0, sizeof(s_dist_tool_sel));
                    }
                }
            }

            cui_scroll(ui, .id = "distribute_tools_scroll", .width = CUI_FILL, .height = CUI_PX(300)) {
                cui_column(ui, .width = CUI_FILL, .gap = 8) {
                    for (int t = 0; t < s->hub_matrix.tool_count; t++) {
                        const char *t_name = s->hub_matrix.tools[t];
                        const char *t_path = s->hub_matrix.tool_paths[t];

                        /* Find tool_copies status if entry exists */
                        tool_copy_status_t *tc = NULL;
                        if (entry) {
                            for (int k = 0; k < entry->tool_count; k++) {
                                if (strcmp(entry->tool_copies[k].tool_name, t_name) == 0) {
                                    tc = &entry->tool_copies[k];
                                    break;
                                }
                            }
                        }

                        bool exists = (tc && tc->exists);
                        int status = tc ? tc->status : HUB_STATUS_NOT_INSTALLED;

                        cui_push_id_int(ui, t);
                        cui_box(ui, .width = CUI_FILL, .radius = 6, .pad = 10, .gap = 8,
                                .bg = cui_alpha(cui_color_of(ui, CUI_COLOR_SURFACE), 0.7f),
                                .border = 1, .border_color = cui_color_of(ui, CUI_COLOR_BORDER)) {
                            cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                                if (t < 64) {
                                    cui_checkbox(ui, "", &s_dist_tool_sel[t]);
                                }

                                cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                                    cui_icon(ui, CUI_ICON_BOX, 16, CUI_COLOR_ACCENT);
                                    cui_column(ui, .gap = 2) {
                                        cui_text(ui, t_name, .font = CUI_FONT_MEDIUM, .size = 13);
                                        cui_text(ui, t_path, .size = 11, .color = CUI_COLOR_TEXT_MUTED);
                                    }
                                }

                                cui_spacer(ui);

                                /* Status Badge */
                                if (!exists) {
                                    cui_badge(ui, "未安装", .tone = CUI_TONE_NEUTRAL, .size = CUI_SIZE_SM);
                                } else if (status == HUB_STATUS_IN_SYNC) {
                                    cui_badge(ui, "版本一致", .tone = CUI_TONE_SUCCESS, .size = CUI_SIZE_SM);
                                } else if (status == HUB_STATUS_LOCAL_MODIFIED) {
                                    cui_badge(ui, "本地改动", .tone = CUI_TONE_WARNING, .size = CUI_SIZE_SM);
                                } else if (status == HUB_STATUS_UNREADABLE) {
                                    cui_badge(ui, "无法比较", .tone = CUI_TONE_WARNING, .size = CUI_SIZE_SM);
                                } else if (status == HUB_STATUS_OUTDATED) {
                                    cui_badge(ui, "版本落后", .tone = CUI_TONE_INFO, .size = CUI_SIZE_SM);
                                }

                                /* Action Buttons */
                                cui_row(ui, .gap = 6, .align = CUI_ALIGN_CENTER) {
                                    if (!exists) {
                                        if (cui_button(ui, "安装", .variant = CUI_BUTTON_GHOST,
                                                       .icon = CUI_ICON_PLUS, .size = CUI_SIZE_SM)) {
                                            start_copy_install_thread(s, t_path, s->distribute_skill_name, t_name, t);
                                        }
                                    } else if (status == HUB_STATUS_LOCAL_MODIFIED || status == HUB_STATUS_OUTDATED) {
                                        if (cui_button(ui, "覆盖", .variant = CUI_BUTTON_GHOST,
                                                       .icon = ICON_OVERWRITE, .size = CUI_SIZE_SM)) {
                                            start_sync_master_to_tool_thread(s, t_path, s->distribute_skill_name, t_name, t);
                                        }
                                    } else {
                                        if (cui_button(ui, "重装", .variant = CUI_BUTTON_GHOST,
                                                       .icon = CUI_ICON_REFRESH_CW, .size = CUI_SIZE_SM)) {
                                            start_sync_master_to_tool_thread(s, t_path, s->distribute_skill_name, t_name, t);
                                        }
                                    }
                                }
                            }
                        }
                        cui_pop_id(ui);
                    }
                }
            }

            /* 3. Footer Safety Note */
            cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                cui_row(ui, .gap = 6, .align = CUI_ALIGN_CENTER) {
                    cui_icon(ui, CUI_ICON_SHIELD, 14, CUI_COLOR_TEXT_MUTED);
                    cui_text(ui, "覆盖前已自动生成安全快照备份",
                             .size = 11, .color = CUI_COLOR_TEXT_MUTED);
                }
                cui_spacer(ui);
                if (cui_button(ui, "关闭", .variant = CUI_BUTTON_SECONDARY, .size = CUI_SIZE_SM)) {
                    s->show_distribute_modal = false;
                }
            }
        }
    }
}
