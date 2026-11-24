// main/prov_parse.c
#include "prov_parse.h"

#include <stdlib.h>
#include <string.h>

#define PROV_SSID_MAX 32
#define PROV_PASS_MIN 8
#define PROV_PASS_MAX 63

// 字段值的上限：SSID 最多 32 字节，URL 编码后最多 96 字节；这里留足余量。
#define PROV_RAW_MAX 128

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

size_t prov_url_decode(const char *src, char *dst, size_t dst_cap) {
    if (dst == NULL || dst_cap == 0) return 0;
    size_t out = 0;
    if (src != NULL) {
        for (size_t i = 0; src[i] != '\0'; i++) {
            char c = src[i];
            if (c == '+') {
                c = ' ';
            } else if (c == '%' && src[i + 1] != '\0' && src[i + 2] != '\0') {
                const int hi = hex_value(src[i + 1]);
                const int lo = hex_value(src[i + 2]);
                if (hi >= 0 && lo >= 0) {
                    c = (char)((hi << 4) | lo);
                    i += 2;
                } else {
                    c = '%'; // 非法的百分号转义按字面保留
                }
            }
            if (out + 1 < dst_cap) {
                dst[out++] = c;
            } else {
                break;
            }
        }
    }
    dst[out] = '\0';
    return out;
}

typedef enum {
    FIELD_MISSING = 0,
    FIELD_OK,
    FIELD_TOO_LONG, // 值解出来放不进调用方的缓冲区
} field_result_t;

// 取出 name 字段的原始值（已 URL 解码）写入 dst。
static field_result_t extract_field(const char *body, const char *name, char *dst,
                                   size_t dst_cap) {
    const size_t name_len = strlen(name);
    size_t i = 0;
    while (body[i] != '\0') {
        const size_t field_start = i;
        size_t eq = field_start;
        while (body[eq] != '\0' && body[eq] != '=' && body[eq] != '&') eq++;
        if (body[eq] == '=' && eq - field_start == name_len &&
            strncmp(body + field_start, name, name_len) == 0) {
            const size_t value_start = eq + 1;
            size_t value_end = value_start;
            while (body[value_end] != '\0' && body[value_end] != '&') value_end++;

            const size_t raw_len = value_end - value_start;
            if (raw_len >= PROV_RAW_MAX) return FIELD_TOO_LONG;

            char raw[PROV_RAW_MAX];
            memcpy(raw, body + value_start, raw_len);
            raw[raw_len] = '\0';

            char decoded[PROV_RAW_MAX];
            const size_t decoded_len = prov_url_decode(raw, decoded, sizeof decoded);
            if (decoded_len >= dst_cap) return FIELD_TOO_LONG;
            memcpy(dst, decoded, decoded_len + 1);
            return FIELD_OK;
        }
        // 跳到下一个字段
        i = field_start;
        while (body[i] != '\0' && body[i] != '&') i++;
        if (body[i] == '&') i++;
    }
    return FIELD_MISSING;
}

prov_result_t prov_parse_form(const char *body, size_t len, char *ssid, size_t ssid_cap,
                              char *pass, size_t pass_cap) {
    if (ssid == NULL || pass == NULL || ssid_cap == 0 || pass_cap == 0) return PROV_ERR_BUFFER;
    ssid[0] = '\0';
    pass[0] = '\0';

    if (body == NULL || len == 0) return PROV_ERR_FORMAT;

    // 只处理不含 NUL 的正文；复制一份，保证下面按字符串处理时不会越界。
    char local[192];
    if (len >= sizeof(local)) return PROV_ERR_FORMAT;
    memcpy(local, body, len);
    local[len] = '\0';

    char buf[PROV_RAW_MAX];
    const field_result_t ssid_result = extract_field(local, "ssid", buf, sizeof buf);
    if (ssid_result == FIELD_MISSING) return PROV_ERR_FORMAT;
    if (ssid_result == FIELD_TOO_LONG) return PROV_ERR_FORMAT;

    const size_t ssid_len = strlen(buf);
    if (ssid_len == 0 || ssid_len > PROV_SSID_MAX) return PROV_ERR_SSID;
    if (ssid_len >= ssid_cap) return PROV_ERR_BUFFER;

    char pass_buf[PROV_RAW_MAX];
    pass_buf[0] = '\0';
    const field_result_t pass_result = extract_field(local, "pass", pass_buf, sizeof pass_buf);
    if (pass_result == FIELD_TOO_LONG) return PROV_ERR_FORMAT;
    const size_t pass_len = strlen(pass_buf);
    if (pass_len != 0 && (pass_len < PROV_PASS_MIN || pass_len > PROV_PASS_MAX)) {
        return PROV_ERR_PASS;
    }
    if (pass_len >= pass_cap) return PROV_ERR_BUFFER;

    memcpy(ssid, buf, ssid_len + 1);
    memcpy(pass, pass_buf, pass_len + 1);
    return PROV_OK;
}

// ------------------------------------------------------------------ 设置页

// 表单正文上限：九个字段 + URL 编码余量，留得比 1 Wi-Fi 表单宽。
#define PROV_SETTINGS_MAX 512

static bool parse_double_field(const char *text, double *out) {
    if (text == NULL || text[0] == '\0') return false;
    char *end = NULL;
    const double value = strtod(text, &end);
    if (end == NULL || *end != '\0') return false;
    *out = value;
    return true;
}

static bool parse_int_field(const char *text, int *out) {
    if (text == NULL || text[0] == '\0') return false;
    char *end = NULL;
    const long value = strtol(text, &end, 10);
    if (end == NULL || *end != '\0') return false;
    if (value < -100000L || value > 100000L) return false;
    *out = (int)value;
    return true;
}

// ssid 留空表示"沿用现有凭证"，不算错误。
static prov_result_t parse_optional_wifi(const char *body, prov_settings_t *out) {
    char buf[PROV_RAW_MAX];
    const field_result_t ssid_result = extract_field(body, "ssid", buf, sizeof buf);
    if (ssid_result == FIELD_TOO_LONG) return PROV_ERR_FORMAT;
    if (ssid_result == FIELD_MISSING || buf[0] == '\0') {
        out->has_wifi = false;
        return PROV_OK;
    }

    const size_t ssid_len = strlen(buf);
    if (ssid_len > PROV_SSID_MAX) return PROV_ERR_SSID;

    char pass_buf[PROV_RAW_MAX];
    pass_buf[0] = '\0';
    const field_result_t pass_result = extract_field(body, "pass", pass_buf, sizeof pass_buf);
    if (pass_result == FIELD_TOO_LONG) return PROV_ERR_FORMAT;
    const size_t pass_len = strlen(pass_buf);
    if (pass_len != 0 && (pass_len < PROV_PASS_MIN || pass_len > PROV_PASS_MAX)) {
        return PROV_ERR_PASS;
    }

    memcpy(out->ssid, buf, ssid_len + 1);
    memcpy(out->pass, pass_buf, pass_len + 1);
    out->has_wifi = true;
    return PROV_OK;
}

prov_result_t prov_parse_settings_form(const char *body, size_t len, prov_settings_t *out) {
    if (out == NULL) return PROV_ERR_BUFFER;
    memset(out, 0, sizeof *out);
    out->tz_offset_min = PLACES_DEFAULT_TZ_OFFSET_MIN;

    if (body == NULL || len == 0) return PROV_ERR_FORMAT;

    char local[PROV_SETTINGS_MAX];
    if (len >= sizeof local) return PROV_ERR_FORMAT;
    memcpy(local, body, len);
    local[len] = '\0';

    static const char *const name_keys[PLACE_COUNT] = {"n0", "n1"};
    static const char *const lat_keys[PLACE_COUNT] = {"la0", "la1"};
    static const char *const lon_keys[PLACE_COUNT] = {"lo0", "lo1"};

    char buf[PROV_RAW_MAX];
    for (int i = 0; i < PLACE_COUNT; i++) {
        if (extract_field(local, name_keys[i], buf, sizeof buf) != FIELD_OK) {
            return PROV_ERR_FORMAT;
        }
        if (!places_name_valid(buf)) return PROV_ERR_PLACE;
        memcpy(out->name[i], buf, strlen(buf) + 1);

        if (extract_field(local, lat_keys[i], buf, sizeof buf) != FIELD_OK) {
            return PROV_ERR_FORMAT;
        }
        if (!parse_double_field(buf, &out->lat_deg[i])) return PROV_ERR_PLACE;

        if (extract_field(local, lon_keys[i], buf, sizeof buf) != FIELD_OK) {
            return PROV_ERR_FORMAT;
        }
        if (!parse_double_field(buf, &out->lon_deg[i])) return PROV_ERR_PLACE;

        // 单看每个数都不越界，但放在一起才算一对合法坐标（也顺手挡住 NaN）。
        if (!places_coord_valid(out->lat_deg[i], out->lon_deg[i])) return PROV_ERR_PLACE;
    }

    if (extract_field(local, "tz", buf, sizeof buf) != FIELD_OK) return PROV_ERR_FORMAT;
    if (!parse_int_field(buf, &out->tz_offset_min)) return PROV_ERR_TZ;
    if (!places_tz_valid(out->tz_offset_min)) return PROV_ERR_TZ;

    return parse_optional_wifi(local, out);
}
