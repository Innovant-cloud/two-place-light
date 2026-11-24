// main/places_store.c
#include "places_store.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "places.h"

static const char *TAG = "places_store";

#define PLACES_NS "places"
// 版本写进 key：将来改布局就换 key，旧记录自然失效、退回默认值。
#define PLACES_KEY "cfg1"

typedef struct {
    char name[PLACE_COUNT][PLACE_NAME_MAX + 1];
    double lat_deg[PLACE_COUNT];
    double lon_deg[PLACE_COUNT];
    int32_t tz_offset_min;
} places_blob_t;

esp_err_t places_store_load(void) {
    places_init();

    nvs_handle_t handle;
    esp_err_t err = nvs_open(PLACES_NS, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK; // 还没配过，用默认
    if (err != ESP_OK) return err;

    places_blob_t blob;
    size_t len = sizeof blob;
    err = nvs_get_blob(handle, PLACES_KEY, &blob, &len);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    if (len != sizeof blob) {
        ESP_LOGW(TAG, "记录长度 %u 与预期 %u 不符，改用默认坐标", (unsigned)len,
                 (unsigned)sizeof blob);
        return ESP_OK;
    }

    // 逐字段校验：一条坏记录不该让整台设备失去坐标。
    for (int i = 0; i < PLACE_COUNT; i++) {
        blob.name[i][PLACE_NAME_MAX] = '\0'; // 防御：旧记录可能没有结尾的 0
        if (!places_set(i, blob.name[i], blob.lat_deg[i], blob.lon_deg[i])) {
            ESP_LOGW(TAG, "第 %d 组坐标不可用，保留默认值", i + 1);
        }
    }
    if (!places_set_tz((int)blob.tz_offset_min)) {
        ESP_LOGW(TAG, "时区偏移不可用，保留默认值");
    }
    ESP_LOGI(TAG, "已载入坐标：%s / %s", places_get(PLACE_PRIMARY)->name,
             places_get(PLACE_SECONDARY)->name);
    return ESP_OK;
}

esp_err_t places_store_save(void) {
    places_blob_t blob = {0};
    for (int i = 0; i < PLACE_COUNT; i++) {
        const place_t *place = places_get(i);
        if (place == NULL) return ESP_ERR_INVALID_STATE;
        memcpy(blob.name[i], place->name, strlen(place->name) + 1);
        blob.lat_deg[i] = place->lat_deg;
        blob.lon_deg[i] = place->lon_deg;
    }
    blob.tz_offset_min = (int32_t)places_tz_offset_min();

    nvs_handle_t handle;
    esp_err_t err = nvs_open(PLACES_NS, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(handle, PLACES_KEY, &blob, sizeof blob);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}
