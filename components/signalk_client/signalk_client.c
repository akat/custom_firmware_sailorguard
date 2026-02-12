#include "signalk_types.h"
#include "signalk_client.h"
#include "signalk_mdns.h"
#include "signalk_auth.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#if __has_include("esp_websocket_client.h")
#include "esp_websocket_client.h"
#define SIGNALK_WS_AVAILABLE 1
#else
#include "esp_err.h"
typedef void *esp_websocket_client_handle_t;
typedef struct {
    const char *uri;
    const char *headers;
    int buffer_size;
    bool disable_auto_reconnect;
} esp_websocket_client_config_t;
typedef struct {
    int data_len;
    const char *data_ptr;
} esp_websocket_event_data_t;
#define WEBSOCKET_EVENT_ANY 0
#define WEBSOCKET_EVENT_CONNECTED 1
#define WEBSOCKET_EVENT_DISCONNECTED 2
#define WEBSOCKET_EVENT_DATA 3
#define WEBSOCKET_EVENT_ERROR 4
static inline esp_websocket_client_handle_t esp_websocket_client_init(const esp_websocket_client_config_t *cfg) {
    (void)cfg;
    return NULL;
}
static inline void esp_websocket_client_destroy(esp_websocket_client_handle_t client) {
    (void)client;
}
static inline esp_err_t esp_websocket_client_start(esp_websocket_client_handle_t client) {
    (void)client;
    return ESP_ERR_NOT_SUPPORTED;
}
static inline esp_err_t esp_websocket_client_stop(esp_websocket_client_handle_t client) {
    (void)client;
    return ESP_ERR_NOT_SUPPORTED;
}
static inline esp_err_t esp_websocket_client_register_event(esp_websocket_client_handle_t client,
                                                           int32_t event_id,
                                                           void (*event_handler)(void *, esp_event_base_t, int32_t, void *),
                                                           void *event_handler_arg) {
    (void)client;
    (void)event_id;
    (void)event_handler;
    (void)event_handler_arg;
    return ESP_ERR_NOT_SUPPORTED;
}
static inline int esp_websocket_client_send_text(esp_websocket_client_handle_t client,
                                                 const char *data,
                                                 int len,
                                                 TickType_t timeout) {
    (void)client;
    (void)data;
    (void)len;
    (void)timeout;
    return -1;
}
#define SIGNALK_WS_AVAILABLE 0
#endif

static const char *TAG = "signalk_client";

// Global state
static struct {
    signalk_config_t config;
    signalk_status_t status;
    TaskHandle_t client_task;
    QueueHandle_t command_queue;
    esp_websocket_client_handle_t ws_client;
    bool ws_connected;
    int64_t next_reconnect_ms;
    int reconnect_delay_ms;
    char ws_url[256];
    char ws_headers[256];
    bool initialized;
} g_signalk_state = {
    .initialized = false,
    .client_task = NULL,
    .command_queue = NULL,
    .ws_client = NULL,
    .ws_connected = false,
    .next_reconnect_ms = 0,
    .reconnect_delay_ms = 2000,
};

// Forward declarations
extern esp_err_t signalk_storage_load_config(signalk_config_t *config);
extern esp_err_t signalk_storage_save_config(const signalk_config_t *config);

static int64_t signalk_now_ms(void) {
    return (int64_t)xTaskGetTickCount() * (int64_t)portTICK_PERIOD_MS;
}

static void signalk_ws_schedule_reconnect(int64_t now_ms) {
    if (g_signalk_state.reconnect_delay_ms <= 0) {
        g_signalk_state.reconnect_delay_ms = 2000;
    } else if (g_signalk_state.reconnect_delay_ms < 30000) {
        g_signalk_state.reconnect_delay_ms *= 2;
        if (g_signalk_state.reconnect_delay_ms > 30000) {
            g_signalk_state.reconnect_delay_ms = 30000;
        }
    }

    g_signalk_state.next_reconnect_ms = now_ms + g_signalk_state.reconnect_delay_ms;
}

static void signalk_ws_reset_reconnect(void) {
    g_signalk_state.reconnect_delay_ms = 2000;
    g_signalk_state.next_reconnect_ms = 0;
}

static void signalk_ws_stop(void) {
    if (g_signalk_state.ws_client) {
        esp_websocket_client_stop(g_signalk_state.ws_client);
        esp_websocket_client_destroy(g_signalk_state.ws_client);
        g_signalk_state.ws_client = NULL;
    }
    g_signalk_state.ws_connected = false;
}

static void signalk_ws_event_handler(void *handler_args,
                                     esp_event_base_t base,
                                     int32_t event_id,
                                     void *event_data) {
    (void)handler_args;
    (void)base;

    if (event_id == WEBSOCKET_EVENT_CONNECTED) {
        g_signalk_state.ws_connected = true;
        g_signalk_state.status.state = SIGNALK_STATE_CONNECTED;
        g_signalk_state.status.error_message[0] = '\0';

        strncpy(g_signalk_state.status.server.hostname,
                g_signalk_state.config.hostname,
                sizeof(g_signalk_state.status.server.hostname) - 1);
        g_signalk_state.status.server.port = g_signalk_state.config.port;
        g_signalk_state.status.server.ssl_enabled = g_signalk_state.config.use_ssl;
        ESP_LOGI(TAG, "WebSocket connected");
        return;
    }

    if (event_id == WEBSOCKET_EVENT_DISCONNECTED) {
        g_signalk_state.ws_connected = false;
        g_signalk_state.status.state = SIGNALK_STATE_DISCONNECTED;
        strncpy(g_signalk_state.status.error_message,
                "WebSocket disconnected",
                sizeof(g_signalk_state.status.error_message) - 1);
        signalk_ws_schedule_reconnect(signalk_now_ms());
        ESP_LOGW(TAG, "WebSocket disconnected");
        return;
    }

    if (event_id == WEBSOCKET_EVENT_DATA) {
        esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
        if (data && data->data_len > 0) {
            g_signalk_state.status.messages_received++;
            g_signalk_state.status.last_message_time = signalk_now_ms();
            g_signalk_state.status.state = SIGNALK_STATE_STREAMING;

            // Parse JSON delta
            char *json_str = malloc(data->data_len + 1);
            if (json_str) {
                memcpy(json_str, data->data_ptr, data->data_len);
                json_str[data->data_len] = '\0';

                cJSON *root = cJSON_Parse(json_str);
                if (root) {
                    // Handle delta message
                    extern void signalk_subscriber_handle_delta(cJSON *delta);
                    signalk_subscriber_handle_delta(root);
                    cJSON_Delete(root);
                }
                free(json_str);
            }
        }
        return;
    }

    if (event_id == WEBSOCKET_EVENT_ERROR) {
        g_signalk_state.ws_connected = false;
        g_signalk_state.status.state = SIGNALK_STATE_ERROR;
        strncpy(g_signalk_state.status.error_message,
                "WebSocket error",
                sizeof(g_signalk_state.status.error_message) - 1);
        signalk_ws_schedule_reconnect(signalk_now_ms());
        ESP_LOGE(TAG, "WebSocket error");
    }
}

static esp_err_t signalk_ws_start(void) {
#if SIGNALK_WS_AVAILABLE == 0
    ESP_LOGW(TAG, "WebSocket client not available - esp_websocket_client.h not found");
    g_signalk_state.status.state = SIGNALK_STATE_DISCONNECTED;
    snprintf(g_signalk_state.status.error_message,
             sizeof(g_signalk_state.status.error_message),
             "WebSocket component not available");
    return ESP_ERR_NOT_SUPPORTED;
#endif

    const char *scheme = g_signalk_state.config.use_ssl ? "wss" : "ws";
    snprintf(g_signalk_state.ws_url, sizeof(g_signalk_state.ws_url),
             "%s://%s:%d/signalk/v1/stream",
             scheme,
             g_signalk_state.config.hostname,
             g_signalk_state.config.port);

    if (g_signalk_state.config.token[0] != '\0') {
        // Limit token length to avoid header truncation warnings
        snprintf(g_signalk_state.ws_headers, sizeof(g_signalk_state.ws_headers),
                 "Authorization: Bearer %.200s\r\n",
                 g_signalk_state.config.token);
    } else {
        g_signalk_state.ws_headers[0] = '\0';
    }

    if (g_signalk_state.ws_client) {
        esp_websocket_client_stop(g_signalk_state.ws_client);
        esp_websocket_client_destroy(g_signalk_state.ws_client);
        g_signalk_state.ws_client = NULL;
    }

    esp_websocket_client_config_t cfg = {
        .uri = g_signalk_state.ws_url,
        .headers = g_signalk_state.ws_headers,
        .buffer_size = 1024,
        .disable_auto_reconnect = true
    };

    ESP_LOGI(TAG, "Starting WebSocket connection to %s", g_signalk_state.ws_url);
    
    g_signalk_state.ws_client = esp_websocket_client_init(&cfg);
    if (!g_signalk_state.ws_client) {
        ESP_LOGE(TAG, "Failed to initialize WebSocket client");
        return ESP_ERR_NO_MEM;
    }

    esp_websocket_client_register_event(g_signalk_state.ws_client,
                                        WEBSOCKET_EVENT_ANY,
                                        signalk_ws_event_handler,
                                        NULL);

    g_signalk_state.status.state = SIGNALK_STATE_CONNECTING;
    return esp_websocket_client_start(g_signalk_state.ws_client);
}

static void signalk_client_task(void *pvParameters) {
    ESP_LOGI(TAG, "SignalK client task started");

    signalk_auth_request_t auth_request = {0};
    bool auth_pending = false;
    int64_t next_request_ms = 0;
    int64_t next_poll_ms = 0;

    while (1) {
        int64_t now_ms = signalk_now_ms();

        if (!g_signalk_state.config.enabled) {
            g_signalk_state.status.state = SIGNALK_STATE_DISABLED;
            g_signalk_state.status.authenticated = false;
            g_signalk_state.status.error_message[0] = '\0';
        } else {
            g_signalk_state.status.state = SIGNALK_STATE_DISCONNECTED;

            if (g_signalk_state.config.hostname[0] == '\0') {
                snprintf(g_signalk_state.status.error_message,
                         sizeof(g_signalk_state.status.error_message),
                         "Hostname not set");
            } else if (g_signalk_state.config.token[0] == '\0') {
                g_signalk_state.status.state = SIGNALK_STATE_AUTH_PENDING;
                g_signalk_state.status.authenticated = false;

                if (!auth_pending && now_ms >= next_request_ms) {
                    ESP_LOGI(TAG, "Requesting SignalK access token");
                    memset(&auth_request, 0, sizeof(auth_request));

                    esp_err_t err = signalk_auth_request_token(
                        g_signalk_state.config.hostname,
                        g_signalk_state.config.port,
                        g_signalk_state.config.use_ssl,
                        g_signalk_state.config.client_id,
                        g_signalk_state.config.vessel_name,
                        &auth_request
                    );

                    if (err == ESP_OK && auth_request.request_id[0] != '\0') {
                        auth_pending = true;
                        next_poll_ms = now_ms + 3000;
                        g_signalk_state.status.error_message[0] = '\0';
                    } else {
                        auth_pending = false;
                        next_request_ms = now_ms + 5000;
                        snprintf(g_signalk_state.status.error_message,
                                 sizeof(g_signalk_state.status.error_message),
                                 "Auth request failed");
                    }
                } else if (auth_pending && now_ms >= next_poll_ms) {
                    signalk_auth_request_t updated = {0};
                    esp_err_t err = signalk_auth_check_request(
                        g_signalk_state.config.hostname,
                        g_signalk_state.config.port,
                        g_signalk_state.config.use_ssl,
                        auth_request.request_id,
                        &updated
                    );

                    if (err == ESP_OK) {
                        if (updated.state == AUTH_REQUEST_APPROVED && updated.token[0] != '\0') {
                            strncpy(g_signalk_state.config.token, updated.token,
                                    sizeof(g_signalk_state.config.token) - 1);
                            g_signalk_state.config.token[sizeof(g_signalk_state.config.token) - 1] = '\0';
                            signalk_storage_save_config(&g_signalk_state.config);

                            g_signalk_state.status.authenticated = true;
                            g_signalk_state.status.state = SIGNALK_STATE_DISCONNECTED;
                            g_signalk_state.status.error_message[0] = '\0';
                            auth_pending = false;
                        } else if (updated.state == AUTH_REQUEST_DENIED) {
                            auth_pending = false;
                            next_request_ms = now_ms + 5000;
                            snprintf(g_signalk_state.status.error_message,
                                     sizeof(g_signalk_state.status.error_message),
                                     "Auth denied by server");
                        } else if (updated.state == AUTH_REQUEST_TIMEOUT) {
                            auth_pending = false;
                            next_request_ms = now_ms + 5000;
                            snprintf(g_signalk_state.status.error_message,
                                     sizeof(g_signalk_state.status.error_message),
                                     "Auth request timed out");
                        } else {
                            next_poll_ms = now_ms + 3000;
                        }
                    } else {
                        auth_pending = false;
                        next_request_ms = now_ms + 5000;
                        snprintf(g_signalk_state.status.error_message,
                                 sizeof(g_signalk_state.status.error_message),
                                 "Auth poll failed");
                    }
                }
            } else {
                g_signalk_state.status.authenticated = true;
                g_signalk_state.status.error_message[0] = '\0';
                auth_pending = false;

                // Update server info
                strncpy(g_signalk_state.status.server.hostname,
                        g_signalk_state.config.hostname,
                        sizeof(g_signalk_state.status.server.hostname) - 1);
                g_signalk_state.status.server.port = g_signalk_state.config.port;
                g_signalk_state.status.server.ssl_enabled = g_signalk_state.config.use_ssl;

                if (!g_signalk_state.ws_connected) {
                    if (g_signalk_state.next_reconnect_ms == 0 ||
                        now_ms >= g_signalk_state.next_reconnect_ms) {
                        esp_err_t err = signalk_ws_start();
                        if (err != ESP_OK) {
                            if (err == ESP_ERR_NOT_SUPPORTED) {
                                // WebSocket not available, but HTTP API works
                                g_signalk_state.status.state = SIGNALK_STATE_CONNECTED;
                                ESP_LOGI(TAG, "Connected via HTTP (WebSocket not available)");
                                g_signalk_state.next_reconnect_ms = INT64_MAX;
                            } else {
                                ESP_LOGW(TAG, "WebSocket connection failed: %s", esp_err_to_name(err));
                                signalk_ws_schedule_reconnect(now_ms);
                            }
                        } else {
                            signalk_ws_reset_reconnect();
                        }
                    } else {
                        // Waiting to retry WebSocket, but still connected via HTTP
                        g_signalk_state.status.state = SIGNALK_STATE_CONNECTED;
                    }
                }
            }
        }

        // Update uptime
        g_signalk_state.status.uptime_seconds++;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

esp_err_t signalk_client_init(void) {
    if (g_signalk_state.initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing SignalK client");

    // Load configuration from NVS
    esp_err_t err = signalk_storage_load_config(&g_signalk_state.config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load configuration: %s", esp_err_to_name(err));
        return err;
    }

    // Initialize status
    memset(&g_signalk_state.status, 0, sizeof(signalk_status_t));
    g_signalk_state.status.state = SIGNALK_STATE_DISCONNECTED;

    // Create command queue for inter-task communication
    g_signalk_state.command_queue = xQueueCreate(10, sizeof(uint32_t));
    if (!g_signalk_state.command_queue) {
        ESP_LOGE(TAG, "Failed to create command queue");
        return ESP_ERR_NO_MEM;
    }

    // Create client task (but don't start it yet)
    g_signalk_state.initialized = true;
    ESP_LOGI(TAG, "SignalK client initialized");

    return ESP_OK;
}

esp_err_t signalk_client_start(void) {
    if (!g_signalk_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!g_signalk_state.config.enabled) {
        g_signalk_state.status.state = SIGNALK_STATE_DISABLED;
        ESP_LOGI(TAG, "SignalK is disabled in configuration");
        return ESP_OK;
    }

    if (g_signalk_state.client_task != NULL) {
        ESP_LOGW(TAG, "Client task already running");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting SignalK client");
    g_signalk_state.status.state = SIGNALK_STATE_DISCONNECTED;

    BaseType_t result = xTaskCreate(
        signalk_client_task,
        "signalk_client",
        8192,
        NULL,
        5,
        &g_signalk_state.client_task
    );

    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create client task");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t signalk_client_stop(void) {
    if (g_signalk_state.client_task != NULL) {
        ESP_LOGI(TAG, "Stopping SignalK client");
        vTaskDelete(g_signalk_state.client_task);
        g_signalk_state.client_task = NULL;
    }

    signalk_ws_stop();
    signalk_ws_reset_reconnect();

    g_signalk_state.status.state = SIGNALK_STATE_DISCONNECTED;
    return ESP_OK;
}

esp_err_t signalk_get_config(signalk_config_t *config) {
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(config, &g_signalk_state.config, sizeof(signalk_config_t));
    return ESP_OK;
}

esp_err_t signalk_set_config(const signalk_config_t *config) {
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    signalk_config_t previous = g_signalk_state.config;

    // Save to NVS
    esp_err_t err = signalk_storage_save_config(config);
    if (err != ESP_OK) {
        return err;
    }

    // Update in-memory config
    memcpy(&g_signalk_state.config, config, sizeof(signalk_config_t));

    bool server_changed =
        strcmp(previous.hostname, config->hostname) != 0 ||
        previous.port != config->port ||
        previous.use_ssl != config->use_ssl ||
        strcmp(previous.token, config->token) != 0;

    if (server_changed) {
        signalk_ws_stop();
        signalk_ws_reset_reconnect();
    }

    // If connection state changes, restart client
    if (config->enabled != g_signalk_state.config.enabled) {
        if (config->enabled) {
            signalk_client_start();
        } else {
            signalk_client_stop();
        }
    }

    return ESP_OK;
}

esp_err_t signalk_get_status(signalk_status_t *status) {
    if (!status) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(status, &g_signalk_state.status, sizeof(signalk_status_t));
    return ESP_OK;
}

esp_err_t signalk_send_data(const signalk_data_t *data) {
    if (!data) {
        return ESP_ERR_INVALID_ARG;
    }

    // Check if authenticated
    if (!g_signalk_state.status.authenticated || g_signalk_state.config.token[0] == '\0') {
        ESP_LOGW(TAG, "Not authenticated, cannot send data");
        return ESP_ERR_INVALID_STATE;
    }

    // Build value JSON (Signal K HTTP PUT format)
    cJSON *root = cJSON_CreateObject();
    
    // Add value based on type
    switch (data->type) {
        case SIGNALK_VALUE_BOOL:
            cJSON_AddBoolToObject(root, "value", data->value.b);
            break;
        case SIGNALK_VALUE_INT:
            cJSON_AddNumberToObject(root, "value", data->value.i);
            break;
        case SIGNALK_VALUE_FLOAT:
            cJSON_AddNumberToObject(root, "value", data->value.f);
            break;
        case SIGNALK_VALUE_STRING:
            cJSON_AddStringToObject(root, "value", data->value.s);
            break;
        case SIGNALK_VALUE_POSITION: {
            cJSON *pos = cJSON_CreateObject();
            cJSON_AddNumberToObject(pos, "latitude", data->value.pos.latitude);
            cJSON_AddNumberToObject(pos, "longitude", data->value.pos.longitude);
            if (data->value.pos.altitude != 0) {
                cJSON_AddNumberToObject(pos, "altitude", data->value.pos.altitude);
            }
            cJSON_AddItemToObject(root, "value", pos);
            break;
        }
        default:
            cJSON_Delete(root);
            return ESP_ERR_INVALID_ARG;
    }

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    // Build URL with path (convert dots to slashes)
    const char *scheme = g_signalk_state.config.use_ssl ? "https" : "http";
    char url[512];
    char path_buf[200];
    
    // Convert path: "environment.outside.temperature" -> "environment/outside/temperature"
    strncpy(path_buf, data->path, sizeof(path_buf) - 1);
    path_buf[sizeof(path_buf) - 1] = '\0';
    for (char *p = path_buf; *p; p++) {
        if (*p == '.') *p = '/';
    }
    
    snprintf(url, sizeof(url), "%s://%s:%d/signalk/v1/api/vessels/self/%s",
             scheme, g_signalk_state.config.hostname, g_signalk_state.config.port, path_buf);

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_PUT,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        free(payload);
        return ESP_ERR_NO_MEM;
    }

    // Set headers
    char auth_header[600];
    int header_len = snprintf(auth_header, sizeof(auth_header) - 1, "Bearer %s", g_signalk_state.config.token);
    if (header_len > 0 && header_len < (int)sizeof(auth_header)) {
        auth_header[header_len] = '\0';
        esp_http_client_set_header(client, "Authorization", auth_header);
    }
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, payload, strlen(payload));

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);

    esp_http_client_cleanup(client);
    free(payload);

    if (err == ESP_OK && status >= 200 && status < 300) {
        g_signalk_state.status.messages_sent++;
        ESP_LOGI(TAG, "Data sent to %s (status: %d)", data->path, status);
        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "Failed to send data to %s: %s, status: %d", 
                 data->path, esp_err_to_name(err), status);
        return ESP_FAIL;
    }
}

esp_err_t signalk_connect(const char *hostname, uint16_t port) {
    if (!hostname) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Attempting to connect to %s:%d", hostname, port);
    g_signalk_state.status.state = SIGNALK_STATE_CONNECTING;

    strncpy(g_signalk_state.config.hostname, hostname,
            sizeof(g_signalk_state.config.hostname) - 1);
    g_signalk_state.config.hostname[sizeof(g_signalk_state.config.hostname) - 1] = '\0';
    g_signalk_state.config.port = port;
    signalk_storage_save_config(&g_signalk_state.config);

    signalk_ws_stop();
    signalk_ws_reset_reconnect();
    g_signalk_state.next_reconnect_ms = 0;

    // Connection logic will be implemented in WebSocket phase
    return ESP_OK;
}

esp_err_t signalk_disconnect(void) {
    ESP_LOGI(TAG, "Disconnecting from SignalK server");
    g_signalk_state.status.state = SIGNALK_STATE_DISCONNECTED;

    signalk_ws_stop();
    signalk_ws_reset_reconnect();

    // Cleanup logic will be implemented in WebSocket phase
    return ESP_OK;
}

esp_err_t signalk_test_connection(const char *hostname, uint16_t port) {
    if (!hostname) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Testing connection to %s:%d", hostname, port);

    // HTTP GET /signalk/v1 to test
    // Implementation in HTTP client phase
    return ESP_OK;
}

// WebSocket subscription functions
esp_err_t signalk_ws_subscribe(const char *path, uint32_t period_ms) {
#if SIGNALK_WS_AVAILABLE == 0
    ESP_LOGW(TAG, "WebSocket not available for subscriptions");
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (!g_signalk_state.ws_client || !g_signalk_state.ws_connected) {
        ESP_LOGW(TAG, "WebSocket not connected");
        return ESP_ERR_INVALID_STATE;
    }

    // Build subscription message
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "context", "vessels.self");
    
    cJSON *subscribe = cJSON_CreateArray();
    cJSON *sub_item = cJSON_CreateObject();
    cJSON_AddStringToObject(sub_item, "path", path);
    cJSON_AddNumberToObject(sub_item, "period", period_ms);
    cJSON_AddStringToObject(sub_item, "format", "delta");
    cJSON_AddStringToObject(sub_item, "policy", "instant");
    cJSON_AddItemToArray(subscribe, sub_item);
    cJSON_AddItemToObject(root, "subscribe", subscribe);

    char *msg = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!msg) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Sending subscription: %s", msg);
    
    int sent = esp_websocket_client_send_text(g_signalk_state.ws_client, 
                                              msg, strlen(msg), 
                                              pdMS_TO_TICKS(5000));
    free(msg);

    return (sent > 0) ? ESP_OK : ESP_FAIL;
#endif
}

esp_err_t signalk_ws_unsubscribe(const char *path) {
#if SIGNALK_WS_AVAILABLE == 0
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (!g_signalk_state.ws_client || !g_signalk_state.ws_connected) {
        return ESP_ERR_INVALID_STATE;
    }

    // Build unsubscribe message
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "context", "vessels.self");
    
    cJSON *unsubscribe = cJSON_CreateArray();
    cJSON *unsub_item = cJSON_CreateObject();
    cJSON_AddStringToObject(unsub_item, "path", path);
    cJSON_AddItemToArray(unsubscribe, unsub_item);
    cJSON_AddItemToObject(root, "unsubscribe", unsubscribe);

    char *msg = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!msg) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Sending unsubscribe: %s", msg);
    
    int sent = esp_websocket_client_send_text(g_signalk_state.ws_client, 
                                              msg, strlen(msg), 
                                              pdMS_TO_TICKS(5000));
    free(msg);

    return (sent > 0) ? ESP_OK : ESP_FAIL;
#endif
}
