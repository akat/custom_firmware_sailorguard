#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "cJSON.h"

#include "wifi_manager.h"

static const char *auth_to_str(wifi_auth_mode_t auth) {
    switch (auth) {
        case WIFI_AUTH_OPEN:
            return "open";
        case WIFI_AUTH_WEP:
            return "wep";
        case WIFI_AUTH_WPA_PSK:
            return "wpa";
        case WIFI_AUTH_WPA2_PSK:
            return "wpa2";
        case WIFI_AUTH_WPA_WPA2_PSK:
            return "wpa_wpa2";
        case WIFI_AUTH_WPA2_ENTERPRISE:
            return "wpa2_ent";
        case WIFI_AUTH_WPA3_PSK:
            return "wpa3";
        case WIFI_AUTH_WPA2_WPA3_PSK:
            return "wpa2_wpa3";
        default:
            return "unknown";
    }
}

static esp_err_t wifi_status_handler(httpd_req_t *req) {
    wifi_status_t status = {0};
    wifi_manager_get_status(&status);

    char payload[256];
    int written = snprintf(
        payload,
        sizeof(payload),
        "{\"staConnected\":%s,\"staSsid\":\"%s\",\"staIp\":\"%s\",\"apStarted\":%s,\"apSsid\":\"%s\"}",
        status.sta_connected ? "true" : "false",
        status.sta_ssid,
        status.sta_ip,
        status.ap_started ? "true" : "false",
        status.ap_ssid);

    if (written < 0 || written >= (int)sizeof(payload)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "status overflow");
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, payload, written);
}

static esp_err_t wifi_config_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) {
        char ssid[33] = {0};
        wifi_manager_get_saved_ssid(ssid, sizeof(ssid));

        char payload[128];
        int written = snprintf(payload, sizeof(payload), "{\"ssid\":\"%s\"}", ssid);
        if (written < 0 || written >= (int)sizeof(payload)) {
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config overflow");
        }

        httpd_resp_set_type(req, "application/json");
        return httpd_resp_send(req, payload, written);
    }

    char buffer[256];
    int received = httpd_req_recv(req, buffer, sizeof(buffer) - 1);
    if (received <= 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid body");
    }
    buffer[received] = '\0';

    cJSON *json = cJSON_Parse(buffer);
    if (!json) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid JSON");
    }

    const cJSON *ssid_item = cJSON_GetObjectItemCaseSensitive(json, "ssid");
    if (!cJSON_IsString(ssid_item) || ssid_item->valuestring[0] == '\0') {
        cJSON_Delete(json);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing ssid");
    }

    char ssid[33] = {0};
    char password[65] = {0};
    strncpy(ssid, ssid_item->valuestring, sizeof(ssid) - 1);

    const cJSON *pass_item = cJSON_GetObjectItemCaseSensitive(json, "password");
    if (cJSON_IsString(pass_item) && pass_item->valuestring) {
        strncpy(password, pass_item->valuestring, sizeof(password) - 1);
    }
    cJSON_Delete(json);

    esp_err_t err = wifi_manager_connect(ssid, password, true);
    if (err != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "connect failed");
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
}

static esp_err_t wifi_scan_handler(httpd_req_t *req) {
    wifi_ap_record_t records[20];
    uint16_t count = (uint16_t)(sizeof(records) / sizeof(records[0]));

    esp_err_t err = wifi_manager_scan(records, &count);
    if (err != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "scan failed");
    }

    /* Deduplicate by SSID, keeping the entry with the strongest RSSI */
    uint16_t unique = 0;
    for (uint16_t i = 0; i < count; ++i) {
        const char *ssid = (const char *)records[i].ssid;
        bool duplicate = false;
        for (uint16_t j = 0; j < unique; ++j) {
            if (strcmp(ssid, (const char *)records[j].ssid) == 0) {
                if (records[i].rssi > records[j].rssi) {
                    records[j] = records[i];
                }
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            if (unique != i) {
                records[unique] = records[i];
            }
            unique++;
        }
    }
    count = unique;

    char *payload = malloc(2048);
    if (!payload) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "out of memory");
    }

    size_t offset = 0;
    offset += snprintf(payload + offset, 2048 - offset, "{\"networks\":[");

    for (uint16_t i = 0; i < count; ++i) {
        const char *ssid = (const char *)records[i].ssid;
        const char *auth = auth_to_str(records[i].authmode);
        int rssi = records[i].rssi;
        offset += snprintf(
            payload + offset,
            2048 - offset,
            "%s{\"ssid\":\"%s\",\"rssi\":%d,\"auth\":\"%s\"}",
            i == 0 ? "" : ",",
            ssid,
            rssi,
            auth);
        if (offset >= 2048) {
            free(payload);
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "scan overflow");
        }
    }

    offset += snprintf(payload + offset, 2048 - offset, "]}");
    if (offset >= 2048) {
        free(payload);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "scan overflow");
    }

    httpd_resp_set_type(req, "application/json");
    esp_err_t result = httpd_resp_send(req, payload, offset);
    free(payload);
    return result;
}

static esp_err_t wifi_ap_config_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) {
        char ssid[33] = {0};
        char password[65] = {0};
        wifi_manager_get_ap_config(ssid, sizeof(ssid), password, sizeof(password));

        char payload[128];
        int written = snprintf(payload, sizeof(payload), "{\"ssid\":\"%s\",\"password\":\"%s\"}", ssid, password);
        if (written < 0 || written >= (int)sizeof(payload)) {
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ap config overflow");
        }

        httpd_resp_set_type(req, "application/json");
        return httpd_resp_send(req, payload, written);
    }

    char buffer[256];
    int received = httpd_req_recv(req, buffer, sizeof(buffer) - 1);
    if (received <= 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid body");
    }
    buffer[received] = '\0';

    cJSON *json = cJSON_Parse(buffer);
    if (!json) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid JSON");
    }

    const cJSON *ssid_item = cJSON_GetObjectItemCaseSensitive(json, "ssid");
    if (!cJSON_IsString(ssid_item) || ssid_item->valuestring[0] == '\0') {
        cJSON_Delete(json);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing ssid");
    }

    const cJSON *pass_item = cJSON_GetObjectItemCaseSensitive(json, "password");
    if (!cJSON_IsString(pass_item) || !pass_item->valuestring) {
        cJSON_Delete(json);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing password");
    }

    char ssid[33] = {0};
    char password[65] = {0};
    strncpy(ssid, ssid_item->valuestring, sizeof(ssid) - 1);
    strncpy(password, pass_item->valuestring, sizeof(password) - 1);
    cJSON_Delete(json);

    esp_err_t err = wifi_manager_set_ap_config(ssid, password);
    if (err != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config failed");
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
}

void wifi_api_register(httpd_handle_t server) {
    httpd_uri_t status_uri = {
        .uri = "/api/wifi/status",
        .method = HTTP_GET,
        .handler = wifi_status_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server, &status_uri);

    httpd_uri_t config_uri = {
        .uri = "/api/wifi/config",
        .method = HTTP_GET,
        .handler = wifi_config_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server, &config_uri);

    httpd_uri_t config_post_uri = {
        .uri = "/api/wifi/config",
        .method = HTTP_POST,
        .handler = wifi_config_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server, &config_post_uri);

    httpd_uri_t scan_uri = {
        .uri = "/api/wifi/scan",
        .method = HTTP_GET,
        .handler = wifi_scan_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server, &scan_uri);

    httpd_uri_t ap_config_uri = {
        .uri = "/api/wifi/ap_config",
        .method = HTTP_GET,
        .handler = wifi_ap_config_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server, &ap_config_uri);

    httpd_uri_t ap_config_post_uri = {
        .uri = "/api/wifi/ap_config",
        .method = HTTP_POST,
        .handler = wifi_ap_config_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server, &ap_config_post_uri);
}
