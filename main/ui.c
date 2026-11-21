// main/ui.c —— 「两地光」的三个页面与对时页。
//
// 设计约定：
//   * 只画自己的界面，不复用基线 demo 的菜单/像素外壳（AI 指南的强制重新设计）。
//   * 界面文字全部为 ASCII：本工程没有验证过的中文字形，宁可不写，也不让缺字
//     方框冒充"已支持"。新增中文属于独立的字库集成任务，见 ai-guide §4。
//   * 曲线随日期缓慢变化，因此按"本地日"缓存，只在跨天时重算，避免每秒跑几十次
//     三角函数。
#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "app_clock.h"
#include "bsp_battery.h"
#include "civil_time.h"
#include "lvgl.h"
#include "places.h"
#include "solar.h"

#define COL_BG 0x0A1420
#define COL_INK 0xE7EEF8
#define COL_DIM 0x6B7A8D
#define COL_WEIHAI 0x4FB6FF
#define COL_TIANJIN 0xFFB454
#define COL_HORIZON 0x24384F

#define PLOT_X0 14
#define PLOT_X1 226
#define PLOT_Y0 92
#define PLOT_Y1 246
#define HORIZON_Y 196
#define DEG_SCALE 1.55
#define EL_CLAMP 56.0
#define ARC_SAMPLES 97 // 全天 24 小时，每 15 分钟一个采样点

#define VIEW_HINT "UP/DN PAGE   OK DIM   HOLD OFF"

static app_view_t s_view = APP_VIEW_ARC;
static bool s_setup_mode;
static bool s_net_mode;
static app_setup_t s_setup;

static lv_obj_t *s_scr;
static lv_obj_t *s_clock_lbl;
static lv_obj_t *s_batt_lbl;

static lv_obj_t *s_arc_line;
static lv_point_precise_t s_arc_pts[ARC_SAMPLES];
static lv_obj_t *s_dot[PLACE_COUNT];
static lv_obj_t *s_place_lbl[PLACE_COUNT];
static lv_obj_t *s_arc_summary;
static int64_t s_arc_day_midnight = INT64_MIN;

static lv_obj_t *s_pair_big[PLACE_COUNT];
static lv_obj_t *s_pair_sub[PLACE_COUNT];
static lv_obj_t *s_pair_delta;

static lv_obj_t *s_left_big;
static lv_obj_t *s_left_sub;
static lv_obj_t *s_left_next;

static lv_obj_t *s_setup_fields[SETUP_FIELD_COUNT];
static lv_obj_t *s_net_l1;
static lv_obj_t *s_net_l2;

// ---------------------------------------------------------------- 小工具

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), 0);
    lv_label_set_text(lbl, "");
    return lbl;
}

static lv_obj_t *make_dot(lv_obj_t *parent, uint32_t color, bool filled) {
    lv_obj_t *dot = lv_obj_create(parent);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 9, 9);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(dot, 2, 0);
    lv_obj_set_style_border_color(dot, lv_color_hex(color), 0);
    lv_obj_set_style_border_opa(dot, LV_OPA_COVER, 0);
    if (filled) {
        lv_obj_set_style_bg_color(dot, lv_color_hex(color), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    }
    return dot;
}

static lv_obj_t *base_screen(void) {
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    return scr;
}

// 建新页面并换过去，然后删掉旧页面：LVGL 不会自动回收被换下的 screen，
// 逐次切换不显式删除就会一直累积。
static void begin_screen(void) {
    lv_obj_t *previous = s_scr;
    s_scr = base_screen();
    lv_screen_load(s_scr);
    if (previous != NULL) {
        lv_obj_delete(previous);
    }
}

// 顶部：标题、时钟、电量（电量固定右上角，见 ai-guide §4）。
static void add_header(lv_obj_t *scr, const char *title) {
    lv_obj_t *t = make_label(scr, &lv_font_montserrat_14, COL_DIM);
    lv_label_set_text(t, title);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 8);

    s_clock_lbl = make_label(scr, &lv_font_montserrat_20, COL_INK);
    lv_label_set_text(s_clock_lbl, "--:--");
    lv_obj_align(s_clock_lbl, LV_ALIGN_TOP_MID, 0, 24);

    s_batt_lbl = make_label(scr, &lv_font_montserrat_14, COL_DIM);
    lv_label_set_text(s_batt_lbl, "B--");
    lv_obj_align(s_batt_lbl, LV_ALIGN_TOP_RIGHT, -8, 10);
}

static void add_footer_hint(lv_obj_t *scr) {
    lv_obj_t *hint = make_label(scr, &lv_font_montserrat_14, COL_DIM);
    lv_label_set_text(hint, VIEW_HINT);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -8);
}

// ---------------------------------------------------------------- 今日几何

typedef struct {
    int64_t midnight_utc;
    solar_day_t day; // 威海（曲线与"还剩多少光"都以它为准）
    int now_min;
} today_t;

static bool today_load(today_t *out) {
    const int64_t now = app_clock_now();
    if (now < APP_CLOCK_VALID_FROM) return false;
    out->midnight_utc = civil_time_local_midnight(now, PLACE_TZ_OFFSET_MIN);
    out->now_min = civil_time_minutes_of_day(now, PLACE_TZ_OFFSET_MIN);
    solar_compute_day(out->midnight_utc, PLACES[PLACE_WEIHAI].lat_deg, PLACES[PLACE_WEIHAI].lon_deg,
                      PLACE_TZ_OFFSET_MIN, &out->day);
    return true;
}

static double elevation_now(int place_index) {
    return solar_elevation_deg(app_clock_now(), PLACES[place_index].lat_deg,
                               PLACES[place_index].lon_deg);
}

static int plot_y(double elevation_deg) {
    const double el = fmax(-EL_CLAMP, fmin(EL_CLAMP, elevation_deg));
    const int y = HORIZON_Y - (int)lround(el * DEG_SCALE);
    return y < PLOT_Y0 ? PLOT_Y0 : (y > PLOT_Y1 ? PLOT_Y1 : y);
}

static int plot_x(int minutes_of_day) {
    const double t = (double)minutes_of_day / 1440.0;
    return PLOT_X0 + (int)lround(t * (double)(PLOT_X1 - PLOT_X0));
}

static void format_hhmm(char *buf, size_t n, double minutes) {
    const int m = solar_minutes_of_day(minutes);
    (void)snprintf(buf, n, "%02d:%02d", m / 60, m % 60);
}

// ---------------------------------------------------------------- ARC 页

static void arc_build(void) {
    begin_screen();
    add_header(s_scr, "TWO-PLACE LIGHT");

    s_arc_summary = make_label(s_scr, &lv_font_montserrat_14, COL_DIM);
    lv_label_set_text(s_arc_summary, "--");
    lv_obj_align(s_arc_summary, LV_ALIGN_TOP_MID, 0, 52);

    lv_obj_t *horizon = lv_obj_create(s_scr);
    lv_obj_remove_style_all(horizon);
    lv_obj_set_pos(horizon, PLOT_X0, HORIZON_Y);
    lv_obj_set_size(horizon, PLOT_X1 - PLOT_X0, 1);
    lv_obj_set_style_bg_color(horizon, lv_color_hex(COL_HORIZON), 0);
    lv_obj_set_style_bg_opa(horizon, LV_OPA_COVER, 0);

    s_arc_line = lv_line_create(s_scr);
    lv_obj_set_style_line_width(s_arc_line, 2, 0);
    lv_obj_set_style_line_color(s_arc_line, lv_color_hex(COL_DIM), 0);
    lv_obj_set_style_line_rounded(s_arc_line, true, 0);

    for (int i = 0; i < PLACE_COUNT; i++) {
        const uint32_t color = i == PLACE_WEIHAI ? COL_WEIHAI : COL_TIANJIN;
        s_dot[i] = make_dot(s_scr, color, i == PLACE_WEIHAI);
        s_place_lbl[i] = make_label(s_scr, &lv_font_montserrat_14, color);
        // 前缀就是图例：* = 实心点(威海)，o = 空心点(天津)，省出一行地方。
        lv_obj_set_pos(s_place_lbl[i], PLOT_X0, 252 + i * 18);
    }

    add_footer_hint(s_scr);
    s_arc_day_midnight = INT64_MIN;
}

static void arc_update(const today_t *t) {
    if (t->midnight_utc != s_arc_day_midnight) {
        for (int i = 0; i < ARC_SAMPLES; i++) {
            const int minutes = i * 1440 / (ARC_SAMPLES - 1);
            const int64_t utc = t->midnight_utc + (int64_t)minutes * 60;
            const double el = solar_elevation_deg(utc, PLACES[PLACE_WEIHAI].lat_deg,
                                                  PLACES[PLACE_WEIHAI].lon_deg);
            s_arc_pts[i].x = plot_x(minutes);
            s_arc_pts[i].y = plot_y(el);
        }
        lv_line_set_points(s_arc_line, s_arc_pts, ARC_SAMPLES);
        s_arc_day_midnight = t->midnight_utc;
    }

    char buf[48];
    const int dot_x = plot_x(t->now_min);
    for (int i = 0; i < PLACE_COUNT; i++) {
        const double el = elevation_now(i);
        lv_obj_set_pos(s_dot[i], dot_x - 4, plot_y(el) - 4);
        (void)snprintf(buf, sizeof buf, "%c %s %+.1f", i == PLACE_WEIHAI ? '*' : 'o',
                       PLACES[i].name, el);
        lv_label_set_text(s_place_lbl[i], buf);
    }

    char rise[8];
    char set[8];
    format_hhmm(rise, sizeof rise, t->day.rise_min);
    format_hhmm(set, sizeof set, t->day.set_min);
    const int daylen = solar_minutes_of_day(t->day.day_length_min);
    (void)snprintf(buf, sizeof buf, "UP %s  DOWN %s   %dh%02d", rise, set, daylen / 60,
                   daylen % 60);
    lv_label_set_text(s_arc_summary, buf);
}

// ---------------------------------------------------------------- PAIR 页

static void pair_build(void) {
    begin_screen();
    add_header(s_scr, "TWO-PLACE LIGHT");
    for (int i = 0; i < PLACE_COUNT; i++) {
        const uint32_t color = i == PLACE_WEIHAI ? COL_WEIHAI : COL_TIANJIN;
        const int y = 84 + i * 88;

        lv_obj_t *name = make_label(s_scr, &lv_font_montserrat_14, color);
        lv_label_set_text(name, PLACES[i].name);
        lv_obj_set_pos(name, 16, y);

        s_pair_big[i] = make_label(s_scr, &lv_font_montserrat_20, COL_INK);
        lv_label_set_text(s_pair_big[i], "--");
        lv_obj_align(s_pair_big[i], LV_ALIGN_TOP_RIGHT, -16, y - 6);

        s_pair_sub[i] = make_label(s_scr, &lv_font_montserrat_14, COL_DIM);
        lv_label_set_text(s_pair_sub[i], "--");
        lv_obj_set_pos(s_pair_sub[i], 16, y + 22);
    }

    s_pair_delta = make_label(s_scr, &lv_font_montserrat_14, COL_INK);
    lv_label_set_text(s_pair_delta, "--");
    lv_obj_align(s_pair_delta, LV_ALIGN_TOP_MID, 0, 264);

    add_footer_hint(s_scr);
}

static void pair_update(const today_t *t) {
    char buf[48];
    double el[PLACE_COUNT];
    for (int i = 0; i < PLACE_COUNT; i++) {
        el[i] = elevation_now(i);
        (void)snprintf(buf, sizeof buf, "%+.1f DEG", el[i]);
        lv_label_set_text(s_pair_big[i], buf);

        solar_day_t day;
        solar_compute_day(t->midnight_utc, PLACES[i].lat_deg, PLACES[i].lon_deg,
                          PLACE_TZ_OFFSET_MIN, &day);
        char rise[8];
        char set[8];
        format_hhmm(rise, sizeof rise, day.rise_min);
        format_hhmm(set, sizeof set, day.set_min);
        (void)snprintf(buf, sizeof buf, "UP %s   DOWN %s", rise, set);
        lv_label_set_text(s_pair_sub[i], buf);
    }

    const double delta = el[PLACE_WEIHAI] - el[PLACE_TIANJIN];
    if (fabs(delta) < 0.1) {
        lv_label_set_text(s_pair_delta, "SAME LIGHT");
        lv_obj_set_style_text_color(s_pair_delta, lv_color_hex(COL_DIM), 0);
    } else {
        const bool haihigher = delta > 0.0;
        (void)snprintf(buf, sizeof buf, "%s %+.1f DEG HIGHER",
                       haihigher ? PLACES[PLACE_WEIHAI].name : PLACES[PLACE_TIANJIN].name,
                       fabs(delta));
        lv_label_set_text(s_pair_delta, buf);
        lv_obj_set_style_text_color(s_pair_delta,
                                    lv_color_hex(haihigher ? COL_WEIHAI : COL_TIANJIN), 0);
    }
}

// ---------------------------------------------------------------- LEFT 页

static void left_build(void) {
    begin_screen();
    add_header(s_scr, "LIGHT LEFT TODAY");

    s_left_big = make_label(s_scr, &lv_font_montserrat_20, COL_INK);
    lv_label_set_text(s_left_big, "--");
    lv_obj_align(s_left_big, LV_ALIGN_TOP_MID, 0, 96);

    s_left_sub = make_label(s_scr, &lv_font_montserrat_14, COL_DIM);
    lv_label_set_text(s_left_sub, "--");
    lv_obj_align(s_left_sub, LV_ALIGN_TOP_MID, 0, 132);

    s_left_next = make_label(s_scr, &lv_font_montserrat_14, COL_DIM);
    lv_label_set_text(s_left_next, "--");
    lv_obj_align(s_left_next, LV_ALIGN_TOP_MID, 0, 176);

    add_footer_hint(s_scr);
}

static void left_update(const today_t *t) {
    char buf[64];
    char rise[8];
    char set[8];
    char dawn[8];
    char dusk[8];
    format_hhmm(rise, sizeof rise, t->day.rise_min);
    format_hhmm(set, sizeof set, t->day.set_min);
    format_hhmm(dawn, sizeof dawn, t->day.dawn_min);
    format_hhmm(dusk, sizeof dusk, t->day.dusk_min);

    const double now = (double)t->now_min;
    const bool sun_up = now >= t->day.rise_min && now < t->day.set_min;

    if (sun_up) {
        const int left = solar_minutes_of_day(t->day.set_min - now);
        (void)snprintf(buf, sizeof buf, "SUNSET IN %dh%02d", left / 60, left % 60);
    } else {
        const double next_rise = now < t->day.rise_min ? t->day.rise_min : t->day.rise_min + 1440.0;
        const int left = solar_minutes_of_day(next_rise - now);
        (void)snprintf(buf, sizeof buf, "SUNRISE IN %dh%02d", left / 60, left % 60);
    }
    lv_label_set_text(s_left_big, buf);

    (void)snprintf(buf, sizeof buf, "DAWN %s  DUSK %s\nUP %s  DOWN %s", dawn, dusk, rise, set);
    lv_label_set_text(s_left_sub, buf);

    // 明天比今天多/少多少光：这是这台设备最想说的一句话。
    solar_day_t tomorrow;
    solar_compute_day(t->midnight_utc + 86400, PLACES[PLACE_WEIHAI].lat_deg,
                      PLACES[PLACE_WEIHAI].lon_deg, PLACE_TZ_OFFSET_MIN, &tomorrow);
    const int tomorrow_len = solar_minutes_of_day(tomorrow.day_length_min);
    const int delta_min = (int)lround(tomorrow.day_length_min - t->day.day_length_min);
    (void)snprintf(buf, sizeof buf, "TOMORROW %dh%02d   %+d MIN", tomorrow_len / 60,
                   tomorrow_len % 60, delta_min);
    lv_label_set_text(s_left_next, buf);
}

// ---------------------------------------------------------------- 对时页

static void setup_build(void) {
    begin_screen();
    add_header(s_scr, "SET TIME");

    lv_obj_t *hint = make_label(s_scr, &lv_font_montserrat_14, COL_DIM);
    lv_label_set_text(hint, "UP/DN ADJUST\nOK NEXT\nHOLD OK CANCEL");
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 216);

    for (int i = 0; i < SETUP_FIELD_COUNT; i++) {
        s_setup_fields[i] = make_label(s_scr, &lv_font_montserrat_20, COL_INK);
        lv_label_set_text(s_setup_fields[i], "--");
        if (i <= SETUP_FIELD_DAY) {
            lv_obj_align(s_setup_fields[i], LV_ALIGN_TOP_LEFT, 16 + i * 70, 112);
        } else {
            lv_obj_align(s_setup_fields[i], LV_ALIGN_TOP_LEFT, 16 + (i - 3) * 70, 160);
        }
    }
}

static void setup_update(void) {
    char buf[8];
    for (int i = 0; i < SETUP_FIELD_COUNT; i++) {
        int value = 0;
        const char *fmt = "%02d";
        switch (i) {
        case SETUP_FIELD_YEAR:
            value = s_setup.year;
            fmt = "%04d";
            break;
        case SETUP_FIELD_MONTH:
            value = s_setup.month;
            break;
        case SETUP_FIELD_DAY:
            value = s_setup.day;
            break;
        case SETUP_FIELD_HOUR:
            value = s_setup.hour;
            break;
        case SETUP_FIELD_MINUTE:
            value = s_setup.minute;
            break;
        default:
            break;
        }
        (void)snprintf(buf, sizeof buf, fmt, value);
        lv_label_set_text(s_setup_fields[i], buf);
        const bool active = (int)s_setup.field == i;
        lv_obj_set_style_text_color(s_setup_fields[i],
                                    lv_color_hex(active ? COL_WEIHAI : COL_INK), 0);
    }
}

// ---------------------------------------------------------------- 联网页

static void net_build(const char *title, const char *line1, const char *line2) {
    begin_screen();
    add_header(s_scr, title);

    s_net_l1 = make_label(s_scr, &lv_font_montserrat_20, COL_INK);
    lv_label_set_text(s_net_l1, line1 == NULL ? "" : line1);
    lv_obj_set_style_text_align(s_net_l1, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_net_l1, LV_ALIGN_TOP_MID, 0, 120);

    s_net_l2 = make_label(s_scr, &lv_font_montserrat_14, COL_DIM);
    lv_label_set_text(s_net_l2, line2 == NULL ? "" : line2);
    lv_obj_set_style_text_align(s_net_l2, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_net_l2, LV_ALIGN_TOP_MID, 0, 156);

    lv_obj_t *hint = make_label(s_scr, &lv_font_montserrat_14, COL_DIM);
    lv_label_set_text(hint, "HOLD OK: MANUAL SET");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -8);
}

void ui_show_net(const char *title, const char *line1, const char *line2) {
    s_net_mode = true;
    net_build(title, line1, line2);
}

// ---------------------------------------------------------------- 公共入口

void ui_show_view(app_view_t view) {
    s_view = view;
    s_setup_mode = false;
    s_net_mode = false;
    switch (view) {
    case APP_VIEW_PAIR:
        pair_build();
        break;
    case APP_VIEW_LEFT:
        left_build();
        break;
    case APP_VIEW_ARC:
    default:
        s_view = APP_VIEW_ARC;
        arc_build();
        break;
    }
}

void ui_show_setup(const app_setup_t *setup) {
    if (setup == NULL) return;
    s_setup = *setup;
    s_setup_mode = true;
    s_net_mode = false;
    setup_build();
}

static void update_battery(void) {
    const int soc = bsp_battery_soc();
    char buf[16];
    if (soc < 0) {
        (void)snprintf(buf, sizeof buf, "B--");
    } else {
        (void)snprintf(buf, sizeof buf, "B%d%%", soc);
    }
    lv_label_set_text(s_batt_lbl, buf);
}

void ui_refresh(void) {
    if (s_scr == NULL) return;

    if (s_net_mode) {
        update_battery();
        return;
    }

    if (s_setup_mode) {
        setup_update();
        return;
    }

    const int64_t now = app_clock_now();
    char clock[16];
    if (now < APP_CLOCK_VALID_FROM) {
        (void)snprintf(clock, sizeof clock, "--:--");
    } else {
        const int minutes = civil_time_minutes_of_day(now, PLACE_TZ_OFFSET_MIN);
        (void)snprintf(clock, sizeof clock, "%02d:%02d", minutes / 60, minutes % 60);
    }
    lv_label_set_text(s_clock_lbl, clock);
    update_battery();

    today_t t;
    if (!today_load(&t)) return;

    switch (s_view) {
    case APP_VIEW_ARC:
        arc_update(&t);
        break;
    case APP_VIEW_PAIR:
        pair_update(&t);
        break;
    case APP_VIEW_LEFT:
        left_update(&t);
        break;
    default:
        break;
    }
}
