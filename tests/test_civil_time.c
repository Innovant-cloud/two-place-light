// tests/test_civil_time.c —— 日历换算的宿主机测试。
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "civil_time.h"

#define TZ 480 // UTC+8

int main(void) {
    int64_t epoch = 0;

    // 1. 本地 2026-11-21 22:30 -> 反向格式化回同一串。
    assert(civil_time_epoch_from_fields(2026, 11, 21, 22, 30, TZ, &epoch));
    char buf[32];
    civil_time_format(epoch, TZ, buf, sizeof buf);
    assert(strcmp(buf, "2026-11-21 22:30") == 0);

    // 2. 那一刻的当天分钟数与本地零点。
    assert(civil_time_minutes_of_day(epoch, TZ) == 22 * 60 + 30);
    const int64_t midnight = civil_time_local_midnight(epoch, TZ);
    int y = 0, mo = 0, d = 0, hh = 0, mm = 0;
    civil_time_local_fields(midnight, TZ, &y, &mo, &d, &hh, &mm);
    assert(y == 2026 && mo == 11 && d == 21 && hh == 0 && mm == 0);

    // 3. 闰年与大小月。
    assert(civil_time_days_in_month(2024, 2) == 29);
    assert(civil_time_days_in_month(2026, 2) == 28);
    assert(civil_time_days_in_month(2000, 2) == 29);
    assert(civil_time_days_in_month(1900, 2) == 28);
    assert(civil_time_days_in_month(2026, 4) == 30);
    assert(civil_time_days_in_month(2026, 12) == 31);
    assert(civil_time_days_in_month(2026, 13) == 0);

    // 4. 非法日期被拒绝，而不是被"就近修正"。
    assert(!civil_time_epoch_from_fields(2026, 2, 30, 12, 0, TZ, &epoch));
    assert(!civil_time_epoch_from_fields(2026, 13, 1, 12, 0, TZ, &epoch));
    assert(!civil_time_epoch_from_fields(2026, 1, 1, 24, 0, TZ, &epoch));
    assert(!civil_time_epoch_from_fields(2019, 1, 1, 12, 0, TZ, &epoch));
    assert(civil_time_epoch_from_fields(2024, 2, 29, 12, 0, TZ, &epoch));

    // 5. 跨月/跨年边界：2026-12-31 23:59 的下一秒是 2027-01-01 00:00。
    assert(civil_time_epoch_from_fields(2026, 12, 31, 23, 59, TZ, &epoch));
    civil_time_format(epoch + 60, TZ, buf, sizeof buf);
    assert(strcmp(buf, "2027-01-01 00:00") == 0);

    // 6. 负 epoch 方向的归一化不会把日期算错（1970 之前也走同一套算术）。
    civil_time_format(-86400, 0, buf, sizeof buf);
    assert(strcmp(buf, "1969-12-31 00:00") == 0);

    // 7. 缓冲区不足时不写越界，只写空串。
    char tiny[4] = "abc";
    civil_time_format(epoch, TZ, tiny, sizeof tiny);
    assert(tiny[0] == '\0');

    (void)printf("test_civil_time: PASS\n");
    return 0;
}
