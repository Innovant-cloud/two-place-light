// main/net_wifi.c
#include "net_wifi.h"

#include <inttypes.h>
#include <string.h>

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "prov_parse.h"

static const char *TAG = "net_wifi";

#define CRED_NAMESPACE "wifi"
#define CRED_KEY_SSID "ssid"
#define CRED_KEY_PASS "pass"

#define AP_SSID "TwoPlaceLight"
// WPA2 要求 8..63 字节；这个字符串是阿渊定的，就按他的来。
#define AP_PASSWORD "lingan-is-so-cute"
#define POLL_PERIOD_MS 100

static esp_netif_t *s_sta_netif;
static esp_netif_t *s_ap_netif;
static httpd_handle_t s_httpd;
static esp_event_handler_instance_t s_wifi_handler;
static esp_event_handler_instance_t s_ip_handler;
static bool s_wifi_inited;
static bool s_wifi_running;
static bool s_handlers_registered;
static volatile bool s_got_ip;
static volatile bool s_provisioned;

// ------------------------------------------------------------------ 网络栈

esp_err_t net_boot(void) {
    static bool netif_ready;
    static bool loop_ready;

    if (!netif_ready) {
        const esp_err_t err = esp_netif_init();
        if (err != ESP_OK) return err;
        netif_ready = true;
    }
    if (!loop_ready) {
        const esp_err_t err = esp_event_loop_create_default();
        if (err != ESP_OK) return err;
        loop_ready = true;
    }
    return ESP_OK;
}

// ------------------------------------------------------------------ 凭证

static bool load_credentials(char *ssid, size_t ssid_cap, char *pass, size_t pass_cap) {
    nvs_handle_t handle;
    if (nvs_open(CRED_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return false;

    size_t ssid_len = ssid_cap;
    esp_err_t err = nvs_get_str(handle, CRED_KEY_SSID, ssid, &ssid_len);
    if (err == ESP_OK) {
        size_t pass_len = pass_cap;
        err = nvs_get_str(handle, CRED_KEY_PASS, pass, &pass_len);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            pass[0] = '\0'; // 开放网络
            err = ESP_OK;
        }
    }
    nvs_close(handle);
    return err == ESP_OK && ssid[0] != '\0';
}

bool net_has_credentials(void) {
    char ssid[40] = {0};
    char pass[72] = {0};
    return load_credentials(ssid, sizeof ssid, pass, sizeof pass);
}

esp_err_t net_save_credentials(const char *ssid, const char *pass) {
    if (ssid == NULL || ssid[0] == '\0') return ESP_ERR_INVALID_ARG;
    const size_t ssid_len = strlen(ssid);
    const size_t pass_len = pass == NULL ? 0 : strlen(pass);
    if (ssid_len > 32) return ESP_ERR_INVALID_ARG;
    if (pass_len != 0 && (pass_len < 8 || pass_len > 63)) return ESP_ERR_INVALID_ARG;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(CRED_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_str(handle, CRED_KEY_SSID, ssid);
    if (err == ESP_OK) err = nvs_set_str(handle, CRED_KEY_PASS, pass == NULL ? "" : pass);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

// ------------------------------------------------------------------ 事件

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg;
    (void)base;
    (void)data;
    if (id == WIFI_EVENT_STA_START) {
        (void)esp_wifi_connect();
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        s_got_ip = false;
        ESP_LOGW(TAG, "STA 断开");
    } else if (id == WIFI_EVENT_AP_STACONNECTED) {
        ESP_LOGI(TAG, "有设备连上配网热点");
    }
}

static void on_ip_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg;
    (void)base;
    (void)data;
    if (id == IP_EVENT_STA_GOT_IP) {
        s_got_ip = true;
        ESP_LOGI(TAG, "已拿到 IP");
    }
}

static esp_err_t register_handlers(void) {
    if (s_handlers_registered) return ESP_OK;
    esp_err_t err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        on_wifi_event, NULL, &s_wifi_handler);
    if (err != ESP_OK) return err;
    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip_event, NULL,
                                              &s_ip_handler);
    if (err != ESP_OK) {
        (void)esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_wifi_handler);
        return err;
    }
    s_handlers_registered = true;
    return ESP_OK;
}

// ------------------------------------------------------------------ 启动顺序

// 初始化驱动（幂等）并设定模式；真正的 esp_wifi_start() 由调用方在配置好
// SSID/AP 参数之后再做，免得 STA_START 事件先带着空配置去连接一次。
static esp_err_t wifi_prepare(wifi_mode_t mode) {
    if (s_wifi_running) {
        (void)esp_wifi_stop();
        s_wifi_running = false;
    }
    if (!s_wifi_inited) {
        const wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
        esp_err_t err = esp_wifi_init(&init_cfg);
        if (err != ESP_OK) return err;
        // 凭证由本应用自己存 NVS，不让 Wi-Fi 驱动再存一份。
        err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
        if (err == ESP_OK) err = register_handlers();
        if (err != ESP_OK) {
            (void)esp_wifi_deinit();
            return err;
        }
        s_wifi_inited = true;
    }
    return esp_wifi_set_mode(mode);
}

static esp_err_t wifi_launch(void) {
    const esp_err_t err = esp_wifi_start();
    if (err == ESP_OK) {
        s_wifi_running = true;
    }
    return err;
}

// ------------------------------------------------------------------ STA

esp_err_t net_connect_sta(uint32_t timeout_ms, net_cancel_cb_t cancel) {
    char ssid[40] = {0};
    char pass[72] = {0};
    if (!load_credentials(ssid, sizeof ssid, pass, sizeof pass)) {
        ESP_LOGW(TAG, "没有可用的 Wi-Fi 凭证");
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t err = net_boot();
    if (err != ESP_OK) return err;

    if (s_sta_netif == NULL) {
        s_sta_netif = esp_netif_create_default_wifi_sta();
        if (s_sta_netif == NULL) return ESP_ERR_NO_MEM;
    }

    err = wifi_prepare(WIFI_MODE_STA);
    if (err != ESP_OK) return err;

    wifi_config_t wifi_cfg = {0};
    memcpy(wifi_cfg.sta.ssid, ssid, strlen(ssid));
    memcpy(wifi_cfg.sta.password, pass, strlen(pass));
    wifi_cfg.sta.threshold.authmode = pass[0] == '\0' ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
    if (err != ESP_OK) return err;

    s_got_ip = false;
    err = wifi_launch();
    if (err != ESP_OK) return err;
    // STA_START 里已经会 connect 一次；这里再补一次，覆盖"驱动已经启动过"的情况。
    (void)esp_wifi_connect();

    for (uint32_t waited = 0; waited < timeout_ms; waited += POLL_PERIOD_MS) {
        if (s_got_ip) return ESP_OK;
        if (cancel != NULL && cancel()) return ESP_ERR_INVALID_STATE;
        vTaskDelay(pdMS_TO_TICKS(POLL_PERIOD_MS));
    }
    ESP_LOGW(TAG, "连接超时 (%" PRIu32 " ms)", timeout_ms);
    return ESP_ERR_TIMEOUT;
}

// ------------------------------------------------------------------ 配网页

static const char PROV_PAGE[] =
    "<!doctype html><html><head><meta charset=utf-8>"
    "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
    "<title>Two-Place Light</title><style>"
    "body{font-family:system-ui,sans-serif;background:#0a1420;color:#e7eef8;padding:24px;"
    "max-width:420px;margin:0 auto}h1{font-size:20px;color:#4fb6ff}"
    "label{display:block;margin-top:16px;font-size:14px;color:#9fb0c4}"
    "input{width:100%;box-sizing:border-box;padding:10px;margin-top:6px;border:0;border-radius:6px;"
    "font-size:16px}button{width:100%;margin-top:24px;padding:12px;border:0;border-radius:6px;"
    "background:#4fb6ff;color:#08202f;font-size:16px;font-weight:600}"
    "p{font-size:12px;color:#6b7a8d}</style></head><body>"
    "<h1>Two-Place Light</h1>"
    "<form method=post action=/save>"
    "<label>Wi-Fi name</label><input name=ssid maxlength=32 required autofocus>"
    "<label>Password</label><input name=pass type=password maxlength=63>"
    "<button type=submit>Save and restart</button></form>"
    "<p>Leave the password empty for an open network.</p>"
    "</body></html>";

static const char SAVED_PAGE[] =
    "<!doctype html><meta charset=utf-8>"
    "<body style=\"font-family:system-ui;background:#0a1420;color:#e7eef8;padding:24px\">"
    "<h2>Saved</h2><p>The device is restarting and will connect to this network.</p></body>";

static esp_err_t handle_root(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, PROV_PAGE, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t handle_save(httpd_req_t *req) {
    char body[192];
    const int received = httpd_req_recv(req, body, sizeof(body) - 1);
    if (received <= 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty body");
    }
    body[received] = '\0';

    char ssid[40] = {0};
    char pass[72] = {0};
    const prov_result_t parsed =
        prov_parse_form(body, (size_t)received, ssid, sizeof ssid, pass, sizeof pass);
    if (parsed != PROV_OK) {
        const char *message = "invalid request";
        if (parsed == PROV_ERR_SSID) message = "wifi name must be 1-32 bytes";
        if (parsed == PROV_ERR_PASS) message = "password must be 8-63 bytes or empty";
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, message);
    }

    const esp_err_t err = net_save_credentials(ssid, pass);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "凭证保存失败: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "cannot store credentials");
    }

    ESP_LOGI(TAG, "凭证已保存，准备重启");
    s_provisioned = true;
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, SAVED_PAGE, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t start_httpd(void) {
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.lru_purge_enable = true;
    cfg.server_port = 80;
    esp_err_t err = httpd_start(&s_httpd, &cfg);
    if (err != ESP_OK) return err;

    const httpd_uri_t root = {
        .uri = "/", .method = HTTP_GET, .handler = handle_root, .user_ctx = NULL};
    const httpd_uri_t save = {
        .uri = "/save", .method = HTTP_POST, .handler = handle_save, .user_ctx = NULL};

    err = httpd_register_uri_handler(s_httpd, &root);
    if (err == ESP_OK) err = httpd_register_uri_handler(s_httpd, &save);
    if (err != ESP_OK) {
        (void)httpd_stop(s_httpd);
        s_httpd = NULL;
    }
    return err;
}

esp_err_t net_start_provisioning(uint32_t timeout_ms, net_cancel_cb_t cancel) {
    esp_err_t err = net_boot();
    if (err != ESP_OK) return err;

    if (s_ap_netif == NULL) {
        s_ap_netif = esp_netif_create_default_wifi_ap();
        if (s_ap_netif == NULL) return ESP_ERR_NO_MEM;
    }

    err = wifi_prepare(WIFI_MODE_AP);
    if (err != ESP_OK) return err;

    wifi_config_t ap_cfg = {0};
    memcpy(ap_cfg.ap.ssid, AP_SSID, strlen(AP_SSID));
    memcpy(ap_cfg.ap.password, AP_PASSWORD, strlen(AP_PASSWORD));
    ap_cfg.ap.ssid_len = (uint8_t)strlen(AP_SSID);
    ap_cfg.ap.max_connection = 2;
    ap_cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    err = esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
    if (err != ESP_OK) return err;

    err = wifi_launch();
    if (err != ESP_OK) return err;

    err = start_httpd();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "配置页启动失败: %s", esp_err_to_name(err));
        return err;
    }

    s_provisioned = false;
    for (uint32_t waited = 0; waited < timeout_ms; waited += POLL_PERIOD_MS) {
        if (cancel != NULL && cancel()) return ESP_ERR_INVALID_STATE;
        if (s_provisioned) {
            // 让 HTTP 响应先发出去，再重启进 STA 流程。
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_restart();
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_PERIOD_MS));
    }
    ESP_LOGW(TAG, "配网超时 (%" PRIu32 " ms)", timeout_ms);
    return ESP_ERR_TIMEOUT;
}

// ------------------------------------------------------------------ 收尾

void net_stop(void) {
    if (s_httpd != NULL) {
        (void)httpd_stop(s_httpd);
        s_httpd = NULL;
    }
    if (s_wifi_running) {
        (void)esp_wifi_stop();
        s_wifi_running = false;
    }
    if (s_wifi_inited) {
        (void)esp_wifi_deinit();
        s_wifi_inited = false;
    }
    if (s_handlers_registered) {
        (void)esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_wifi_handler);
        (void)esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, s_ip_handler);
        s_handlers_registered = false;
    }
    if (s_sta_netif != NULL) {
        esp_netif_destroy_default_wifi(s_sta_netif);
        s_sta_netif = NULL;
    }
    if (s_ap_netif != NULL) {
        esp_netif_destroy_default_wifi(s_ap_netif);
        s_ap_netif = NULL;
    }
    s_got_ip = false;
}
