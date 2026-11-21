// main/app_clock.h —— 设备时间。
//
// 板子没有独立 RTC 芯片，时间来自 ESP32-C3 的 RTC 计数器（newlib 的 RTC 时基），
// 所以：深睡眠期间时间照走；整机断电后时间清零，需要重新对时。
//
// 对时只有一条路：USB 串口控制台 `t` 命令。最近一次对时的时间戳会存进 NVS，
// 断电重启后在画面上作为"上次对时"提示显示，但【不会】被当成当前时间来用——
// 我们无法知道断电期间过了多久，假装知道就是撒谎。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "civil_time.h"
#include "esp_err.h"

// 早于 2025-01-01 00:00:00 UTC 的时间一律视为"未对时"。
#define APP_CLOCK_VALID_FROM CIVIL_TIME_VALID_FROM_EPOCH

// 读 NVS 里上次对时的记录。调用前必须已完成 nvs_flash_init()。
esp_err_t app_clock_init(void);

// 当前时间是否可信（RTC 已被设定过且没有掉电）。
bool app_clock_is_set(void);

// 最近一次对时的 UTC 时间戳；从未对过时返回 0。
int64_t app_clock_last_sync_epoch(void);

// 设定时间：写 RTC 并持久化到 NVS。epoch 早于 APP_CLOCK_VALID_FROM 时返回
// ESP_ERR_INVALID_ARG，不写入。
esp_err_t app_clock_set(int64_t utc_epoch);

// 当前 UTC 时间戳；未对时时返回值不可信（可能 < APP_CLOCK_VALID_FROM）。
int64_t app_clock_now(void);
