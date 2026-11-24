// main/main.c —— 「两地光」/ Two-Place Light
//
// 一台只关心光的小设备：把威海和天津此刻的太阳高度、日出日落、今天还剩多少光
// 放在同一块 240x320 的屏上。全部计算在本地完成，不依赖网络；网络只用来对时。
//
// 启动顺序（时间优先）：
//   1. RTC 里已有可信时间 -> 先在有凭证时校一次时，再进主界面。
//   2. 没有可信时间 -> 有 Wi-Fi 凭证就先连、再 SNTP 对时。
//   3. 没凭证或连不上 -> 开设置热点，手机打开 192.168.4.1 填城市与 Wi-Fi。
//   4. 任何一步长按确定 -> 放弃联网，改用三键手动对时。
//
// 运行中：每 6 小时后台静默校时；长按下键随时打开设置热点。
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
#include "places.h"
#include "places_store.h"
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
#define SETTINGS_PORTAL_TIMEOUT_MS (5 * 60 * 1000)
#define RESYNC_PERIOD_MS (6 * 60 * 60 * 1000)

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

static void show_net_locked(const char *title, const char *line1, const char *line2,
                            const char *hint) {
    if (bsp_lvgl_lock(LVGL_LOCK_TIMEOUT_MS)) {
        ui_show_net(title, line1, line2, hint);
        bsp_lvgl_unlock();
    }
}

// ------------------------------------------------------------------ 时间获取

static bool cancel_requested(void) { return s_manual_requested; }

// 没有可信时间时的获取路径。返回时 apps_clock 可能已可信，也可能没有。
static void acquire_time(void) {
    s_manual_requested = false;

    if (net_has_credentials()) {
        show_net_locked("NO TIME", "CONNECTING WI-FI", "", "HOLD OK: MANUAL SET");
        if (net_connect_sta(WIFI_CONNECT_TIMEOUT_MS, cancel_requested) == ESP_OK) {
            show_net_locked("WI-FI OK", "SYNCING TIME", "", "HOLD OK: MANUAL SET");
            if (net_time_sync(SNTP_TIMEOUT_MS) == ESP_OK) {
                show_net_locked("TIME OK", "SET FROM NETWORK", "", "HOLD OK: MANUAL SET");
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

    show_net_locked("SETUP WI-FI", "JOIN: TwoPlaceLight", "OPEN 192.168.4.1",
                    "HOLD OK: MANUAL SET");
    // 提交成功后本函数内部重启设备，不会返回；这里只处理超时与中止。
    const esp_err_t prov = net_start_portal(PROVISIONING_TIMEOUT_MS, cancel_requested);
    if (prov == ESP_ERR_INVALID_STATE) {
        ESP_LOGI(TAG, "用户要求手动对时");
    } else {
        ESP_LOGW(TAG, "配网未完成: %s", esp_err_to_name(prov));
    }
    net_stop();
}

// 时间已经有过、但可能已经跑偏：RTC 走的是片内 RC，没有 32.768 kHz 晶振，
// 放一天就能偏出肉眼可见的分钟数。这里做一次静默校准——对不上就继续用旧时间，
// 绝不因为校时失败把设备卡在联网页上。
static void resync_time(bool show_progress) {
    if (!net_has_credentials()) return;
    if (show_progress) {
        show_net_locked("TIME", "SYNCING", "", NULL);
    }
    if (net_connect_sta(WIFI_CONNECT_TIMEOUT_MS, NULL) != ESP_OK) {
        net_stop();
        return;
    }
    if (net_time_sync(SNTP_TIMEOUT_MS) != ESP_OK) {
        ESP_LOGW(TAG, "校时失败，继续用旧时间");
    }
    net_stop();
}

static void resync_task(void *arg) {
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(RESYNC_PERIOD_MS));
        resync_time(false);
    }
}

// ------------------------------------------------------------------ 设置热点

// 长按 `下` 随叫随开：屏幕上给出热点名和地址，按任意键关掉。
// 抢时间阶段（时钟还不可信）不允许打断，免得把配网流程顶掉。
static volatile bool s_portal_active;
static volatile bool s_portal_cancel;

static bool portal_cancel_requested(void) { return s_portal_cancel; }

static void settings_task(void *arg) {
    (void)arg;
    show_net_locked("SETTINGS", "JOIN: TwoPlaceLight", "OPEN 192.168.4.1", "ANY KEY  CLOSE");
    const esp_err_t err = net_start_portal(SETTINGS_PORTAL_TIMEOUT_MS, portal_cancel_requested);
    net_stop();
    s_portal_active = false;
    s_portal_cancel = false;
    if (err == ESP_ERR_TIMEOUT || err == ESP_ERR_INVALID_STATE) {
        // 保存成功时 net_start_portal() 内部会重启，走不到这里。
        show_view_locked(s_state.view);
    }
    vTaskDelete(NULL);
}

static void open_settings_portal(void) {
    if (s_portal_active || s_setup.active || !app_clock_is_set()) return;
    s_portal_cancel = false;
    s_portal_active = true;
    if (xTaskCreate(settings_task, "tpl_settings", 4096, NULL, 4, NULL) != pdPASS) {
        s_portal_active = false;
        ESP_LOGE(TAG, "设置热点任务创建失败");
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
            // 设置热点开着的时候，任意键都只做一件事：关掉它。
            if (s_portal_active) {
                s_portal_cancel = true;
                continue;
            }
            // 长按下键 = 打开设置热点（换城市/换 Wi-Fi），不进 app_state 的语义。
            if (raw.ev == BSP_BTN_LONG && raw.btn == BSP_BTN_DOWN) {
                open_settings_portal();
                continue;
            }
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
    // 坐标与时区来自 NVS（设置页写的）；没有记录就用出厂默认的威海/天津。
    const esp_err_t places_err = places_store_load();
    if (places_err != ESP_OK) {
        ESP_LOGW(TAG, "坐标读取失败(%s)，使用默认值", esp_err_to_name(places_err));
    }
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
    } else {
        // 重置或刷机后 RTC 里的时间会保留下来，但可能已经跑偏；有凭证就先校一次。
        resync_time(true);
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

    if (xTaskCreate(resync_task, "tpl_resync", 4096, NULL, 3, NULL) != pdPASS) {
        ESP_LOGW(TAG, "定期对时任务创建失败，时间只能靠 RTC 自己走");
    }

    ESP_LOGI(TAG, "就绪：Battery=%s Clock=%s WiFi creds=%s",
             battery_err == ESP_OK ? "ok" : "unavailable", app_clock_is_set() ? "set" : "unset",
             net_has_credentials() ? "stored" : "none");
}
