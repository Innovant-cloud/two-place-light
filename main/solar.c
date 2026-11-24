// main/solar.c —— 太阳位置与昼夜事件。纯数学，无硬件依赖，供宿主机测试。
#include "solar.h"

#include <math.h>
#include <stddef.h> // NULL：glibc 的 math.h 不带，newlib 的带

#define SOLAR_PI 3.14159265358979323846

// 日出/日落用的天顶角：90° + 34'(大气折射) + 16'(日面半径)。
#define ZENITH_SUNRISE 90.833
// 民用暮光结束：太阳中心在地平线下 6°。
#define ZENITH_CIVIL 96.0

static double deg2rad(double d) { return d * (SOLAR_PI / 180.0); }
static double rad2deg(double r) { return r * (180.0 / SOLAR_PI); }

static double wrap360(double deg) {
    double d = fmod(deg, 360.0);
    if (d < 0.0) d += 360.0;
    return d;
}

// 赤纬与时差：NOAA Solar Calculator 的简化解析式。
typedef struct {
    double declination_deg;
    double equation_of_time_min;
} solar_terms_t;

static solar_terms_t terms_at(int64_t utc_epoch) {
    solar_terms_t out;

    const double jd = (double)utc_epoch / 86400.0 + 2440587.5; // Unix 纪元 = JD 2440587.5
    const double t = (jd - 2451545.0) / 36525.0;               // 儒略世纪
    const double L0 = wrap360(280.46646 + t * (36000.76983 + t * 0.0003032));
    const double M = 357.52911 + t * (35999.05029 - 0.0001537 * t);
    const double ecc = 0.016708634 - t * (0.000042037 + 0.0000001267 * t);
    const double M_rad = deg2rad(M);

    // 中心差 -> 真黄经 -> 视黄经
    const double center = sin(M_rad) * (1.914602 - t * (0.004817 + 0.000014 * t)) +
                          sin(2.0 * M_rad) * (0.019993 - 0.000101 * t) +
                          sin(3.0 * M_rad) * 0.000289;
    const double true_long = L0 + center;
    const double omega = 125.04 - 1934.136 * t;
    const double lambda = true_long - 0.00569 - 0.00478 * sin(deg2rad(omega));

    // 平黄赤交角 + 章动修正
    const double eps0 =
        23.0 + (26.0 + (21.448 - t * (46.815 + t * (0.00059 - t * 0.001813))) / 60.0) / 60.0;
    const double eps = eps0 + 0.00256 * cos(deg2rad(omega));

    const double sin_decl = sin(deg2rad(eps)) * sin(deg2rad(lambda));
    out.declination_deg = rad2deg(asin(fmax(-1.0, fmin(1.0, sin_decl))));

    // 时差：y = tan²(ε/2)，四项展开
    const double L0_rad = deg2rad(L0);
    const double y = pow(tan(deg2rad(eps) / 2.0), 2.0);
    const double eq =
        y * sin(2.0 * L0_rad) - 2.0 * ecc * sin(M_rad) +
        4.0 * ecc * y * sin(M_rad) * cos(2.0 * L0_rad) -
        0.5 * y * y * sin(4.0 * L0_rad) - 1.25 * ecc * ecc * sin(2.0 * M_rad);
    out.equation_of_time_min = 4.0 * rad2deg(eq);

    return out;
}

// UTC 时刻当天的"UTC 分钟数"，0..1440。
static double utc_minutes_of_day(int64_t utc_epoch) {
    int64_t sod = utc_epoch % 86400;
    if (sod < 0) sod += 86400;
    return (double)sod / 60.0;
}

// 太阳中心在给定天顶角时的时角（度，取正值）。无解返回 false 并回填是否极昼。
static bool hour_angle_for_zenith(double lat_rad, double decl_rad, double zenith_deg,
                                  double *out_hour_angle_deg, bool *out_always_up) {
    const double cos_zenith = cos(deg2rad(zenith_deg));
    const double denom = cos(lat_rad) * cos(decl_rad);
    if (fabs(denom) < 1e-12) {
        if (out_always_up) *out_always_up = (cos_zenith - sin(lat_rad) * sin(decl_rad)) <= 0.0;
        return false;
    }
    const double arg = (cos_zenith - sin(lat_rad) * sin(decl_rad)) / denom;
    if (arg < -1.0 || arg > 1.0) {
        if (out_always_up) *out_always_up = (arg < -1.0);
        return false;
    }
    *out_hour_angle_deg = rad2deg(acos(arg));
    if (out_always_up) *out_always_up = false;
    return true;
}

// 时角 -> 本地时钟分钟：tst = 4·(H+180)，tst = local + eqtime + 4·lon - tz。
static double local_minutes_from_hour_angle(double hour_angle_deg, double equation_of_time_min,
                                            double lon_deg, int tz_offset_min) {
    return 4.0 * (hour_angle_deg + 180.0) - equation_of_time_min - 4.0 * lon_deg +
           (double)tz_offset_min;
}

void solar_compute_day(int64_t utc_midnight_epoch, double lat_deg, double lon_deg,
                       int tz_offset_min, solar_day_t *out) {
    if (out == NULL) return;

    // NOAA 的常规做法：一日的几何量在本地正午求解一次。
    const int64_t noon_utc =
        utc_midnight_epoch + 720 * 60 - (int64_t)tz_offset_min * 60;
    const solar_terms_t terms = terms_at(noon_utc);
    const double lat = deg2rad(lat_deg);
    const double decl = deg2rad(terms.declination_deg);

    out->declination_deg = terms.declination_deg;
    out->equation_of_time_min = terms.equation_of_time_min;
    out->noon_min =
        local_minutes_from_hour_angle(0.0, terms.equation_of_time_min, lon_deg, tz_offset_min);

    double h0 = 0.0;
    bool always_up = false;
    if (hour_angle_for_zenith(lat, decl, ZENITH_SUNRISE, &h0, &always_up)) {
        out->has_rise = true;
        out->always_up = true;
        out->rise_min =
            local_minutes_from_hour_angle(-h0, terms.equation_of_time_min, lon_deg, tz_offset_min);
        out->set_min =
            local_minutes_from_hour_angle(h0, terms.equation_of_time_min, lon_deg, tz_offset_min);
        out->day_length_min = 8.0 * h0;
    } else {
        out->has_rise = false;
        out->always_up = always_up;
        out->rise_min = out->noon_min;
        out->set_min = out->noon_min;
        out->day_length_min = always_up ? 1440.0 : 0.0;
    }

    double hc = 0.0;
    if (hour_angle_for_zenith(lat, decl, ZENITH_CIVIL, &hc, NULL)) {
        out->dawn_min =
            local_minutes_from_hour_angle(-hc, terms.equation_of_time_min, lon_deg, tz_offset_min);
        out->dusk_min =
            local_minutes_from_hour_angle(hc, terms.equation_of_time_min, lon_deg, tz_offset_min);
    } else {
        out->dawn_min = out->rise_min;
        out->dusk_min = out->set_min;
    }
}

// 给定时刻的时角（弧度）与赤纬，供高度角/方位角复用。
static void hour_angle_at(int64_t utc_epoch, double lon_deg, double *out_hour_angle_rad,
                          double *out_decl_rad) {
    const solar_terms_t terms = terms_at(utc_epoch);
    const double tst_min =
        utc_minutes_of_day(utc_epoch) + terms.equation_of_time_min + 4.0 * lon_deg;
    *out_hour_angle_rad = deg2rad(tst_min / 4.0 - 180.0);
    *out_decl_rad = deg2rad(terms.declination_deg);
}

double solar_elevation_deg(int64_t utc_epoch, double lat_deg, double lon_deg) {
    double h_rad = 0.0;
    double decl_rad = 0.0;
    hour_angle_at(utc_epoch, lon_deg, &h_rad, &decl_rad);

    const double lat = deg2rad(lat_deg);
    const double sin_el = sin(lat) * sin(decl_rad) + cos(lat) * cos(decl_rad) * cos(h_rad);
    return rad2deg(asin(fmax(-1.0, fmin(1.0, sin_el))));
}

double solar_azimuth_deg(int64_t utc_epoch, double lat_deg, double lon_deg) {
    double h_rad = 0.0;
    double decl_rad = 0.0;
    hour_angle_at(utc_epoch, lon_deg, &h_rad, &decl_rad);

    const double lat = deg2rad(lat_deg);
    const double az = atan2(sin(h_rad), cos(h_rad) * sin(lat) - tan(decl_rad) * cos(lat));
    return wrap360(rad2deg(az) + 180.0); // atan2 给出相对南方的方位，转成相对北方
}

int solar_minutes_of_day(double local_min) {
    if (!isfinite(local_min)) return 0;
    long m = lround(local_min);
    m %= 1440;
    if (m < 0) m += 1440;
    return (int)m;
}
