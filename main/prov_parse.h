// main/prov_parse.h —— 配网表单解析。纯函数，可在宿主机上测。
//
// 设备在配网模式下开一个 SoftAP，手机浏览器提交
//   ssid=<SSID>&pass=<密码>
// 这里负责 URL 解码、字段提取与长度校验。校验规则按 802.11 的实际约束：
// SSID 1..32 字节（可以含空格），密码要么留空（开放网络），要么 8..63 字节。
#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    PROV_OK = 0,
    PROV_ERR_FORMAT, // 不是 key=value&key=value 形式，或缺 ssid
    PROV_ERR_SSID,   // SSID 为空或超过 32 字节
    PROV_ERR_PASS,   // 密码既不是空也不算合法 WPA2 长度
    PROV_ERR_BUFFER, // 输出缓冲区放不下
} prov_result_t;

// URL 解码：把 %XX 还原成字节，'+' 还原成空格。始终以 NUL 结尾。
// 返回值写入 dst 的字节数（不含结尾 NUL）；dst 太小时截断。
size_t prov_url_decode(const char *src, char *dst, size_t dst_cap);

// 解析表单正文。ssid/pass 缓冲区不足或字段非法时返回对应的错误码，
// 并且不会把半截字符串留在缓冲区里（失败时写入空串）。
prov_result_t prov_parse_form(const char *body, size_t len, char *ssid, size_t ssid_cap,
                              char *pass, size_t pass_cap);
