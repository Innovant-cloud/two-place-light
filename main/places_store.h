// main/places_store.h —— 把运行时坐标存进 NVS。读写都要能失败而不致命：
// 记录缺失就用出厂默认，单条记录坏了只回退那一条。
#pragma once

#include "esp_err.h"

// 读出并写入运行时值。没有记录不是错误。
esp_err_t places_store_load(void);

// 把当前运行时值写回 NVS。
esp_err_t places_store_save(void);
