// main/net_wifi.h —— 联网与配网。
//
// 两条路：
//   1. STA：用 NVS 里存着的凭证连路由器，等 DHCP 给出 IP。
//   2. 配网：开一个 SoftAP（TwoPlaceLight / twoplacelight），手机上打开
//      http://192.168.4.1 填 Wi-Fi 名和密码，设备存进 NVS 后重启，再走路径 1。
//
// 没网也能用：对时的最后手段仍是三键手动对时（app_setup.c）。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

// 准备网络栈（esp_netif + 默认事件循环）。幂等，成功后可重复调用。
esp_err_t net_boot(void);

// 阻塞等待期间每隔 100 ms 询问一次：返回 true 表示用户要求中止（例如长按确定
// 改用手动对时）。中止时阻塞函数返回 ESP_ERR_INVALID_STATE。
typedef bool (*net_cancel_cb_t)(void);

// NVS 里是否已经有一组凭证。
bool net_has_credentials(void);

// 保存凭证到 NVS（不做连接）。ssid 1..32 字节，pass 为空或 8..63 字节。
esp_err_t net_save_credentials(const char *ssid, const char *pass);

// 以 STA 连接并等待拿到 IP。超时返回 ESP_ERR_TIMEOUT；失败原因按 esp_err_t 透传。
esp_err_t net_connect_sta(uint32_t timeout_ms, net_cancel_cb_t cancel);

// 开 SoftAP 配置页并等待用户提交。提交成功后本函数内部重启设备，不返回；
// 超时返回 ESP_ERR_TIMEOUT；被中止返回 ESP_ERR_INVALID_STATE。
esp_err_t net_start_provisioning(uint32_t timeout_ms, net_cancel_cb_t cancel);

// 关掉 Wi-Fi 与配网服务（网络栈本身保留给后续复用）。
void net_stop(void);
