// main/main.c —— 「两地光」/ Two-Place Light
//
// 一台只关心光的小设备：把威海和天津此刻的太阳高度、日出日落、今天还剩多少光
// 放在同一块 240x320 的屏上。全部计算在本地完成，不联网、不带素材。
//
// 结构：
//   app_main 只做初始化与任务编排；
//   按键 -> 队列 -> 输入任务 -> 状态机(app_state / app_setup) -> UI(ui.*)。
//   LVGL 非线程安全：所有 lv_* 调用要么在刷新定时器里（LVGL 任务内），
//   要么持有 bsp_lvgl_lock()。
#include <stdio.h>
#include <string.h>

#include "app_clock.h"
#include "app_setup.h"
#include "app_state.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "nvs_flash.h"
#include "places.h"
#include "ui.h"

static const char *TAG = "two_place_light";

#define INPUT_QUEUE_DEPTH 8
#define REFRESH_PERIOD_MS 1000
#define LVGL_LOCK_TIMEOUT_MS 500

typedef struct {
    bsp_btn_t btn;
    bsp_btn_ev_t ev;
} raw_input_t;

static QueueHandle_t s_input_queue;
static app_state_t s_state;
static app_setup_t s_setup;

// ------------------------------------------------------------------ 按键

// 按钮回调运行在 button 组件的 esp_timer 任务里：只入队，立刻返回。
static void on_button(bsp_btn_t btn, bsp_btn_ev_t ev, void *user) {
    (void)user;
    if (ev != BSP_BTN_CLICK && ev != BSP_BTN_LONG) return;
    const raw_input_t input = {.btn = btn, .ev = ev};
    (void)xQueueSend(s_input_queue, &input, 0);
}

// 把 BSP 事件翻译成应用语义；不认识的组合返回 false。
static bool translate(const raw_input_t *raw, app_key_t *out) {
    if (raw->ev == BSP_BTN_CLICK) {
        switch (raw->btn) {
        case BSP_BTN_UP:
            *out = APP_KEY_UP_CLICK;
            return true;
        case BSP_BTN_DOWN:
            *out = APP_KEY_DOWN_CLICK;
            return true;
        case BSP_BTN_OK:
            *out = APP_KEY_OK_CLICK;
            return true;
        default:
            return false;
        }
    }
    if (raw->ev == BSP_BTN_LONG && raw->btn == BSP_BTN_OK) {
        *out = APP_KEY_OK_LONG;
        return true;
    }
    return false;
}

// ------------------------------------------------------------------ 界面

static void show_view_locked(app_view_t view) {
    if (bsp_lvgl_lock(LVGL_LOCK_TIMEOUT_MS)) {
        ui_show_view(view);
        bsp_lvgl_unlock();
    } else {
        ESP_LOGW(TAG, "LVGL 加锁超时，本次不重画");
    }
}

static void show_setup_locked(void) {
    if (bsp_lvgl_lock(LVGL_LOCK_TIMEOUT_MS)) {
        ui_show_setup(&s_setup);
        bsp_lvgl_unlock();
    }
}

// 对时页结束后的去向：时间有效就进主界面，否则留在对时页。
static void leave_setup(bool committed) {
    app_setup_end(&s_setup);
    if (committed) {
        ESP_LOGI(TAG, "对时完成");
    }
    if (app_clock_is_set()) {
        show_view_locked(s_state.view);
    } else {
        // 没有可信时间，主界面的数字全是假的，所以重新回到对时页。
        app_setup_begin(&s_setup, app_clock_last_sync_epoch());
        show_setup_locked();
    }
}

static void handle_key(app_key_t key) {
    if (s_setup.active) {
        const setup_result_t result = app_setup_handle_key(&s_setup, key);
        if (result == SETUP_RESULT_COMMITTED) {
            int64_t epoch = 0;
            if (app_setup_epoch(&s_setup, &epoch)) {
                (void)app_clock_set(epoch);
            }
            leave_setup(true);
        } else if (result == SETUP_RESULT_CANCELLED) {
            leave_setup(false);
        } else {
            show_setup_locked(); // 字段变了，重画
        }
        return;
    }

    const app_state_effect_t effect = app_state_handle_key(&s_state, key);
    if (effect.powered_changed || effect.brightness_changed) {
        bsp_display_backlight(app_state_backlight_percent(&s_state));
    }
    if (effect.view_changed) {
        show_view_locked(s_state.view);
    }
}

static void input_task(void *arg) {
    (void)arg;
    raw_input_t raw;
    for (;;) {
        if (xQueueReceive(s_input_queue, &raw, portMAX_DELAY) == pdTRUE) {
            app_key_t key;
            if (translate(&raw, &key)) {
                handle_key(key);
            }
        }
    }
}

// ------------------------------------------------------------------ 刷新

static void refresh_timer_cb(lv_timer_t *timer) {
    (void)timer;
    ui_refresh();
}

// ------------------------------------------------------------------ 启动

static esp_err_t nvs_boot(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // 分区里没有可用空间或版本不匹配：清空重来。这里只清本应用自己的 NVS。
        ESP_LOGW(TAG, "NVS 需要重新初始化: %s", esp_err_to_name(err));
        const esp_err_t erase_err = nvs_flash_erase();
        if (erase_err != ESP_OK) return erase_err;
        err = nvs_flash_init();
    }
    return err;
}

void app_main(void) {
    ESP_LOGI(TAG, "Two-Place Light 启动");

    const esp_err_t nvs_err = nvs_boot();
    if (nvs_err != ESP_OK) {
        ESP_LOGE(TAG, "NVS 初始化失败: %s（对时记录将无法保存）", esp_err_to_name(nvs_err));
    }

    bsp_i2c_init();
    bsp_i2c_scan();

    // 屏幕是唯一的输出，初始化失败就没有界面可言，直接报清楚后停下。
    if (bsp_display_init() != ESP_OK || bsp_lvgl_init() == NULL) {
        ESP_LOGE(TAG, "显示或 LVGL 初始化失败，无法继续");
        return;
    }
    bsp_display_backlight(100);

    (void)app_clock_init();
    const esp_err_t battery_err = bsp_battery_init();
    if (battery_err != ESP_OK) {
        ESP_LOGW(TAG, "电量计不可用(%s)，界面显示 B--", esp_err_to_name(battery_err));
    }

    app_state_init(&s_state, 0);

    s_input_queue = xQueueCreate(INPUT_QUEUE_DEPTH, sizeof(raw_input_t));
    if (s_input_queue == NULL) {
        ESP_LOGE(TAG, "输入队列创建失败");
        return;
    }

    if (!app_clock_is_set()) {
        ESP_LOGW(TAG, "时间未设定，先进入对时页");
        app_setup_begin(&s_setup, app_clock_last_sync_epoch());
        show_setup_locked();
    } else {
        show_view_locked(s_state.view);
    }

    if (xTaskCreate(input_task, "tpl_input", 3072, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "输入任务创建失败");
        return;
    }

    const esp_err_t button_err = bsp_button_init(on_button, NULL);
    if (button_err != ESP_OK) {
        // 没有按键就无法对时也无法翻页，但画面仍然可以提供信息，因此不静默退出。
        ESP_LOGE(TAG, "按键初始化失败: %s（只能显示，不能操作）", esp_err_to_name(button_err));
    }

    if (bsp_lvgl_lock(LVGL_LOCK_TIMEOUT_MS)) {
        (void)lv_timer_create(refresh_timer_cb, REFRESH_PERIOD_MS, NULL);
        bsp_lvgl_unlock();
    }

    ESP_LOGI(TAG, "就绪：Battery=%s Clock=%s", battery_err == ESP_OK ? "ok" : "unavailable",
             app_clock_is_set() ? "set" : "unset");
}
