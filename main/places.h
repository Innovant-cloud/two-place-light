// main/places.h —— 这台设备关心的两个坐标。
//
// 只放坐标与显示名，不放任何业务逻辑：想改成自己关心的两座城市，改这里即可。
// 数值为城市中心附近的近似值（±0.05° 量级，对日照推算的影响远小于一分钟），
// 不是测绘级坐标。
#pragma once

#include <stddef.h>

typedef struct {
    const char *name;   // 屏上显示的 ASCII 名，避免依赖中文字库
    double lat_deg;     // 北纬为正
    double lon_deg;     // 东经为正
} place_t;

// 索引 0 = 威海（老家），索引 1 = 天津（学校）。
#define PLACE_WEIHAI 0
#define PLACE_TIANJIN 1
#define PLACE_COUNT 2

// 两个坐标都在 UTC+8。
#define PLACE_TZ_OFFSET_MIN 480

extern const place_t PLACES[PLACE_COUNT];
