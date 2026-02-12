#include "signalk_subscriber.h"
#include "signalk_client.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>

#if __has_include("esp_websocket_client.h")
#include "esp_websocket_client.h"
#define SIGNALK_WS_AVAILABLE 1
#else
#define SIGNALK_WS_AVAILABLE 0
#endif

static const char *TAG = "signalk_sub";

// Forward declarations
static void signalk_polling_task(void *pvParameters);

// Maximum number of subscriptions
#define MAX_SUBSCRIPTIONS 16

// Subscription entry
typedef struct {
    char path[128];
    signalk_subscribe_callback_t callback;
    void *user_data;
    uint32_t period_ms;
    int64_t last_poll_ms;
    signalk_data_t last_value;
    bool active;
} subscription_entry_t;

// Global subscription table
static subscription_entry_t g_subscriptions[MAX_SUBSCRIPTIONS] = {0};
static SemaphoreHandle_t g_sub_mutex = NULL;
static TaskHandle_t g_poll_task = NULL;

esp_err_t signalk_get_json(const char *path, char *json_out, size_t max_len) {
    if (!path || !json_out || max_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    signalk_config_t config;
    esp_err_t err = signalk_get_config(&config);
    if (err != ESP_OK || !config.enabled || config.token[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }

    // Convert path: "environment.outside.temperature" -> "environment/outside/temperature"
    char path_buf[200];
    strncpy(path_buf, path, sizeof(path_buf) - 1);
    path_buf[sizeof(path_buf) - 1] = '\0';
    for (char *p = path_buf; *p; p++) {
        if (*p == '.') *p = '/';
    }

    // Build URL
    const char *scheme = config.use_ssl ? "https" : "http";
    char url[512];
    snprintf(url, sizeof(url), "%s://%s:%d/signalk/v1/api/vessels/self/%s",
             scheme, config.hostname, config.port, path_buf);

    esp_http_client_config_t http_cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (!client) {
        return ESP_ERR_NO_MEM;
    }

    // Set Authorization header
    char auth_header[600];
    int header_len = snprintf(auth_header, sizeof(auth_header) - 1, "Bearer %s", config.token);
    if (header_len > 0 && header_len < (int)sizeof(auth_header)) {
        auth_header[header_len] = '\0';
        esp_http_client_set_header(client, "Authorization", auth_header);
    }

    err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return err;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);

    if (status != 200) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    if (content_length > 0 && content_length < (int)max_len) {
        int read_len = esp_http_client_read(client, json_out, content_length);
        if (read_len > 0) {
            json_out[read_len] = '\0';
        }
    } else {
        json_out[0] = '\0';
        err = ESP_ERR_INVALID_SIZE;
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    return err;
}

esp_err_t signalk_read_value(const char *path, signalk_data_t *data) {
    if (!path || !data) {
        return ESP_ERR_INVALID_ARG;
    }

    char json_buf[1024];
    esp_err_t err = signalk_get_json(path, json_buf, sizeof(json_buf));
    if (err != ESP_OK) {
        return err;
    }

    // Parse JSON response
    cJSON *root = cJSON_Parse(json_buf);
    if (!root) {
        return ESP_FAIL;
    }

    cJSON *value_item = cJSON_GetObjectItem(root, "value");
    if (!value_item) {
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    // Fill in data structure
    strncpy(data->path, path, sizeof(data->path) - 1);
    data->path[sizeof(data->path) - 1] = '\0';
    data->source_label[0] = '\0';

    // Determine type and extract value
    if (cJSON_IsBool(value_item)) {
        data->type = SIGNALK_VALUE_BOOL;
        data->value.b = cJSON_IsTrue(value_item);
    } else if (cJSON_IsNumber(value_item)) {
        double val = value_item->valuedouble;
        if (val == (int)val) {
            data->type = SIGNALK_VALUE_INT;
            data->value.i = (int32_t)val;
        } else {
            data->type = SIGNALK_VALUE_FLOAT;
            data->value.f = (float)val;
        }
    } else if (cJSON_IsString(value_item)) {
        data->type = SIGNALK_VALUE_STRING;
        strncpy(data->value.s, value_item->valuestring, sizeof(data->value.s) - 1);
        data->value.s[sizeof(data->value.s) - 1] = '\0';
    } else if (cJSON_IsObject(value_item)) {
        // Check if it's a position
        cJSON *lat = cJSON_GetObjectItem(value_item, "latitude");
        cJSON *lon = cJSON_GetObjectItem(value_item, "longitude");
        if (lat && lon && cJSON_IsNumber(lat) && cJSON_IsNumber(lon)) {
            data->type = SIGNALK_VALUE_POSITION;
            data->value.pos.latitude = lat->valuedouble;
            data->value.pos.longitude = lon->valuedouble;
            cJSON *alt = cJSON_GetObjectItem(value_item, "altitude");
            data->value.pos.altitude = (alt && cJSON_IsNumber(alt)) ? alt->valuedouble : 0;
        } else {
            cJSON_Delete(root);
            return ESP_ERR_NOT_SUPPORTED;
        }
    } else {
        cJSON_Delete(root);
        return ESP_ERR_NOT_SUPPORTED;
    }

    cJSON_Delete(root);
    return ESP_OK;
}

// Initialize subscription system
esp_err_t signalk_subscriber_init(void) {
    if (!g_sub_mutex) {
        g_sub_mutex = xSemaphoreCreateMutex();
        if (!g_sub_mutex) {
            return ESP_ERR_NO_MEM;
        }
    }
    
    // Note: polling task NOT started here - wait for WiFi connection
    // Use signalk_start_polling_task() after WiFi connects
    
    return ESP_OK;
}

// Start polling task (call after WiFi connected)
esp_err_t signalk_start_polling_task(void) {
#if SIGNALK_WS_AVAILABLE == 0
    if (!g_poll_task) {
        BaseType_t result = xTaskCreate(
            signalk_polling_task,
            "signalk_poll",
            16384,  // 16KB for HTTP operations + JSON parsing
            NULL,
            3,
            &g_poll_task
        );
        if (result != pdPASS) {
            ESP_LOGW(TAG, "Failed to create polling task");
            return ESP_ERR_NO_MEM;
        }
        ESP_LOGI(TAG, "Started polling task for subscriptions");
        return ESP_OK;
    }
    return ESP_OK;  // Already running
#else
    ESP_LOGI(TAG, "WebSocket available - polling task not needed");
    return ESP_OK;
#endif
}

// Background polling task (HTTP polling fallback when WebSocket unavailable)
static void signalk_polling_task(void *pvParameters) {
    (void)pvParameters;
    
    while (1) {
        int64_t now_ms = (int64_t)xTaskGetTickCount() * (int64_t)portTICK_PERIOD_MS;
        
        if (xSemaphoreTake(g_sub_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        
        // Poll each active subscription
        for (int i = 0; i < MAX_SUBSCRIPTIONS; i++) {
            if (!g_subscriptions[i].active) {
                continue;
            }
            
            // Check if it's time to poll this subscription
            if (now_ms < g_subscriptions[i].last_poll_ms + (int64_t)g_subscriptions[i].period_ms) {
                continue;
            }
            
            // Poll the value
            signalk_data_t new_value = {0};
            if (signalk_read_value(g_subscriptions[i].path, &new_value) == ESP_OK) {
                g_subscriptions[i].last_poll_ms = now_ms;
                
                // Check if value changed (simple comparison)
                bool changed = false;
                if (new_value.type != g_subscriptions[i].last_value.type) {
                    changed = true;
                } else if (new_value.type == SIGNALK_VALUE_FLOAT && 
                           new_value.value.f != g_subscriptions[i].last_value.value.f) {
                    changed = true;
                } else if (new_value.type == SIGNALK_VALUE_INT && 
                           new_value.value.i != g_subscriptions[i].last_value.value.i) {
                    changed = true;
                } else if (new_value.type == SIGNALK_VALUE_BOOL && 
                           new_value.value.b != g_subscriptions[i].last_value.value.b) {
                    changed = true;
                }
                
                // Call callback if changed
                if (changed && g_subscriptions[i].callback) {
                    g_subscriptions[i].last_value = new_value;
                    
                    // Release mutex during callback to avoid deadlock
                    xSemaphoreGive(g_sub_mutex);
                    g_subscriptions[i].callback(g_subscriptions[i].path, &new_value, 
                                               g_subscriptions[i].user_data);
                    if (xSemaphoreTake(g_sub_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
                        vTaskDelay(pdMS_TO_TICKS(1000));
                        continue;
                    }
                }
            }
        }
        
        xSemaphoreGive(g_sub_mutex);
        vTaskDelay(pdMS_TO_TICKS(100));  // Poll check every 100ms
    }
}

// Subscribe to a Signal K path
esp_err_t signalk_subscribe(const char *path, 
                           signalk_subscribe_callback_t callback,
                           uint32_t poll_interval_ms,
                           void *user_data) {
    if (!path || !callback) {
        return ESP_ERR_INVALID_ARG;
    }

    // Initialize mutex if needed
    signalk_subscriber_init();

    if (xSemaphoreTake(g_sub_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    // Find empty slot
    int slot = -1;
    for (int i = 0; i < MAX_SUBSCRIPTIONS; i++) {
        if (!g_subscriptions[i].active) {
            slot = i;
            break;
        }
    }

    if (slot == -1) {
        xSemaphoreGive(g_sub_mutex);
        ESP_LOGW(TAG, "No free subscription slots (max %d)", MAX_SUBSCRIPTIONS);
        return ESP_ERR_NO_MEM;
    }

    // Add subscription
    strncpy(g_subscriptions[slot].path, path, sizeof(g_subscriptions[slot].path) - 1);
    g_subscriptions[slot].path[sizeof(g_subscriptions[slot].path) - 1] = '\0';
    g_subscriptions[slot].callback = callback;
    g_subscriptions[slot].user_data = user_data;
    g_subscriptions[slot].period_ms = poll_interval_ms;
    g_subscriptions[slot].active = true;

    xSemaphoreGive(g_sub_mutex);

    ESP_LOGI(TAG, "Subscribed to '%s' (period: %lu ms)", path, (unsigned long)poll_interval_ms);

#if SIGNALK_WS_AVAILABLE
    // Send WebSocket subscription message
    extern esp_err_t signalk_ws_subscribe(const char *path, uint32_t period_ms);
    return signalk_ws_subscribe(path, poll_interval_ms);
#else
    ESP_LOGW(TAG, "WebSocket not available - subscriptions won't receive automatic updates");
    return ESP_OK;
#endif
}

// Unsubscribe from a Signal K path
esp_err_t signalk_unsubscribe(const char *path) {
    if (!path || !g_sub_mutex) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(g_sub_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    // Find and remove subscription
    bool found = false;
    for (int i = 0; i < MAX_SUBSCRIPTIONS; i++) {
        if (g_subscriptions[i].active && 
            strcmp(g_subscriptions[i].path, path) == 0) {
            g_subscriptions[i].active = false;
            found = true;
            break;
        }
    }

    xSemaphoreGive(g_sub_mutex);

    if (!found) {
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "Unsubscribed from '%s'", path);

#if SIGNALK_WS_AVAILABLE
    // Send WebSocket unsubscribe message
    extern esp_err_t signalk_ws_unsubscribe(const char *path);
    return signalk_ws_unsubscribe(path);
#else
    return ESP_OK;
#endif
}

// Handle incoming delta message
void signalk_subscriber_handle_delta(cJSON *delta) {
    if (!delta || !g_sub_mutex) {
        return;
    }

    // Parse delta: {"updates":[{"values":[{"path":"...","value":...}]}]}
    cJSON *updates = cJSON_GetObjectItem(delta, "updates");
    if (!updates || !cJSON_IsArray(updates)) {
        return;
    }

    // Iterate through updates
    cJSON *update = NULL;
    cJSON_ArrayForEach(update, updates) {
        cJSON *values = cJSON_GetObjectItem(update, "values");
        if (!values || !cJSON_IsArray(values)) {
            continue;
        }

        // Iterate through values
        cJSON *value_obj = NULL;
        cJSON_ArrayForEach(value_obj, values) {
            cJSON *path_item = cJSON_GetObjectItem(value_obj, "path");
            cJSON *value_item = cJSON_GetObjectItem(value_obj, "value");

            if (!path_item || !cJSON_IsString(path_item) || !value_item) {
                continue;
            }

            const char *path = path_item->valuestring;

            // Find matching subscription
            if (xSemaphoreTake(g_sub_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
                continue;
            }

            for (int i = 0; i < MAX_SUBSCRIPTIONS; i++) {
                if (!g_subscriptions[i].active) {
                    continue;
                }

                if (strcmp(g_subscriptions[i].path, path) == 0) {
                    // Parse value and call callback
                    signalk_data_t data = {0};
                    strncpy(data.path, path, sizeof(data.path) - 1);

                    if (cJSON_IsBool(value_item)) {
                        data.type = SIGNALK_VALUE_BOOL;
                        data.value.b = cJSON_IsTrue(value_item);
                    } else if (cJSON_IsNumber(value_item)) {
                        double val = value_item->valuedouble;
                        if (val == (int)val) {
                            data.type = SIGNALK_VALUE_INT;
                            data.value.i = (int32_t)val;
                        } else {
                            data.type = SIGNALK_VALUE_FLOAT;
                            data.value.f = (float)val;
                        }
                    } else if (cJSON_IsString(value_item)) {
                        data.type = SIGNALK_VALUE_STRING;
                        strncpy(data.value.s, value_item->valuestring, 
                               sizeof(data.value.s) - 1);
                    } else if (cJSON_IsObject(value_item)) {
                        cJSON *lat = cJSON_GetObjectItem(value_item, "latitude");
                        cJSON *lon = cJSON_GetObjectItem(value_item, "longitude");
                        if (lat && lon && cJSON_IsNumber(lat) && cJSON_IsNumber(lon)) {
                            data.type = SIGNALK_VALUE_POSITION;
                            data.value.pos.latitude = lat->valuedouble;
                            data.value.pos.longitude = lon->valuedouble;
                            cJSON *alt = cJSON_GetObjectItem(value_item, "altitude");
                            data.value.pos.altitude = (alt && cJSON_IsNumber(alt)) ? 
                                                     alt->valuedouble : 0;
                        }
                    }

                    // Call user callback
                    if (g_subscriptions[i].callback) {
                        g_subscriptions[i].callback(path, &data, 
                                                   g_subscriptions[i].user_data);
                    }
                }
            }

            xSemaphoreGive(g_sub_mutex);
        }
    }
}
