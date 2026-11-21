// main/app_state.h —— 界面状态与按键语义。
//
// 纯状态机：不碰 LVGL、不碰 ESP-IDF，输出"该重画什么 / 背光该多少"的意图，
// 由 UI 层去执行。这样按键手感可以在宿主机上直接测。
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    APP_VIEW_ARC = 0, // 一天的太阳高度曲线 + 两个坐标的当前光点
    APP_VIEW_PAIR,    // 两个坐标的数字对照（高度、日出日落、日照差）
    APP_VIEW_LEFT,    // 距离下一次日出/日落还有多久，以及明天的日照变化
    APP_VIEW_COUNT
} app_view_t;

typedef enum {
    APP_KEY_UP_CLICK = 0,
    APP_KEY_DOWN_CLICK,
    APP_KEY_OK_CLICK,
    APP_KEY_OK_LONG,
} app_key_t;

#define APP_BRIGHTNESS_STEPS 3

// 背光档位百分比，索引即 brightness_step。
extern const uint8_t APP_BRIGHTNESS_PERCENT[APP_BRIGHTNESS_STEPS];

typedef struct {
    app_view_t view;
    uint8_t brightness_step;
    bool screen_off;
} app_state_t;

typedef struct {
    bool view_changed;      // 需要重建页面
    bool brightness_changed; // 需要重设背光
    bool powered_changed;   // screen_off 翻转
} app_state_effect_t;

// 初始化。brightness_step 会被夹到合法范围。
void app_state_init(app_state_t *state, uint8_t brightness_step);

// 处理一次按键，返回本次产生的副作用。
//
// 语义：息屏时任意键先点亮、不改变页面；UP/DOWN 循环切页；OK 短按循环背光档；
// OK 长按息屏。
app_state_effect_t app_state_handle_key(app_state_t *state, app_key_t key);

// 当前应设置的背光百分比；息屏时为 0。
uint8_t app_state_backlight_percent(const app_state_t *state);

// 依方向取下一个页面：direction > 0 向前，< 0 向后，0 原地。
app_view_t app_state_step_view(app_view_t current, int direction);
