// main/app_setup.h —— 三键对时页的状态机。
//
// 板子没有独立 RTC，整机断电后时间会归零；又没有可用的串口输入通道（本仓库
// 从未验证过 stdin），所以对时只能用三个实体按键完成。这里是它的纯逻辑：
// 上下改当前字段，确定跳到下一字段，在最后一个字段上再按确定即提交。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app_state.h" // app_key_t

typedef enum {
    SETUP_FIELD_YEAR = 0,
    SETUP_FIELD_MONTH,
    SETUP_FIELD_DAY,
    SETUP_FIELD_HOUR,
    SETUP_FIELD_MINUTE,
    SETUP_FIELD_COUNT
} setup_field_t;

typedef struct {
    bool active;
    setup_field_t field;
    int year;
    int month;
    int day;
    int hour;
    int minute;
} app_setup_t;

typedef enum {
    SETUP_RESULT_NONE = 0,   // 还在填
    SETUP_RESULT_COMMITTED,  // 填完且合法，可以写入 RTC
    SETUP_RESULT_CANCELLED,  // 放弃
} setup_result_t;

// 进入对时页。hint_epoch 是"上一次对时"的时间戳：≥ APP_CLOCK_VALID_FROM 时
// 用它预填字段（改起来通常只需动日期和小时），否则用固定占位值。
void app_setup_begin(app_setup_t *s, int64_t hint_epoch);

// 处理按键。UP/DOWN 调整当前字段（含闰年与大小月夹取），OK 短按下一字段并
// 在最后一个字段提交，OK 长按取消。
setup_result_t app_setup_handle_key(app_setup_t *s, app_key_t key);

// 当前字段拼出的 UTC 时间戳；字段非法时返回 false。
bool app_setup_epoch(const app_setup_t *s, int64_t *out_epoch);

void app_setup_end(app_setup_t *s);
