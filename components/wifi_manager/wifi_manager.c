#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif_ip_addr.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "wifi_manager.h"

static const char *TAG = "wifi_manager";
static const char *k_nvs_ns = "wifi";
static const char *k_nvs_ssid = "ssid";
static const char *k_nvs_pass = "pass";
static const char *k_nvs_ap_ssid = "ap_ssid";
static const char *k_nvs_ap_pass = "ap_pass";

static esp_netif_t *s_ap_netif = NULL;
static esp_netif_t *s_sta_netif = NULL;
static esp_timer_handle_t s_ap_fallback_timer = NULL;

static bool s_sta_connected = false;
static bool s_ap_started = false;
static uint32_t s_ap_fallback_ms = 15000;

static char s_ap_ssid[33] = {0};
static char s_ap_password[65] = {0};
static char s_sta_ssid[33] = {0};
static char s_sta_ip[16] = "0.0.0.0";

static bool s_should_persist_credentials = false;
static char s_pending_ssid[33] = {0};
static char s_pending_pass[65] = {0};

static void nvs_save_string(const char *key, const char *value) {
    nvs_handle_t handle;
    if (nvs_open(k_nvs_ns, NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }
    nvs_set_str(handle, key, value);
    nvs_commit(handle);
    nvs_close(handle);
}

static void nvs_load_string(const char *key, char *value, size_t value_len) {
    nvs_handle_t handle;
    if (nvs_open(k_nvs_ns, NVS_READONLY, &handle) != ESP_OK) {
        value[0] = '\0';
        return;
    }
    size_t len = value_len;
    if (nvs_get_str(handle, key, value, &len) != ESP_OK) {
        value[0] = '\0';
    }
    nvs_close(handle);
}

static void start_ap(void) {
    if (s_ap_started) {
        return;
    }

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.ap.ssid, s_ap_ssid, sizeof(wifi_config.ap.ssid) - 1);
    wifi_config.ap.ssid[sizeof(wifi_config.ap.ssid) - 1] = '\0';
    strncpy((char *)wifi_config.ap.password, s_ap_password, sizeof(wifi_config.ap.password) - 1);
    wifi_config.ap.password[sizeof(wifi_config.ap.password) - 1] = '\0';
    wifi_config.ap.ssid_len = strlen((char *)wifi_config.ap.ssid);
    wifi_config.ap.channel = 6;
    wifi_config.ap.max_connection = 4;
    wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));

    s_ap_started = true;
    ESP_LOGI(TAG, "SoftAP started: SSID=%s", wifi_config.ap.ssid);
}

static void ap_fallback_cb(void *arg) {
    (void)arg;
    if (!s_sta_connected) {
        start_ap();
    }
}

static void schedule_ap_fallback(void) {
    if (!s_ap_fallback_timer) {
        const esp_timer_create_args_t args = {
            .callback = ap_fallback_cb,
            .name = "ap_fallback",
        };
        if (esp_timer_create(&args, &s_ap_fallback_timer) != ESP_OK) {
            return;
        }
    }

    esp_timer_stop(s_ap_fallback_timer);
    esp_timer_start_once(s_ap_fallback_timer, (uint64_t)s_ap_fallback_ms * 1000ULL);
}

static void cancel_ap_fallback(void) {
    if (s_ap_fallback_timer) {
        esp_timer_stop(s_ap_fallback_timer);
    }
}

static void update_sta_ip(void) {
    if (!s_sta_netif) {
        snprintf(s_sta_ip, sizeof(s_sta_ip), "0.0.0.0");
        return;
    }

    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(s_sta_netif, &ip_info) == ESP_OK) {
        snprintf(s_sta_ip, sizeof(s_sta_ip), IPSTR, IP2STR(&ip_info.ip));
        return;
    }

    snprintf(s_sta_ip, sizeof(s_sta_ip), "0.0.0.0");
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    (void)arg;
    (void)event_data;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_sta_connected = false;
        s_sta_ssid[0] = '\0';
        snprintf(s_sta_ip, sizeof(s_sta_ip), "0.0.0.0");
        // Clear pending credentials if disconnected before successful connection
        if (s_should_persist_credentials) {
            s_pending_ssid[0] = '\0';
            s_pending_pass[0] = '\0';
        }
        esp_wifi_connect();
        schedule_ap_fallback();
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        wifi_config_t config;
        if (esp_wifi_get_config(WIFI_IF_STA, &config) == ESP_OK) {
            snprintf(s_sta_ssid, sizeof(s_sta_ssid), "%s", (char *)config.sta.ssid);
        }
        s_sta_connected = true;
        update_sta_ip();
        cancel_ap_fallback();

        // Save credentials only after successful connection
        if (s_should_persist_credentials) {
            nvs_save_string(k_nvs_ssid, s_pending_ssid);
            nvs_save_string(k_nvs_pass, s_pending_pass);
            s_should_persist_credentials = false;
            ESP_LOGI(TAG, "Credentials saved for SSID: %s", s_pending_ssid);
        }
    }
}

void wifi_manager_init(const char *ap_ssid, const char *ap_password, uint32_t ap_fallback_ms) {
    // Set default AP config
    snprintf(s_ap_ssid, sizeof(s_ap_ssid), "%s", ap_ssid);
    snprintf(s_ap_password, sizeof(s_ap_password), "%s", ap_password);

    // Try to load saved AP config from NVS
    char saved_ap_ssid[33] = {0};
    char saved_ap_pass[65] = {0};
    nvs_load_string(k_nvs_ap_ssid, saved_ap_ssid, sizeof(saved_ap_ssid));
    nvs_load_string(k_nvs_ap_pass, saved_ap_pass, sizeof(saved_ap_pass));

    if (saved_ap_ssid[0] != '\0') {
        snprintf(s_ap_ssid, sizeof(s_ap_ssid), "%s", saved_ap_ssid);
        if (saved_ap_pass[0] != '\0') {
            snprintf(s_ap_password, sizeof(s_ap_password), "%s", saved_ap_pass);
        }
    }

    s_ap_fallback_ms = ap_fallback_ms;

    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));
}

void wifi_manager_start(void) {
    // Keep SoftAP visible even when STA is connected.
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    start_ap();
    ESP_ERROR_CHECK(esp_wifi_start());

    char ssid[33] = {0};
    char pass[65] = {0};
    nvs_load_string(k_nvs_ssid, ssid, sizeof(ssid));
    nvs_load_string(k_nvs_pass, pass, sizeof(pass));

    if (ssid[0] != '\0') {
        wifi_manager_connect(ssid, pass, false);
        schedule_ap_fallback();
    }
}

esp_err_t wifi_manager_connect(const char *ssid, const char *password, bool persist) {
    if (!ssid || ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_config_t config = {0};
    snprintf((char *)config.sta.ssid, sizeof(config.sta.ssid), "%s", ssid);
    snprintf((char *)config.sta.password, sizeof(config.sta.password), "%s", password ? password : "");
    config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    config.sta.pmf_cfg.capable = true;
    config.sta.pmf_cfg.required = false;

    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &config);
    if (err != ESP_OK) {
        return err;
    }

    if (persist) {
        // Store credentials to be saved only after successful connection
        s_should_persist_credentials = true;
        snprintf(s_pending_ssid, sizeof(s_pending_ssid), "%s", ssid);
        snprintf(s_pending_pass, sizeof(s_pending_pass), "%s", password ? password : "");
    }

    err = esp_wifi_connect();
    if (err == ESP_OK) {
        schedule_ap_fallback();
    }
    return err;
}

esp_err_t wifi_manager_scan(wifi_ap_record_t *records, uint16_t *count) {
    if (!records || !count || *count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t primary = 0;
    wifi_second_chan_t secondary = WIFI_SECOND_CHAN_NONE;
    if (s_ap_started) {
        esp_wifi_get_channel(&primary, &secondary);
    }

    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = s_ap_started ? primary : 0,
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time = {
            .active = {
                .min = 500,
                .max = 5000,
            },
        },
    };

    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        return err;
    }

    uint16_t ap_num = *count;
    err = esp_wifi_scan_get_ap_records(&ap_num, records);
    if (err != ESP_OK) {
        return err;
    }

    *count = ap_num;
    return ESP_OK;
}

void wifi_manager_get_status(wifi_status_t *status) {
    if (!status) {
        return;
    }

    status->sta_connected = s_sta_connected;
    snprintf(status->sta_ssid, sizeof(status->sta_ssid), "%s", s_sta_ssid);
    snprintf(status->sta_ip, sizeof(status->sta_ip), "%s", s_sta_ip);
    status->ap_started = s_ap_started;
    snprintf(status->ap_ssid, sizeof(status->ap_ssid), "%s", s_ap_ssid);
}

void wifi_manager_get_saved_ssid(char *ssid, size_t ssid_len) {
    if (!ssid || ssid_len == 0) {
        return;
    }
    nvs_load_string(k_nvs_ssid, ssid, ssid_len);
}

esp_err_t wifi_manager_get_ap_config(char *ssid, size_t ssid_len, char *password, size_t password_len) {
    if (!ssid || ssid_len == 0 || !password || password_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    snprintf(ssid, ssid_len, "%s", s_ap_ssid);
    snprintf(password, password_len, "%s", s_ap_password);
    return ESP_OK;
}

esp_err_t wifi_manager_set_ap_config(const char *ssid, const char *password) {
    if (!ssid || ssid[0] == '\0' || !password) {
        return ESP_ERR_INVALID_ARG;
    }

    // Save to NVS
    nvs_save_string(k_nvs_ap_ssid, ssid);
    nvs_save_string(k_nvs_ap_pass, password);

    // Update runtime values
    snprintf(s_ap_ssid, sizeof(s_ap_ssid), "%s", ssid);
    snprintf(s_ap_password, sizeof(s_ap_password), "%s", password);

    // If AP is already started, restart it with new config
    if (s_ap_started) {
        s_ap_started = false;
        start_ap();
    }

    ESP_LOGI(TAG, "AP configuration updated: SSID=%s", ssid);
    return ESP_OK;
}

esp_netif_t *wifi_manager_get_ap_netif(void) {
    return s_ap_netif;
}
