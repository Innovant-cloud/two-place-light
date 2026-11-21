// main/solar.h —— 太阳几何：赤纬、时差、日出日落、任意时刻高度角。
//
// 纯数学模块，不碰 ESP-IDF / LVGL，可在宿主机上直接编译测试。
// 算法取自 NOAA Solar Calculator 的简化式（精度约 ±1 分钟日出日落、
// ±0.05° 高度角），对一台只用来"看光"的小设备足够。
#pragma once

#include <stdbool.h>
#include <stdint.h>

// 一个本地日（06:00 本地时间所在的那一天）的太阳几何快照。
// 所有 *_min 字段都是【本地时钟分钟】，0..1440，可能略小于 0 或超过 1440
// （高纬度的极昼边缘），用之前按需归一化。
typedef struct {
    double declination_deg;      // 太阳赤纬
    double equation_of_time_min; // 时差 = 真太阳时 - 平太阳时，约 -14..+16 分钟
    double noon_min;             // 太阳正午
    double rise_min;             // 日出（上边缘触地平线，含大气折射）
    double set_min;              // 日落
    double dawn_min;             // 民用晨光开始（太阳高度 -6°）
    double dusk_min;             // 民用暮光结束
    double day_length_min;       // 日照分钟数
    bool has_rise;               // false = 该纬度此日出现极昼或极夜
    bool always_up;              // 仅 has_rise == false 时有意义
} solar_day_t;

// 求解某一本地日的太阳几何。
//
// utc_midnight_epoch 传【该本地日 00:00 对应的 UTC 秒数】（已含时区偏移）。
// lat_deg 北纬为正，lon_deg 东经为正，tz_offset_min 是本地时区相对 UTC 的分钟数
// （UTC+8 传 480）。各字段在本地正午时刻一次求解，遵循 NOAA 的常规做法。
void solar_compute_day(int64_t utc_midnight_epoch, double lat_deg, double lon_deg,
                       int tz_offset_min, solar_day_t *out);

// 某一 UTC 时刻在某地的太阳高度角（度）。地平线以下为负。
double solar_elevation_deg(int64_t utc_epoch, double lat_deg, double lon_deg);

// 某一 UTC 时刻的太阳方位角（度，正北 0°、顺时针）。
double solar_azimuth_deg(int64_t utc_epoch, double lat_deg, double lon_deg);

// 把"本地时钟分钟"格式化用的纯函数，供 UI 与测试共用：四舍五入到分钟并按
// 24 小时归一化，返回 0..1439；输入非有限值返回 0。
int solar_minutes_of_day(double local_min);
