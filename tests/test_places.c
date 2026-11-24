// tests/test_places.c —— 坐标与时区校验的宿主机测试。
//
// 校验规则直接决定设置页能接受什么：名字必须是屏上字体画得出的 ASCII，
// 经纬度必须在球面范围内，时区必须是 15 分钟的整数倍。
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "places.h"

int main(void) {
    places_init();

    // 默认值来自出厂表。
    const place_t *primary = places_get(PLACE_PRIMARY);
    const place_t *secondary = places_get(PLACE_SECONDARY);
    assert(primary != NULL && secondary != NULL);
    assert(strcmp(primary->name, "WEIHAI") == 0);
    assert(strcmp(secondary->name, "TIANJIN") == 0);
    assert(places_tz_offset_min() == PLACES_DEFAULT_TZ_OFFSET_MIN);
    // 越界索引返回 NULL，调用方不用猜。
    assert(places_get(-1) == NULL);
    assert(places_get(PLACE_COUNT) == NULL);

    // 名字白名单：字母、数字、空格、'-'、'_'，1..12 字节。
    assert(places_name_valid("TIANJIN"));
    assert(places_name_valid("NEW YORK"));
    assert(places_name_valid("A1-b_c"));
    assert(places_name_valid("ABCDEFGHIJKL")); // 正好 12
    assert(!places_name_valid(""));
    assert(!places_name_valid("ABCDEFGHIJKLM"));   // 13 字节
    assert(!places_name_valid("BAD<NAME"));        // 尖括号会破坏设置页
    assert(!places_name_valid("bad&name"));
    assert(!places_name_valid("   "));             // 只有空格不算名字
    assert(!places_name_valid("\xE5\x8C\x97"));    // 中文：屏上没有字形

    // 坐标范围（含 NaN / 无穷这类"比较永远为假"的输入）。
    assert(places_coord_valid(37.51, 122.12));
    assert(places_coord_valid(-90.0, -180.0));
    assert(places_coord_valid(90.0, 180.0));
    assert(!places_coord_valid(90.01, 0.0));
    assert(!places_coord_valid(0.0, 180.01));
    assert(!places_coord_valid(-91.0, 0.0));
    assert(!places_coord_valid(NAN, 0.0));
    assert(!places_coord_valid(0.0, INFINITY));

    // 时区：-720..840 且是 15 分钟的整数倍。
    assert(places_tz_valid(0));
    assert(places_tz_valid(480));
    assert(places_tz_valid(-720));
    assert(places_tz_valid(840));
    assert(!places_tz_valid(841));
    assert(!places_tz_valid(-721));
    assert(!places_tz_valid(481)); // 不是 15 的倍数

    // 写入成功。
    assert(places_set(PLACE_PRIMARY, "QINGDAO", 36.07, 120.38));
    assert(strcmp(places_get(PLACE_PRIMARY)->name, "QINGDAO") == 0);
    assert(places_get(PLACE_PRIMARY)->lat_deg > 36.06);
    assert(places_get(PLACE_PRIMARY)->lon_deg > 120.37);
    // 副坐标不受影响。
    assert(strcmp(places_get(PLACE_SECONDARY)->name, "TIANJIN") == 0);

    // 非法写入被拒绝，且原有值保持不变。
    assert(!places_set(PLACE_PRIMARY, "", 36.0, 120.0));
    assert(!places_set(PLACE_PRIMARY, "OK", 95.0, 120.0));
    assert(!places_set(PLACE_PRIMARY, "OK", 36.0, -181.0));
    assert(!places_set(PLACE_COUNT, "OK", 36.0, 120.0));
    assert(strcmp(places_get(PLACE_PRIMARY)->name, "QINGDAO") == 0);

    assert(places_set_tz(-300));
    assert(places_tz_offset_min() == -300);
    assert(!places_set_tz(7));
    assert(places_tz_offset_min() == -300);

    // 重新初始化能回到出厂值：设置页读的是 NVS，这份运行时值随时可以重建。
    places_init();
    assert(strcmp(places_get(PLACE_PRIMARY)->name, "WEIHAI") == 0);
    assert(places_tz_offset_min() == PLACES_DEFAULT_TZ_OFFSET_MIN);

    (void)printf("test_places: PASS\n");
    return 0;
}
