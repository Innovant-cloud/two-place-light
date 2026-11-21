// main/main.c —— 「两地光」/ Two-Place Light
//
// 一台只关心光的小设备：把威海和天津此刻的太阳高度、日出日落、今天还剩多少光
// 放在同一块 240x320 的屏上。全部计算在本地完成，不依赖网络；网络只用来对时。
//
// 启动顺序（时间优先）：
//   1. RTC 里已有可信时间 -> 直接进主界面。
//   2. 没有可信时间 -> 有 Wi-Fi 凭证就先连、再 SNTP 对时。
//   3. 没凭证或连不上 -> 开 SoftAP，手机打开 192.168.4.1 填 Wi-Fi。
//   4. 任何一步长按确定 -> 放弃联网，改用三键手动对时。
//
// LVGL 非线程安全：所有 lv_* 调用要么在刷新定时器里（LVGL 任务内），
// 要么持有 bsp_lvgl_lock()。
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
#include "net_time.h"
#include "net_wifi.h"
#include "nvs_flash.h"
#include "ui.h"

static const char *TAG = "two_place_light";

#define INPUT_QUEUE_DEPTH 8
#define REFRESH_PERIOD_MS 1000
#define LVGL_LOCK_TIMEOUT_MS 500

#define WIFI_CONNECT_TIMEOUT_MS 15000
#define SNTP_TIMEOUT_MS 8000
#define PROVISIONING_TIMEOUT_MS (3 * 60 * 1000)

typedef struct {
    bsp_btn_t btn;
    bsp_btn_ev_t ev;
} raw_input_t;

static QueueHandle_t s_input_queue;
static app_state_t s_state;
static app_setup_t s_setup;

// 联网阶段用户要求"别连了，我自己对时"。
static volatile bool s_manual_requested;

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

static void show_net_locked(const char *title, const char *line1, const char *line2) {
    if (bsp_lvgl_lock(LVGL_LOCK_TIMEOUT_MS)) {
        ui_show_net(title, line1, line2);
        bsp_lvgl_unlock();
    }
}

// ------------------------------------------------------------------ 时间获取

static bool cancel_requested(void) { return s_manual_requested; }

// 没有可信时间时的获取路径。返回时 apps_clock 可能已可信，也可能没有。
static void acquire_time(void) {
    s_manual_requested = false;

    if (net_has_credentials()) {
        show_net_locked("NO TIME", "CONNECTING WI-FI", "");
        if (net_connect_sta(WIFI_CONNECT_TIMEOUT_MS, cancel_requested) == ESP_OK) {
            show_net_locked("WI-FI OK", "SYNCING TIME", "");
            if (net_time_sync(SNTP_TIMEOUT_MS) == ESP_OK) {
                show_net_locked("TIME OK", "SET FROM NETWORK", "");
                net_stop();
                return;
            }
        }
        net_stop();
        if (s_manual_requested) return;
        ESP_LOGW(TAG, "联网对时未成功，转入配网/手动");
    } else {
        ESP_LOGI(TAG, "没有 Wi-Fi 凭证，进入配网");
    }

    show_net_locked("SETUP WI-FI", "JOIN: TwoPlaceLight", "OPEN 192.168.4.1");
    // 提交成功后本函数内部重启设备，不会返回；这里只处理超时与中止。
    const esp_err_t prov = net_start_provisioning(PROVISIONING_TIMEOUT_MS, cancel_requested);
    if (prov == ESP_ERR_INVALID_STATE) {
        ESP_LOGI(TAG, "用户要求手动对时");
    } else {
        ESP_LOGW(TAG, "配网未完成: %s", esp_err_to_name(prov));
    }
    net_stop();
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

static void handle_key(app_key_t key) {
    // 还在抢时间：唯一有意义的按键是"别连了，手动"。
    if (!app_clock_is_set() && !s_setup.active) {
        if (key == APP_KEY_OK_LONG) {
            s_manual_requested = true;
        }
        return;
    }

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
        ESP_LOGE(TAG, "NVS 初始化失败: %s（无法保存对时记录与 Wi-Fi 凭证）",
                 esp_err_to_name(nvs_err));
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
    if (xTaskCreate(input_task, "tpl_input", 3072, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "输入任务创建失败");
        return;
    }

    const esp_err_t button_err = bsp_button_init(on_button, NULL);
    if (button_err != ESP_OK) {
        ESP_LOGE(TAG, "按键初始化失败: %s（只能显示，不能操作）", esp_err_to_name(button_err));
    }

    if (!app_clock_is_set()) {
        acquire_time();
    }

    if (app_clock_is_set()) {
        show_view_locked(s_state.view);
    } else {
        ESP_LOGW(TAG, "时间仍未设定，进入三键对时");
        app_setup_begin(&s_setup, app_clock_last_sync_epoch());
        show_setup_locked();
    }

    if (bsp_lvgl_lock(LVGL_LOCK_TIMEOUT_MS)) {
        (void)lv_timer_create(refresh_timer_cb, REFRESH_PERIOD_MS, NULL);
        bsp_lvgl_unlock();
    }

    ESP_LOGI(TAG, "就绪：Battery=%s Clock=%s WiFi creds=%s",
             battery_err == ESP_OK ? "ok" : "unavailable", app_clock_is_set() ? "set" : "unset",
             net_has_credentials() ? "stored" : "none");
}
