// tests/test_app_state.c —— 按键语义的宿主机测试。
#include <assert.h>
#include <stdio.h>

#include "app_state.h"

int main(void) {
    app_state_t state;
    app_state_init(&state, 0);
    assert(state.view == APP_VIEW_ARC);
    assert(app_state_backlight_percent(&state) == APP_BRIGHTNESS_PERCENT[0]);

    // 1. 下键依次向前翻页并回绕。
    app_state_effect_t e = app_state_handle_key(&state, APP_KEY_DOWN_CLICK);
    assert(e.view_changed);
    assert(state.view == APP_VIEW_PAIR);
    e = app_state_handle_key(&state, APP_KEY_DOWN_CLICK);
    assert(state.view == APP_VIEW_LEFT);
    e = app_state_handle_key(&state, APP_KEY_DOWN_CLICK);
    assert(state.view == APP_VIEW_ARC);

    // 2. 上键反向。
    e = app_state_handle_key(&state, APP_KEY_UP_CLICK);
    assert(e.view_changed);
    assert(state.view == APP_VIEW_LEFT);

    // 3. 确定短按循环背光档位，绕回起点。
    for (int i = 0; i < APP_BRIGHTNESS_STEPS; i++) {
        e = app_state_handle_key(&state, APP_KEY_OK_CLICK);
        assert(e.brightness_changed);
        assert(!e.view_changed);
        assert(app_state_backlight_percent(&state) ==
               APP_BRIGHTNESS_PERCENT[(i + 1) % APP_BRIGHTNESS_STEPS]);
    }
    assert(state.brightness_step == 0);

    // 4. 长按息屏：背光归零，且不改页面。
    const app_view_t before = state.view;
    e = app_state_handle_key(&state, APP_KEY_OK_LONG);
    assert(e.powered_changed);
    assert(state.screen_off);
    assert(app_state_backlight_percent(&state) == 0);
    assert(state.view == before);

    // 5. 息屏时任意键只负责点亮：不翻页、不改背光档。
    const uint8_t step_before = state.brightness_step;
    e = app_state_handle_key(&state, APP_KEY_UP_CLICK);
    assert(e.powered_changed);
    assert(!state.screen_off);
    assert(!e.view_changed);
    assert(!e.brightness_changed);
    assert(state.view == before);
    assert(state.brightness_step == step_before);
    assert(app_state_backlight_percent(&state) == APP_BRIGHTNESS_PERCENT[0]);

    // 6. 非法初始档位被夹回 0。
    app_state_init(&state, 200);
    assert(state.brightness_step == 0);

    // 7. 方向值为 0 时不动。
    assert(app_state_step_view(APP_VIEW_PAIR, 0) == APP_VIEW_PAIR);

    (void)printf("test_app_state: PASS\n");
    return 0;
}
