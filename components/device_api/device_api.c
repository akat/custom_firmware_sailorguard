#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "device_api.h"
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifndef BOARD_NAME
#define BOARD_NAME "esp32s3box"
#endif

static const char *TAG = "device_api";
static const char *NVS_NAMESPACE = "device";
static const char *NVS_AUTO_UPDATE = "auto_update";
static const char *GITHUB_API_URL =
    "https://api.github.com/repos/akatsaris/custom_firmware_sailorguard/releases/latest";

// ============================================================================
// HTTP client helpers (same pattern as signalk_auth.c)
// ============================================================================

typedef struct {
    char *data;
    size_t size;
} http_buf_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    http_buf_t *buf = (http_buf_t *)evt->user_data;
    if (!buf) {
        return ESP_OK;
    }
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data_len > 0) {
        char *next = realloc(buf->data, buf->size + evt->data_len + 1);
        if (!next) {
            return ESP_ERR_NO_MEM;
        }
        buf->data = next;
        memcpy(buf->data + buf->size, evt->data, evt->data_len);
        buf->size += evt->data_len;
        buf->data[buf->size] = '\0';
    }
    return ESP_OK;
}

// ============================================================================
// NVS helpers
// ============================================================================

static bool nvs_get_auto_update(void) {
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }
    uint8_t val = 0;
    nvs_get_u8(handle, NVS_AUTO_UPDATE, &val);
    nvs_close(handle);
    return val != 0;
}

static esp_err_t nvs_set_auto_update(bool enabled) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u8(handle, NVS_AUTO_UPDATE, enabled ? 1 : 0);
    if (err == ESP_OK) {
        nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

// ============================================================================
// Version comparison
// ============================================================================

static int compare_versions(const char *current, const char *latest) {
    int c_major = 0, c_minor = 0, c_patch = 0;
    int l_major = 0, l_minor = 0, l_patch = 0;

    // Skip leading 'v' if present
    if (current[0] == 'v' || current[0] == 'V') current++;
    if (latest[0] == 'v' || latest[0] == 'V') latest++;

    sscanf(current, "%d.%d.%d", &c_major, &c_minor, &c_patch);
    sscanf(latest, "%d.%d.%d", &l_major, &l_minor, &l_patch);

    if (l_major != c_major) return l_major - c_major;
    if (l_minor != c_minor) return l_minor - c_minor;
    return l_patch - c_patch;
}

// ============================================================================
// GET /api/device/info
// ============================================================================

static esp_err_t device_info_handler(httpd_req_t *req) {
    const esp_app_desc_t *app = esp_app_get_description();
    esp_chip_info_t chip;
    esp_chip_info(&chip);

    uint64_t uptime_ms = esp_timer_get_time() / 1000ULL;
    size_t free_heap = esp_get_free_heap_size();

    char payload[384];
    int len = snprintf(payload, sizeof(payload),
        "{\"firmware_version\":\"%s\","
        "\"idf_version\":\"%s\","
        "\"chip_model\":%d,"
        "\"chip_cores\":%d,"
        "\"chip_revision\":%d,"
        "\"free_heap\":%u,"
        "\"uptime_ms\":%llu,"
        "\"auto_update\":%s}",
        app ? app->version : "unknown",
        app ? app->idf_ver : "unknown",
        (int)chip.model,
        (int)chip.cores,
        (int)chip.revision,
        (unsigned int)free_heap,
        (unsigned long long)uptime_ms,
        nvs_get_auto_update() ? "true" : "false");

    if (len < 0 || len >= (int)sizeof(payload)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "overflow");
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, payload, len);
}

// ============================================================================
// POST /api/device/reboot
// ============================================================================

static void reboot_task(void *arg) {
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}

static esp_err_t device_reboot_handler(httpd_req_t *req) {
    ESP_LOGW(TAG, "Reboot requested via API");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);

    // Reboot after response is sent
    xTaskCreate(reboot_task, "reboot", 2048, NULL, 5, NULL);
    return ESP_OK;
}

// ============================================================================
// GET /api/device/update-check
// ============================================================================

static esp_err_t device_update_check_handler(httpd_req_t *req) {
    const esp_app_desc_t *app = esp_app_get_description();
    const char *current = app ? app->version : "0.0.0";

    http_buf_t buf = {0};
    esp_http_client_config_t cfg = {
        .url = GITHUB_API_URL,
        .event_handler = http_event_handler,
        .user_data = &buf,
        .timeout_ms = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "http init failed");
    }

    // GitHub API requires User-Agent header
    esp_http_client_set_header(client, "User-Agent", "ESP32-SailorGuard");
    esp_http_client_set_header(client, "Accept", "application/vnd.github.v3+json");

    esp_err_t err = esp_http_client_perform(client);
    int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status_code != 200) {
        free(buf.data);
        ESP_LOGE(TAG, "GitHub API request failed: err=%d status=%d", err, status_code);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "github api failed");
    }

    // Parse GitHub response
    cJSON *root = cJSON_Parse(buf.data);
    free(buf.data);
    if (!root) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json parse failed");
    }

    cJSON *tag = cJSON_GetObjectItem(root, "tag_name");
    const char *latest = cJSON_IsString(tag) ? tag->valuestring : "0.0.0";
    bool update_available = compare_versions(current, latest) > 0;

    // Find firmware and spiffs asset download URLs matching our board
    // Expected names: firmware-<board>.bin, spiffs-<board>.bin
    const char *firmware_url = "";
    const char *spiffs_url = "";
    char fw_name[64], sp_name[64];
    snprintf(fw_name, sizeof(fw_name), "firmware-%s.bin", BOARD_NAME);
    snprintf(sp_name, sizeof(sp_name), "spiffs-%s.bin", BOARD_NAME);

    cJSON *assets = cJSON_GetObjectItem(root, "assets");
    if (cJSON_IsArray(assets)) {
        cJSON *asset = NULL;
        cJSON_ArrayForEach(asset, assets) {
            cJSON *name = cJSON_GetObjectItem(asset, "name");
            cJSON *url = cJSON_GetObjectItem(asset, "browser_download_url");
            if (!cJSON_IsString(name) || !cJSON_IsString(url)) continue;

            if (strcmp(name->valuestring, fw_name) == 0) {
                firmware_url = url->valuestring;
            } else if (strcmp(name->valuestring, sp_name) == 0) {
                spiffs_url = url->valuestring;
            }
        }
    }

    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "current", current);
    cJSON_AddStringToObject(response, "latest", latest);
    cJSON_AddBoolToObject(response, "update_available", update_available);
    cJSON_AddStringToObject(response, "firmware_url", firmware_url);
    cJSON_AddStringToObject(response, "spiffs_url", spiffs_url);
    cJSON_AddStringToObject(response, "board", BOARD_NAME);

    char *payload = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);
    cJSON_Delete(root);

    if (!payload) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "serialize failed");
    }

    httpd_resp_set_type(req, "application/json");
    esp_err_t result = httpd_resp_send(req, payload, HTTPD_RESP_USE_STRLEN);
    free(payload);
    return result;
}

// ============================================================================
// POST /api/device/ota
// ============================================================================

typedef struct {
    char firmware_url[512];
    char spiffs_url[512];
} ota_task_params_t;

static volatile bool ota_in_progress = false;

#define SPIFFS_OTA_CHUNK_SIZE 4096

static esp_err_t spiffs_ota_write(const char *url) {
    const esp_partition_t *spiffs_part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, NULL);
    if (!spiffs_part) {
        ESP_LOGE(TAG, "SPIFFS partition not found");
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "SPIFFS OTA: streaming from %s", url);

    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 60000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return ESP_ERR_NO_MEM;

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS HTTP open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);

    if (status_code != 200) {
        ESP_LOGE(TAG, "SPIFFS HTTP status: %d", status_code);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    if (content_length > 0 && (size_t)content_length > spiffs_part->size) {
        ESP_LOGE(TAG, "SPIFFS image too large: %d > %"PRIu32, content_length, spiffs_part->size);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t *chunk = malloc(SPIFFS_OTA_CHUNK_SIZE);
    if (!chunk) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Erasing SPIFFS partition...");
    err = esp_partition_erase_range(spiffs_part, 0, spiffs_part->size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS erase failed: %s", esp_err_to_name(err));
        free(chunk);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return err;
    }

    size_t written = 0;
    int read_len;

    while ((read_len = esp_http_client_read(client, (char *)chunk, SPIFFS_OTA_CHUNK_SIZE)) > 0) {
        if (written + (size_t)read_len > spiffs_part->size) {
            ESP_LOGE(TAG, "SPIFFS image exceeds partition size");
            err = ESP_ERR_INVALID_SIZE;
            break;
        }
        // Flash writes must be 4-byte aligned in size; pad last chunk with 0xFF
        size_t write_len = ((size_t)read_len + 3u) & ~3u;
        if (write_len > (size_t)read_len) {
            memset(chunk + read_len, 0xFF, write_len - (size_t)read_len);
        }
        err = esp_partition_write(spiffs_part, written, chunk, write_len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "SPIFFS write failed at offset %u: %s", (unsigned)written, esp_err_to_name(err));
            break;
        }
        written += (size_t)read_len;
    }

    if (err == ESP_OK && read_len < 0) {
        ESP_LOGE(TAG, "SPIFFS HTTP read error: %d", read_len);
        err = ESP_FAIL;
    }

    free(chunk);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "SPIFFS OTA completed: %u bytes written", (unsigned)written);
    }
    return err;
}

static void ota_task(void *arg) {
    ota_task_params_t *params = (ota_task_params_t *)arg;
    bool spiffs_ok = true;

    // Step 1: Update SPIFFS if URL provided (do this first, before app OTA)
    if (params->spiffs_url[0] != '\0') {
        esp_err_t err = spiffs_ota_write(params->spiffs_url);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "SPIFFS OTA failed: %s", esp_err_to_name(err));
            spiffs_ok = false;
            // Continue with firmware update anyway
        }
    }

    // Step 2: Update firmware if URL provided
    if (params->firmware_url[0] != '\0') {
        ESP_LOGI(TAG, "Starting firmware OTA from: %s", params->firmware_url);

        esp_http_client_config_t cfg = {
            .url = params->firmware_url,
            .timeout_ms = 60000,
            .crt_bundle_attach = esp_crt_bundle_attach,
        };

        esp_https_ota_config_t ota_config = {
            .http_config = &cfg,
        };

        esp_err_t err = esp_https_ota(&ota_config);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Firmware OTA succeeded, rebooting...");
            free(params);
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_restart();
        } else {
            ESP_LOGE(TAG, "Firmware OTA failed: %s", esp_err_to_name(err));
        }
    } else if (params->spiffs_url[0] != '\0' && spiffs_ok) {
        // SPIFFS-only update succeeded, reboot to remount
        ESP_LOGI(TAG, "SPIFFS updated successfully, rebooting...");
        free(params);
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    } else if (!spiffs_ok) {
        ESP_LOGE(TAG, "OTA aborted: SPIFFS update failed, not rebooting");
    }

    free(params);
    ota_in_progress = false;
    vTaskDelete(NULL);
}

static esp_err_t device_ota_handler(httpd_req_t *req) {
    if (ota_in_progress) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "OTA already in progress");
    }

    size_t total_len = req->content_len;
    if (total_len == 0 || total_len > 2048) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid body");
    }

    char *buffer = malloc(total_len + 1);
    if (!buffer) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "out of memory");
    }

    size_t received = 0;
    while (received < total_len) {
        int len = httpd_req_recv(req, buffer + received, total_len - received);
        if (len <= 0) {
            free(buffer);
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "recv failed");
        }
        received += (size_t)len;
    }
    buffer[received] = '\0';

    cJSON *body = cJSON_Parse(buffer);
    free(buffer);
    if (!body) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");
    }

    cJSON *fw_url = cJSON_GetObjectItem(body, "firmware_url");
    cJSON *sp_url = cJSON_GetObjectItem(body, "spiffs_url");

    bool has_fw = cJSON_IsString(fw_url) && strlen(fw_url->valuestring) > 0;
    bool has_sp = cJSON_IsString(sp_url) && strlen(sp_url->valuestring) > 0;

    if (!has_fw && !has_sp) {
        cJSON_Delete(body);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing firmware_url or spiffs_url");
    }

    ota_task_params_t *params = malloc(sizeof(ota_task_params_t));
    if (!params) {
        cJSON_Delete(body);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "out of memory");
    }

    memset(params, 0, sizeof(*params));
    if (has_fw) snprintf(params->firmware_url, sizeof(params->firmware_url), "%s", fw_url->valuestring);
    if (has_sp) snprintf(params->spiffs_url, sizeof(params->spiffs_url), "%s", sp_url->valuestring);
    cJSON_Delete(body);

    ota_in_progress = true;
    if (xTaskCreate(ota_task, "ota", 8192, params, 5, NULL) != pdPASS) {
        free(params);
        ota_in_progress = false;
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "task create failed");
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, "{\"ok\":true,\"message\":\"OTA started\"}", HTTPD_RESP_USE_STRLEN);
}

// ============================================================================
// GET/POST /api/device/auto-update
// ============================================================================

static esp_err_t device_auto_update_get_handler(httpd_req_t *req) {
    bool enabled = nvs_get_auto_update();
    char payload[32];
    snprintf(payload, sizeof(payload), "{\"enabled\":%s}", enabled ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, payload, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t device_auto_update_post_handler(httpd_req_t *req) {
    size_t total_len = req->content_len;
    if (total_len == 0 || total_len > 256) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid body");
    }

    char buffer[257] = {0};
    size_t received = 0;
    while (received < total_len) {
        int len = httpd_req_recv(req, buffer + received, total_len - received);
        if (len <= 0) {
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "recv failed");
        }
        received += (size_t)len;
    }
    buffer[received] = '\0';

    cJSON *body = cJSON_Parse(buffer);
    if (!body) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");
    }

    cJSON *enabled = cJSON_GetObjectItem(body, "enabled");
    if (!cJSON_IsBool(enabled)) {
        cJSON_Delete(body);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing enabled");
    }

    nvs_set_auto_update(cJSON_IsTrue(enabled));
    cJSON_Delete(body);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
}

// ============================================================================
// Registration
// ============================================================================

void device_api_register(httpd_handle_t server) {
    httpd_uri_t info_uri = {
        .uri = "/api/device/info",
        .method = HTTP_GET,
        .handler = device_info_handler,
    };
    httpd_register_uri_handler(server, &info_uri);

    httpd_uri_t reboot_uri = {
        .uri = "/api/device/reboot",
        .method = HTTP_POST,
        .handler = device_reboot_handler,
    };
    httpd_register_uri_handler(server, &reboot_uri);

    httpd_uri_t update_check_uri = {
        .uri = "/api/device/update-check",
        .method = HTTP_GET,
        .handler = device_update_check_handler,
    };
    httpd_register_uri_handler(server, &update_check_uri);

    httpd_uri_t ota_uri = {
        .uri = "/api/device/ota",
        .method = HTTP_POST,
        .handler = device_ota_handler,
    };
    httpd_register_uri_handler(server, &ota_uri);

    httpd_uri_t auto_update_get_uri = {
        .uri = "/api/device/auto-update",
        .method = HTTP_GET,
        .handler = device_auto_update_get_handler,
    };
    httpd_register_uri_handler(server, &auto_update_get_uri);

    httpd_uri_t auto_update_post_uri = {
        .uri = "/api/device/auto-update",
        .method = HTTP_POST,
        .handler = device_auto_update_post_handler,
    };
    httpd_register_uri_handler(server, &auto_update_post_uri);

    ESP_LOGI(TAG, "Device API registered");
}
