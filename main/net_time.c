// main/net_time.c
#include "net_time.h"

#include "app_clock.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "net_time";

// 优先国内可达的 NTP；SNTP 客户端会按时延选源。
#define NTP_SERVER_PRIMARY "ntp.aliyun.com"

esp_err_t net_time_sync(uint32_t timeout_ms) {
    // ESP_NETIF_SNTP_DEFAULT_CONFIG 已带上 start=true、smooth_sync=false、
    // wait_for_sync=true：没有 wait_for_sync，下面的 sync_wait 会直接返回
    // ESP_ERR_INVALID_STATE，所以这里不再手改这几个字段。
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(NTP_SERVER_PRIMARY);

    esp_err_t err = esp_netif_sntp_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SNTP 初始化失败: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(timeout_ms));
    esp_netif_sntp_deinit();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SNTP 同步超时/失败: %s", esp_err_to_name(err));
        return err;
    }

    const int64_t now = app_clock_now();
    if (now < APP_CLOCK_VALID_FROM) {
        // 同步"成功"但时间仍不合理，等于没对上，不要假装成功。
        ESP_LOGE(TAG, "同步后时间仍不可信");
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t set_err = app_clock_set(now);
    if (set_err != ESP_OK) {
        ESP_LOGW(TAG, "对时记录写入失败: %s", esp_err_to_name(set_err));
    }
    ESP_LOGI(TAG, "SNTP 对时完成");
    return ESP_OK;
}
