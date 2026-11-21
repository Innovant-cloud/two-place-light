// main/app_setup.c
#include "app_setup.h"

#include "civil_time.h"
#include "places.h"

// 没有历史记录时的占位起点：日期明显像"待填"，避免让人误以为时间是准的。
#define SETUP_PLACEHOLDER_YEAR 2026
#define SETUP_PLACEHOLDER_MONTH 1
#define SETUP_PLACEHOLDER_DAY 1
#define SETUP_PLACEHOLDER_HOUR 12
#define SETUP_PLACEHOLDER_MINUTE 0

#define SETUP_YEAR_MIN 2020
#define SETUP_YEAR_MAX 2100

static int wrap_int(int value, int lo, int hi) {
    const int span = hi - lo + 1;
    int v = value;
    while (v < lo) v += span;
    while (v > hi) v -= span;
    return v;
}

static void clamp_day(app_setup_t *s) {
    const int dim = civil_time_days_in_month(s->year, s->month);
    if (dim > 0 && s->day > dim) s->day = dim;
    if (s->day < 1) s->day = 1;
}

void app_setup_begin(app_setup_t *s, int64_t hint_epoch) {
    if (s == NULL) return;
    s->active = true;
    s->field = SETUP_FIELD_YEAR;

    if (hint_epoch >= CIVIL_TIME_VALID_FROM_EPOCH) {
        civil_time_local_fields(hint_epoch, PLACE_TZ_OFFSET_MIN, &s->year, &s->month, &s->day,
                                &s->hour, &s->minute);
    } else {
        s->year = SETUP_PLACEHOLDER_YEAR;
        s->month = SETUP_PLACEHOLDER_MONTH;
        s->day = SETUP_PLACEHOLDER_DAY;
        s->hour = SETUP_PLACEHOLDER_HOUR;
        s->minute = SETUP_PLACEHOLDER_MINUTE;
    }
    clamp_day(s);
}

bool app_setup_epoch(const app_setup_t *s, int64_t *out_epoch) {
    if (s == NULL || out_epoch == NULL) return false;
    return civil_time_epoch_from_fields(s->year, s->month, s->day, s->hour, s->minute,
                                        PLACE_TZ_OFFSET_MIN, out_epoch);
}

static void adjust_field(app_setup_t *s, int delta) {
    switch (s->field) {
    case SETUP_FIELD_YEAR:
        s->year = wrap_int(s->year + delta, SETUP_YEAR_MIN, SETUP_YEAR_MAX);
        clamp_day(s);
        break;
    case SETUP_FIELD_MONTH:
        s->month = wrap_int(s->month + delta, 1, 12);
        clamp_day(s);
        break;
    case SETUP_FIELD_DAY: {
        const int dim = civil_time_days_in_month(s->year, s->month);
        s->day = wrap_int(s->day + delta, 1, dim > 0 ? dim : 1);
        break;
    }
    case SETUP_FIELD_HOUR:
        s->hour = wrap_int(s->hour + delta, 0, 23);
        break;
    case SETUP_FIELD_MINUTE:
        s->minute = wrap_int(s->minute + delta, 0, 59);
        break;
    default:
        break;
    }
}

setup_result_t app_setup_handle_key(app_setup_t *s, app_key_t key) {
    if (s == NULL || !s->active) return SETUP_RESULT_NONE;

    switch (key) {
    case APP_KEY_UP_CLICK:
        adjust_field(s, 1);
        return SETUP_RESULT_NONE;
    case APP_KEY_DOWN_CLICK:
        adjust_field(s, -1);
        return SETUP_RESULT_NONE;
    case APP_KEY_OK_CLICK:
        if (s->field < SETUP_FIELD_MINUTE) {
            s->field = (setup_field_t)((int)s->field + 1);
            return SETUP_RESULT_NONE;
        }
        // 最后一个字段上按确定 = 提交；字段非法时停在原地，不装作成功。
        {
            int64_t epoch = 0;
            return app_setup_epoch(s, &epoch) ? SETUP_RESULT_COMMITTED : SETUP_RESULT_NONE;
        }
    case APP_KEY_OK_LONG:
        return SETUP_RESULT_CANCELLED;
    default:
        return SETUP_RESULT_NONE;
    }
}

void app_setup_end(app_setup_t *s) {
    if (s != NULL) s->active = false;
}
