// main/civil_time.h —— 日历换算。纯整数运算，无时区数据库依赖。
//
// 板子只认 UTC 秒，界面只认本地时间；两者之间的换算全部集中在这里，
// 好让"某天的太阳几何"和"对时页填的日期"用同一套算术。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// 早于这一刻（2025-01-01T00:00:00Z）的 UTC 秒一律视为"时间不可信"。
// 放在这个纯头文件里，是为了让对时状态机不必依赖 esp_err.h。
#define CIVIL_TIME_VALID_FROM_EPOCH 1735689600LL

// UTC 秒 + 时区分钟 -> "YYYY-MM-DD HH:MM"。buf_len 至少 17；不足时写入空串。
void civil_time_format(int64_t utc_epoch, int tz_offset_min, char *buf, size_t buf_len);

// UTC 秒 -> 该本地日 00:00 对应的 UTC 秒。
int64_t civil_time_local_midnight(int64_t utc_epoch, int tz_offset_min);

// UTC 秒 -> 本地当天分钟数，0..1439。
int civil_time_minutes_of_day(int64_t utc_epoch, int tz_offset_min);

// 本地日期时间 -> UTC 秒。字段越界时返回 false（例如 2 月 30 日）。
// 闰年与大小月规则在这里统一处理，对时页与测试共用。
bool civil_time_epoch_from_fields(int year, int month, int day, int hour, int minute,
                                  int tz_offset_min, int64_t *out_epoch);

// 给定年月的天数；month 越界返回 0。
int civil_time_days_in_month(int year, int month);

// UTC 秒 -> 本地日期（各字段可传 NULL 表示不需要）。
void civil_time_local_fields(int64_t utc_epoch, int tz_offset_min, int *out_year,
                             int *out_month, int *out_day, int *out_hour, int *out_minute);
