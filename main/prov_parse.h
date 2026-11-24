// main/prov_parse.h —— 配网页表单解析。纯函数，可在宿主机上测。
//
// 设备在配网/设置模式下开一个 SoftAP，手机浏览器提交一个表单，包含两部分：
//   ssid=<SSID>&pass=<密码>                               （可选：留空表示不改 Wi-Fi）
//   n0=<名字>&la0=<纬度>&lo0=<经度>&n1=..&la1=..&lo1=..&tz=<分钟>
// 这里负责 URL 解码、字段提取与范围校验。Wi-Fi 规则按 802.11 的实际约束：
// SSID 1..32 字节（可以含空格），密码要么留空（开放网络），要么 8..63 字节。
// 坐标与时区规则来自 places.h，屏上字体没有中文，所以名字限制为 ASCII。
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "places.h"

typedef enum {
    PROV_OK = 0,
    PROV_ERR_FORMAT, // 不是 key=value&key=value 形式，或缺少必填字段
    PROV_ERR_SSID,   // SSID 为空或超过 32 字节
    PROV_ERR_PASS,   // 密码既不是空也不算合法 WPA2 长度
    PROV_ERR_BUFFER, // 输出缓冲区放不下
    PROV_ERR_PLACE,  // 城市名或经纬度不合法
    PROV_ERR_TZ,     // 时区偏移不合法
} prov_result_t;

// URL 解码：把 %XX 还原成字节，'+' 还原成空格。始终以 NUL 结尾。
// 返回值写入 dst 的字节数（不含结尾 NUL）；dst 太小时截断。
size_t prov_url_decode(const char *src, char *dst, size_t dst_cap);

// 解析纯 Wi-Fi 表单（保留给最老的入口与既有测试）。
prov_result_t prov_parse_form(const char *body, size_t len, char *ssid, size_t ssid_cap,
                              char *pass, size_t pass_cap);

// 设置页提交的内容。
typedef struct {
    bool has_wifi; // ssid 非空才更新 Wi-Fi 凭证；留空表示沿用现有凭证
    char ssid[33];
    char pass[64];
    char name[PLACE_COUNT][PLACE_NAME_MAX + 1];
    double lat_deg[PLACE_COUNT];
    double lon_deg[PLACE_COUNT];
    int tz_offset_min;
} prov_settings_t;

// 解析设置页表单。成功时 out 里的每一项都已经是校验过的值，可以直接落库。
prov_result_t prov_parse_settings_form(const char *body, size_t len, prov_settings_t *out);
