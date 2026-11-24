// main/places.c
#include "places.h"

#include <string.h>

const place_t PLACES_DEFAULT[PLACE_COUNT] = {
    [PLACE_PRIMARY] = {.name = "WEIHAI", .lat_deg = 37.51, .lon_deg = 122.12},
    [PLACE_SECONDARY] = {.name = "TIANJIN", .lat_deg = 39.08, .lon_deg = 117.20},
};

static place_t s_places[PLACE_COUNT];
static int s_tz_offset_min = PLACES_DEFAULT_TZ_OFFSET_MIN;
static bool s_ready;

static void ensure_ready(void) {
    if (!s_ready) places_init();
}

void places_init(void) {
    for (int i = 0; i < PLACE_COUNT; i++) {
        s_places[i] = PLACES_DEFAULT[i];
    }
    s_tz_offset_min = PLACES_DEFAULT_TZ_OFFSET_MIN;
    s_ready = true;
}

const place_t *places_get(int index) {
    if (index < 0 || index >= PLACE_COUNT) return NULL;
    ensure_ready();
    return &s_places[index];
}

int places_tz_offset_min(void) {
    ensure_ready();
    return s_tz_offset_min;
}

bool places_name_valid(const char *name) {
    if (name == NULL) return false;
    const size_t len = strlen(name);
    if (len == 0 || len > PLACE_NAME_MAX) return false;

    bool has_alnum = false;
    for (size_t i = 0; i < len; i++) {
        const char c = name[i];
        const bool alpha = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
        const bool digit = c >= '0' && c <= '9';
        if (alpha || digit) {
            has_alnum = true;
        } else if (c != ' ' && c != '-' && c != '_') {
            // 中文字符、标点、尖括号一律拒绝：既没有字形，也别让页面被注入。
            return false;
        }
    }
    // 允许中间有空格（"NEW YORK"），但不允许整串只有空格和符号。
    return has_alnum;
}

bool places_coord_valid(double lat_deg, double lon_deg) {
    // 同时排除 NaN 与无穷：NaN 与任何数比较都是 false，所以取反条件能拦住它。
    if (!(lat_deg >= -90.0 && lat_deg <= 90.0)) return false;
    if (!(lon_deg >= -180.0 && lon_deg <= 180.0)) return false;
    return true;
}

bool places_tz_valid(int minutes) {
    if (minutes < PLACE_TZ_MIN || minutes > PLACE_TZ_MAX) return false;
    return minutes % 15 == 0;
}

bool places_set(int index, const char *name, double lat_deg, double lon_deg) {
    if (index < 0 || index >= PLACE_COUNT) return false;
    if (!places_name_valid(name)) return false;
    if (!places_coord_valid(lat_deg, lon_deg)) return false;

    ensure_ready();
    memcpy(s_places[index].name, name, strlen(name) + 1);
    s_places[index].lat_deg = lat_deg;
    s_places[index].lon_deg = lon_deg;
    return true;
}

bool places_set_tz(int minutes) {
    if (!places_tz_valid(minutes)) return false;
    ensure_ready();
    s_tz_offset_min = minutes;
    return true;
}
