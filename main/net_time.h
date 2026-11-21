// main/net_time.h —— 联网对时（SNTP）。
#pragma once

#include <stdint.h>

#include "esp_err.h"

// 在已经拿到 IP 的前提下做一次 SNTP 同步：成功后把时间写进 RTC，并更新 NVS 里
// 的"上次对时"记录。超时返回 ESP_ERR_TIMEOUT；同步后时间仍不可信视为失败。
esp_err_t net_time_sync(uint32_t timeout_ms);
