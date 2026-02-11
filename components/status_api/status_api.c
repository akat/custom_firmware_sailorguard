#include <stdio.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_system.h"
#include "esp_timer.h"

static const char *TAG = "status_api";

typedef struct {
    esp_netif_t *ap_netif;
} status_ctx_t;

static const char *reset_reason_to_str(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_POWERON:
            return "power_on";
        case ESP_RST_EXT:
            return "external";
        case ESP_RST_SW:
            return "software";
        case ESP_RST_PANIC:
            return "panic";
        case ESP_RST_INT_WDT:
            return "int_wdt";
        case ESP_RST_TASK_WDT:
            return "task_wdt";
        case ESP_RST_WDT:
            return "other_wdt";
        case ESP_RST_DEEPSLEEP:
            return "deep_sleep";
        case ESP_RST_BROWNOUT:
            return "brownout";
        case ESP_RST_SDIO:
            return "sdio";
        default:
            return "unknown";
    }
}

static esp_err_t status_handler(httpd_req_t *req) {
    status_ctx_t *ctx = (status_ctx_t *)req->user_ctx;
    char ip_str[16] = "0.0.0.0";
    if (ctx && ctx->ap_netif) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(ctx->ap_netif, &ip_info) == ESP_OK) {
            snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
        }
    }

    const esp_app_desc_t *app_desc = esp_app_get_description();
    const char *fw_version = app_desc ? app_desc->version : "unknown";
    uint64_t uptime_us = esp_timer_get_time();
    uint64_t uptime_ms = uptime_us / 1000ULL;
    size_t free_heap = esp_get_free_heap_size();
    const char *reset_reason = reset_reason_to_str(esp_reset_reason());

    char payload[256];
    int written = snprintf(
        payload,
        sizeof(payload),
        "{\"firmwareVersion\":\"%s\",\"uptimeMs\":%llu,\"lastReset\":\"%s\",\"freeMemory\":%u,\"ipAddress\":\"%s\"}",
        fw_version,
        (unsigned long long)uptime_ms,
        reset_reason,
        (unsigned int)free_heap,
        ip_str);

    if (written < 0 || written >= (int)sizeof(payload)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "status overflow");
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, payload, written);
}

void status_api_register(httpd_handle_t server, esp_netif_t *ap_netif) {
    static status_ctx_t ctx = {0};
    ctx.ap_netif = ap_netif;

    httpd_uri_t status_uri = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = status_handler,
        .user_ctx = &ctx,
    };

    if (httpd_register_uri_handler(server, &status_uri) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register status handler");
    }
}
