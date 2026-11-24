// main/places.h —— 这台设备关心的两个坐标。
//
// 只放坐标与显示名，不放任何业务逻辑。运行时值可以被设置页改写并持久化，
// 出厂默认是威海（老家）和天津（学校）。
//
// 数值为城市中心附近的近似值（±0.05° 量级，对日照推算的影响远小于一分钟），
// 不是测绘级坐标。
//
// 本文件不依赖 ESP-IDF：NVS 读写放在 places_store.c，这里保持可在主机上测试。
#pragma once

#include <stdbool.h>
#include <stddef.h>

// 索引 0（主）和索引 1（副）。配色与曲线顺序按索引走，不跟具体城市绑定。
#define PLACE_PRIMARY 0
#define PLACE_SECONDARY 1
#define PLACE_COUNT 2

// 屏上只有 ASCII 字体：名字限定为字母、数字、空格、'-'、'_'，最长这么多字符。
#define PLACE_NAME_MAX 12

// 时区偏移的合法范围（分钟）：UTC-12:00 .. UTC+14:00，且必须是 15 分钟的整数倍。
#define PLACE_TZ_MIN (-720)
#define PLACE_TZ_MAX 840

typedef struct {
    char name[PLACE_NAME_MAX + 1]; // 屏上显示的名字，ASCII
    double lat_deg;                // 北纬为正
    double lon_deg;                // 东经为正
} place_t;

// 出厂默认值，供 places_init()/places_store_load() 回退使用，也供主机测试引用。
extern const place_t PLACES_DEFAULT[PLACE_COUNT];
#define PLACES_DEFAULT_TZ_OFFSET_MIN 480

// 把运行时值置为出厂默认；重复调用是安全的。
void places_init(void);

// 越界返回 NULL。返回的指针指向模块内部存储，写入请走 places_set()。
const place_t *places_get(int index);

int places_tz_offset_min(void);

// 校验通过才写入；失败时保持原值不动。
bool places_set(int index, const char *name, double lat_deg, double lon_deg);
bool places_set_tz(int minutes);

// 纯校验函数（places_set 内部同样调用它们，主机测试直接引用）。
bool places_name_valid(const char *name);
bool places_coord_valid(double lat_deg, double lon_deg);
bool places_tz_valid(int minutes);
