// main/app_clock.c
#include "app_clock.h"

#include <sys/time.h>
#include <time.h>

#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "app_clock";
static const char *NVS_NAMESPACE = "tpl";
static const char *NVS_KEY_LAST_SYNC = "sync_epoch";

static int64_t s_last_sync_epoch;

esp_err_t app_clock_init(void) {
    s_last_sync_epoch = 0;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "没有历史对时记录");
        return ESP_OK; // 首次启动，正常
    }
    if (err != ESP_OK) return err;

    int64_t stored = 0;
    err = nvs_get_i64(handle, NVS_KEY_LAST_SYNC, &stored);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;

    s_last_sync_epoch = stored;
    return ESP_OK;
}

bool app_clock_is_set(void) { return app_clock_now() >= APP_CLOCK_VALID_FROM; }

int64_t app_clock_last_sync_epoch(void) { return s_last_sync_epoch; }

int64_t app_clock_now(void) { return (int64_t)time(NULL); }

static esp_err_t persist_last_sync(int64_t epoch) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_i64(handle, NVS_KEY_LAST_SYNC, epoch);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

esp_err_t app_clock_set(int64_t utc_epoch) {
    if (utc_epoch < APP_CLOCK_VALID_FROM) return ESP_ERR_INVALID_ARG;

    const struct timeval tv = {
        .tv_sec = (time_t)utc_epoch,
        .tv_usec = 0,
    };
    if (settimeofday(&tv, NULL) != 0) return ESP_FAIL;

    s_last_sync_epoch = utc_epoch;

    // 对时记录只用于重启后的提示，写不进去不影响当前走时，所以分开报错。
    const esp_err_t err = persist_last_sync(utc_epoch);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "对时记录写入失败: %s", esp_err_to_name(err));
    }
    ESP_LOGI(TAG, "时间已设定");
    return err;
}
