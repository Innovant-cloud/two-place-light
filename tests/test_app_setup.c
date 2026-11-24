// tests/test_app_setup.c —— 三键对时页状态机的宿主机测试。
#include <assert.h>
#include <stdio.h>

#include "app_setup.h"
#include "civil_time.h"
#include "places.h"

#define TZ PLACES_DEFAULT_TZ_OFFSET_MIN

static void press(app_setup_t *s, app_key_t key, int times) {
    for (int i = 0; i < times; i++) {
        (void)app_setup_handle_key(s, key);
    }
}

int main(void) {
    app_setup_t setup;

    // 1. 没有历史记录时用占位值，并且停在年份字段。
    app_setup_begin(&setup, 0);
    assert(setup.active);
    assert(setup.field == SETUP_FIELD_YEAR);
    assert(setup.year == 2026 && setup.month == 1 && setup.day == 1);

    // 2. 有历史记录时用它对预填。
    int64_t hint = 0;
    assert(civil_time_epoch_from_fields(2026, 11, 21, 22, 30, TZ, &hint));
    app_setup_begin(&setup, hint);
    assert(setup.year == 2026 && setup.month == 11 && setup.day == 21);
    assert(setup.hour == 22 && setup.minute == 30);

    // 3. 上下键改当前字段；月份回绕。
    press(&setup, APP_KEY_UP_CLICK, 1); // 2027
    assert(setup.year == 2027);
    press(&setup, APP_KEY_DOWN_CLICK, 1);
    assert(setup.year == 2026);
    press(&setup, APP_KEY_OK_CLICK, 1); // -> 月
    assert(setup.field == SETUP_FIELD_MONTH);
    press(&setup, APP_KEY_UP_CLICK, 1); // 11 -> 12
    assert(setup.month == 12);
    press(&setup, APP_KEY_UP_CLICK, 1); // 12 -> 1
    assert(setup.month == 1);

    // 4. 换月时把"日"夹到该月上限：先设 1 月 31 日，再退到 2 月。
    app_setup_begin(&setup, 0);
    assert(setup.month == 1 && setup.day == 1);
    setup.field = SETUP_FIELD_DAY;
    press(&setup, APP_KEY_DOWN_CLICK, 1); // 1 月没有 0 日 -> 回绕到 31
    assert(setup.day == 31);
    setup.field = SETUP_FIELD_MONTH;
    press(&setup, APP_KEY_UP_CLICK, 1); // 2 月
    assert(setup.month == 2);
    assert(setup.day == 28); // 2026 不是闰年

    // 5. 闰年 2 月能到 29 日。
    app_setup_begin(&setup, 0);
    setup.year = 2024;
    setup.month = 2;
    setup.day = 28;
    setup.field = SETUP_FIELD_DAY;
    press(&setup, APP_KEY_UP_CLICK, 1);
    assert(setup.day == 29);
    press(&setup, APP_KEY_UP_CLICK, 1); // 回绕回 1
    assert(setup.day == 1);

    // 6. 时、分的范围。
    setup.field = SETUP_FIELD_HOUR;
    setup.hour = 23;
    press(&setup, APP_KEY_UP_CLICK, 1);
    assert(setup.hour == 0);
    setup.field = SETUP_FIELD_MINUTE;
    setup.minute = 0;
    press(&setup, APP_KEY_DOWN_CLICK, 1);
    assert(setup.minute == 59);

    // 7. 年份不会越出 2020..2100。
    app_setup_begin(&setup, 0);
    setup.year = 2020;
    press(&setup, APP_KEY_DOWN_CLICK, 1);
    assert(setup.year == 2100);
    press(&setup, APP_KEY_UP_CLICK, 1);
    assert(setup.year == 2020);
    // 从 2100 退到 2020 后，"日"仍合法。
    setup.month = 2;
    setup.day = 29;
    press(&setup, APP_KEY_DOWN_CLICK, 1); // 2020 是闰年，29 日合法并保留
    assert(setup.year == 2100);
    assert(setup.day <= 28);

    // 8. 确定逐字段前进，最后一个字段上再按 = 提交。
    app_setup_begin(&setup, 0);
    for (int i = 0; i < SETUP_FIELD_COUNT - 1; i++) {
        assert(app_setup_handle_key(&setup, APP_KEY_OK_CLICK) == SETUP_RESULT_NONE);
    }
    assert(setup.field == SETUP_FIELD_MINUTE);
    assert(app_setup_handle_key(&setup, APP_KEY_OK_CLICK) == SETUP_RESULT_COMMITTED);

    // 9. 提交时给出的 epoch 与 civil_time 一致。
    int64_t epoch = 0;
    assert(app_setup_epoch(&setup, &epoch));
    int y = 0, mo = 0, d = 0, hh = 0, mm = 0;
    civil_time_local_fields(epoch, TZ, &y, &mo, &d, &hh, &mm);
    assert(y == setup.year && mo == setup.month && d == setup.day);
    assert(hh == setup.hour && mm == setup.minute);

    // 10. 长按取消。
    assert(app_setup_handle_key(&setup, APP_KEY_OK_LONG) == SETUP_RESULT_CANCELLED);

    // 11. 结束后不再响应按键。
    app_setup_end(&setup);
    assert(!setup.active);
    assert(app_setup_handle_key(&setup, APP_KEY_OK_CLICK) == SETUP_RESULT_NONE);

    (void)printf("test_app_setup: PASS\n");
    return 0;
}
