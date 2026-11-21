// main/app_state.c
#include "app_state.h"

const uint8_t APP_BRIGHTNESS_PERCENT[APP_BRIGHTNESS_STEPS] = {100, 60, 30};

void app_state_init(app_state_t *state, uint8_t brightness_step) {
    if (state == NULL) return;
    state->view = APP_VIEW_ARC;
    state->brightness_step = brightness_step < APP_BRIGHTNESS_STEPS ? brightness_step : 0;
    state->screen_off = false;
}

app_view_t app_state_step_view(app_view_t current, int direction) {
    if (direction == 0) return current;
    int v = (int)current;
    v = (v + (direction > 0 ? 1 : -1) + (int)APP_VIEW_COUNT) % (int)APP_VIEW_COUNT;
    return (app_view_t)v;
}

app_state_effect_t app_state_handle_key(app_state_t *state, app_key_t key) {
    app_state_effect_t effect = {0};
    if (state == NULL) return effect;

    // 息屏状态下任何按键只负责点亮：避免"想亮屏却顺手翻了页"。
    if (state->screen_off) {
        state->screen_off = false;
        effect.powered_changed = true;
        return effect;
    }

    switch (key) {
    case APP_KEY_UP_CLICK: {
        const app_view_t next = app_state_step_view(state->view, -1);
        if (next != state->view) {
            state->view = next;
            effect.view_changed = true;
        }
        break;
    }
    case APP_KEY_DOWN_CLICK: {
        const app_view_t next = app_state_step_view(state->view, 1);
        if (next != state->view) {
            state->view = next;
            effect.view_changed = true;
        }
        break;
    }
    case APP_KEY_OK_CLICK:
        state->brightness_step = (uint8_t)((state->brightness_step + 1) % APP_BRIGHTNESS_STEPS);
        effect.brightness_changed = true;
        break;
    case APP_KEY_OK_LONG:
        state->screen_off = true;
        effect.powered_changed = true;
        break;
    default:
        break;
    }
    return effect;
}

uint8_t app_state_backlight_percent(const app_state_t *state) {
    if (state == NULL || state->screen_off) return 0;
    const uint8_t step = state->brightness_step < APP_BRIGHTNESS_STEPS ? state->brightness_step : 0;
    return APP_BRIGHTNESS_PERCENT[step];
}
