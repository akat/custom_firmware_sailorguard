#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_netif.h"
#include "esp_wifi.h"

typedef struct {
    bool sta_connected;
    char sta_ssid[33];
    char sta_ip[16];
    bool ap_started;
    char ap_ssid[33];
} wifi_status_t;

void wifi_manager_init(const char *ap_ssid, const char *ap_password, uint32_t ap_fallback_ms);
void wifi_manager_start(void);

esp_err_t wifi_manager_connect(const char *ssid, const char *password, bool persist);

esp_err_t wifi_manager_scan(wifi_ap_record_t *records, uint16_t *count);
void wifi_manager_get_status(wifi_status_t *status);
void wifi_manager_get_saved_ssid(char *ssid, size_t ssid_len);

esp_err_t wifi_manager_get_ap_config(char *ssid, size_t ssid_len, char *password, size_t password_len);
esp_err_t wifi_manager_set_ap_config(const char *ssid, const char *password);

esp_netif_t *wifi_manager_get_ap_netif(void);
