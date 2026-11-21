// tests/test_solar.c —— 太阳几何的宿主机测试。
//
// 参考值来自同一套公式的独立实现（见会话里跑过的 Python 镜像），核对的是
// "量级与关系对不对"：赤纬的周年范围、正午高度与赤纬的互补关系、日出日落的
// 对称性、日照时长的季节性次序，以及极昼/极夜分支。
#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "civil_time.h"
#include "places.h"
#include "solar.h"

#define TZ PLACE_TZ_OFFSET_MIN

static int64_t day_epoch(int y, int m, int d) {
    int64_t epoch = 0;
    const bool ok = civil_time_epoch_from_fields(y, m, d, 12, 0, TZ, &epoch);
    assert(ok);
    return civil_time_local_midnight(epoch, TZ);
}

static solar_day_t day_of(int y, int m, int d, double lat, double lon) {
    solar_day_t day = {0};
    solar_compute_day(day_epoch(y, m, d), lat, lon, TZ, &day);
    return day;
}

int main(void) {
    const double lat = PLACES[PLACE_WEIHAI].lat_deg;
    const double lon = PLACES[PLACE_WEIHAI].lon_deg;

    // 1. 赤纬的周年范围：二至约 ±23.44°，春分接近 0°。
    const solar_day_t summer = day_of(2026, 6, 21, lat, lon);
    const solar_day_t winter = day_of(2026, 12, 21, lat, lon);
    const solar_day_t equinox = day_of(2026, 3, 20, lat, lon);
    assert(fabs(summer.declination_deg - 23.44) < 0.05);
    assert(fabs(winter.declination_deg + 23.44) < 0.05);
    assert(fabs(equinox.declination_deg) < 0.6);

    // 2. 时差永远落在 ±17 分钟内。
    const solar_day_t days[] = {summer, winter, equinox};
    for (size_t i = 0; i < sizeof(days) / sizeof(days[0]); i++) {
        assert(fabs(days[i].equation_of_time_min) < 17.0);
    }

    // 3. 正午高度角 = 90° - |纬度 - 赤纬|。
    const double noon_el = 90.0 - fabs(lat - summer.declination_deg);
    const int64_t summer_midnight = day_epoch(2026, 6, 21);
    const double el_at_noon = solar_elevation_deg(
        summer_midnight + (int64_t)llround(summer.noon_min) * 60, lat, lon);
    assert(fabs(el_at_noon - noon_el) < 0.3);

    // 4. 日出日落关于正午对称，日照 = 8 × 时角。
    for (size_t i = 0; i < sizeof(days) / sizeof(days[0]); i++) {
        const solar_day_t day = days[i];
        assert(day.has_rise);
        assert(fabs((day.rise_min + day.set_min) - 2.0 * day.noon_min) < 1e-9);
        assert(day.set_min > day.rise_min);
        assert(fabs(day.day_length_min - (day.set_min - day.rise_min)) < 1e-9);
        assert(day.dawn_min < day.rise_min);
        assert(day.dusk_min > day.set_min);
    }

    // 5. 北半球的季节性次序：夏至日照 > 春分 > 冬至。
    assert(summer.day_length_min > equinox.day_length_min);
    assert(equinox.day_length_min > winter.day_length_min);
    // 与独立实现的实测值对齐（分钟级）。
    assert(fabs(summer.day_length_min - 885.0) < 2.0);
    assert(fabs(winter.day_length_min - 574.0) < 2.0);

    // 6. 天津比威海稍南、稍西：同一天日出更晚、日照更短（11 月下旬）。
    const solar_day_t tj = day_of(2026, 11, 21, PLACES[PLACE_TIANJIN].lat_deg,
                                  PLACES[PLACE_TIANJIN].lon_deg);
    const solar_day_t wh = day_of(2026, 11, 21, lat, lon);
    assert(tj.rise_min > wh.rise_min);
    assert(tj.day_length_min < wh.day_length_min);

    // 7. 极昼/极夜分支：78°N 夏至永远在天上，冬至永远在下面。
    const solar_day_t polar_summer = day_of(2026, 6, 21, 78.0, 15.0);
    assert(!polar_summer.has_rise);
    assert(polar_summer.always_up);
    assert(fabs(polar_summer.day_length_min - 1440.0) < 1e-9);

    const solar_day_t polar_winter = day_of(2026, 12, 21, 78.0, 15.0);
    assert(!polar_winter.has_rise);
    assert(!polar_winter.always_up);
    assert(fabs(polar_winter.day_length_min) < 1e-9);

    // 8. 高度角：正午为正、子夜为负，且方位角落在 0..360。
    const int64_t midnight = day_epoch(2026, 11, 21);
    const int64_t noon_utc = midnight + 4 * 3600; // 12:00 本地 = 04:00 UTC
    assert(solar_elevation_deg(noon_utc, lat, lon) > 25.0);
    assert(solar_elevation_deg(midnight, lat, lon) < 0.0);
    const double az = solar_azimuth_deg(noon_utc, lat, lon);
    assert(az >= 0.0 && az < 360.0);
    assert(fabs(az - 180.0) < 12.0); // 正午前后太阳大致在正南

    // 9. 分钟归一化。
    assert(solar_minutes_of_day(-1.0) == 1439);
    assert(solar_minutes_of_day(1440.0) == 0);
    assert(solar_minutes_of_day(90.4) == 90);
    assert(solar_minutes_of_day(NAN) == 0);

    (void)printf("test_solar: PASS\n");
    return 0;
}
