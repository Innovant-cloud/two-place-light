// main/ui.h —— 屏幕内容。所有 LVGL 调用都在 LVGL 任务或持锁上下文里发生。
#pragma once

#include "app_setup.h"
#include "app_state.h"

// 建首个页面（ARC）。必须在 bsp_lvgl_init() 成功之后调用。
void ui_show_view(app_view_t view);

// 切到对时页。
void ui_show_setup(const app_setup_t *setup);

// 联网/配网状态页：标题 + 两行提示。
void ui_show_net(const char *title, const char *line1, const char *line2);

// 刷新当前页面的数字与曲线。由 LVGL 定时器每秒调用一次。
void ui_refresh(void);
