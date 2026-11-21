// main/civil_time.c
#include "civil_time.h"

#include <stdio.h>

// 以下两式出自 Howard Hinnant 的日期算法（proleptic Gregorian，无时区表）。
static int64_t days_from_civil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153u * (m + (m > 2 ? -3u : 9u)) + 2u) / 5u + d - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

static void civil_from_days(int64_t days, int *out_y, unsigned *out_m, unsigned *out_d) {
    days += 719468;
    const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const unsigned doe = (unsigned)(days - era * 146097);
    const unsigned yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    const int y = (int)yoe + (int)(era * 400);
    const unsigned doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    const unsigned mp = (5u * doy + 2u) / 153u;
    const unsigned d = doy - (153u * mp + 2u) / 5u + 1u;
    const int m = (int)mp + (mp < 10u ? 3 : -9);
    *out_y = y + (m <= 2 ? 1 : 0);
    *out_m = (unsigned)m;
    *out_d = d;
}

int civil_time_days_in_month(int year, int month) {
    static const int DAYS[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) return 0;
    if (month != 2) return DAYS[month - 1];
    const bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    return leap ? 29 : 28;
}

bool civil_time_epoch_from_fields(int year, int month, int day, int hour, int minute,
                                  int tz_offset_min, int64_t *out_epoch) {
    if (out_epoch == NULL) return false;
    if (year < 2020 || year > 2100) return false;
    if (month < 1 || month > 12) return false;
    const int dim = civil_time_days_in_month(year, month);
    if (day < 1 || day > dim) return false;
    if (hour < 0 || hour > 23) return false;
    if (minute < 0 || minute > 59) return false;

    const int64_t days = days_from_civil(year, (unsigned)month, (unsigned)day);
    const int64_t local = days * 86400 + (int64_t)hour * 3600 + (int64_t)minute * 60;
    *out_epoch = local - (int64_t)tz_offset_min * 60;
    return true;
}

void civil_time_local_fields(int64_t utc_epoch, int tz_offset_min, int *out_year,
                             int *out_month, int *out_day, int *out_hour, int *out_minute) {
    const int64_t local = utc_epoch + (int64_t)tz_offset_min * 60;
    int64_t days = local / 86400;
    int64_t sod = local % 86400;
    if (sod < 0) {
        sod += 86400;
        days -= 1;
    }
    int y = 0;
    unsigned m = 0, d = 0;
    civil_from_days(days, &y, &m, &d);
    if (out_year) *out_year = y;
    if (out_month) *out_month = (int)m;
    if (out_day) *out_day = (int)d;
    if (out_hour) *out_hour = (int)(sod / 3600);
    if (out_minute) *out_minute = (int)((sod % 3600) / 60);
}

void civil_time_format(int64_t utc_epoch, int tz_offset_min, char *buf, size_t buf_len) {
    if (buf == NULL || buf_len < 17) {
        if (buf != NULL && buf_len > 0) buf[0] = '\0';
        return;
    }
    int y = 0, mo = 0, d = 0, hh = 0, mm = 0;
    civil_time_local_fields(utc_epoch, tz_offset_min, &y, &mo, &d, &hh, &mm);
    (void)snprintf(buf, buf_len, "%04d-%02d-%02d %02d:%02d", y, mo, d, hh, mm);
}

int64_t civil_time_local_midnight(int64_t utc_epoch, int tz_offset_min) {
    const int64_t local = utc_epoch + (int64_t)tz_offset_min * 60;
    int64_t day_start = local - (local % 86400);
    if (local % 86400 < 0) day_start -= 86400;
    return day_start - (int64_t)tz_offset_min * 60;
}

int civil_time_minutes_of_day(int64_t utc_epoch, int tz_offset_min) {
    const int64_t local = utc_epoch + (int64_t)tz_offset_min * 60;
    int64_t sod = local % 86400;
    if (sod < 0) sod += 86400;
    return (int)(sod / 60);
}
