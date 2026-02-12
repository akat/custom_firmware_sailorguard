#include "signalk_types.h"
#include "signalk_client.h"
#include "signalk_mdns.h"
#include "signalk_auth.h"
#include "signalk_udp.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

#if __has_include("esp_websocket_client.h")
#include "esp_websocket_client.h"
#define SIGNALK_WS_AVAILABLE 1
#else
#include "esp_err.h"
typedef void *esp_websocket_client_handle_t;
typedef void *esp_event_base_t;
typedef struct {
    const char *uri;
    const char *headers;
    int buffer_size;
    bool disable_auto_reconnect;
} esp_websocket_client_config_t;
typedef struct {
    int data_len;
    const char *data_ptr;
    int payload_len;
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
static inline esp_err_t esp_websocket_register_events(esp_websocket_client_handle_t client,
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
                                                  const char *data, int len, TickType_t timeout) {
    (void)client;
    (void)data;
    (void)len;
    (void)timeout;
    return -1;
}
#define SIGNALK_WS_AVAILABLE 0
#endif

static const char *TAG = "signalk_client";

#define SIGNALK_MAX_SUBSCRIPTIONS  8
#define SIGNALK_MAX_CALLBACKS      4
#define SIGNALK_MAX_DATA_CACHE     16
#define SIGNALK_DUPLICATE_WINDOW_MS 100

typedef enum {
    SIGNALK_SOURCE_WS = 0,
    SIGNALK_SOURCE_UDP
} signalk_transport_source_t;

typedef struct {
    signalk_data_t data;
    int64_t last_update_ms;
    signalk_transport_source_t source;
} signalk_cache_entry_t;

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

    // Subscriptions
    signalk_subscription_t subscriptions[SIGNALK_MAX_SUBSCRIPTIONS];
    uint8_t subscription_count;
    bool subscriptions_sent;

    // Callbacks
    struct {
        signalk_data_callback_t fn;
        void *ctx;
        char path_filter[128];
    } callbacks[SIGNALK_MAX_CALLBACKS];
    uint8_t callback_count;

    // Data cache
    signalk_cache_entry_t data_cache[SIGNALK_MAX_DATA_CACHE];
    uint8_t data_cache_count;

    // Server hello
    char self_context[128];
    bool hello_received;
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
    g_signalk_state.hello_received = false;
    g_signalk_state.subscriptions_sent = false;
}

static bool signalk_ws_enabled(const signalk_config_t *config) {
    return config->transport_mode == SIGNALK_TRANSPORT_WS ||
           config->transport_mode == SIGNALK_TRANSPORT_BOTH;
}

static bool signalk_udp_enabled(const signalk_config_t *config) {
    return config->transport_mode == SIGNALK_TRANSPORT_UDP ||
           config->transport_mode == SIGNALK_TRANSPORT_BOTH;
}

// --- Delta parsing helpers ---

static void signalk_parse_value(cJSON *value_item, signalk_data_t *data) {
    if (!value_item || cJSON_IsNull(value_item)) {
        data->type = SIGNALK_VALUE_NULL;
    } else if (cJSON_IsBool(value_item)) {
        data->type = SIGNALK_VALUE_BOOL;
        data->value.b = cJSON_IsTrue(value_item);
    } else if (cJSON_IsNumber(value_item)) {
        data->type = SIGNALK_VALUE_FLOAT;
        data->value.f = (float)value_item->valuedouble;
    } else if (cJSON_IsString(value_item)) {
        data->type = SIGNALK_VALUE_STRING;
        strncpy(data->value.s, value_item->valuestring, sizeof(data->value.s) - 1);
        data->value.s[sizeof(data->value.s) - 1] = '\0';
    } else if (cJSON_IsObject(value_item)) {
        cJSON *lat = cJSON_GetObjectItem(value_item, "latitude");
        cJSON *lon = cJSON_GetObjectItem(value_item, "longitude");
        if (lat && lon && cJSON_IsNumber(lat) && cJSON_IsNumber(lon)) {
            data->type = SIGNALK_VALUE_POSITION;
            data->value.pos.latitude = lat->valuedouble;
            data->value.pos.longitude = lon->valuedouble;
            cJSON *alt = cJSON_GetObjectItem(value_item, "altitude");
            if (alt && cJSON_IsNumber(alt)) {
                data->value.pos.altitude = alt->valuedouble;
            }
        } else {
            data->type = SIGNALK_VALUE_STRING;
            char *printed = cJSON_PrintUnformatted(value_item);
            if (printed) {
                strncpy(data->value.s, printed, sizeof(data->value.s) - 1);
                data->value.s[sizeof(data->value.s) - 1] = '\0';
                free(printed);
            }
        }
    }
}

static void signalk_update_cache(const signalk_data_t *data, signalk_transport_source_t source) {
    int64_t now_ms = signalk_now_ms();

    for (int i = 0; i < g_signalk_state.data_cache_count; i++) {
        if (strcmp(g_signalk_state.data_cache[i].data.path, data->path) == 0) {
            memcpy(&g_signalk_state.data_cache[i].data, data, sizeof(signalk_data_t));
            g_signalk_state.data_cache[i].last_update_ms = now_ms;
            g_signalk_state.data_cache[i].source = source;
            return;
        }
    }

    if (g_signalk_state.data_cache_count < SIGNALK_MAX_DATA_CACHE) {
        signalk_cache_entry_t *entry =
            &g_signalk_state.data_cache[g_signalk_state.data_cache_count++];
        memcpy(&entry->data, data, sizeof(signalk_data_t));
        entry->last_update_ms = now_ms;
        entry->source = source;
    }
}

static bool signalk_should_drop_udp(const signalk_data_t *data) {
    if (g_signalk_state.config.transport_mode != SIGNALK_TRANSPORT_BOTH) {
        return false;
    }

    int64_t now_ms = signalk_now_ms();
    for (int i = 0; i < g_signalk_state.data_cache_count; i++) {
        if (strcmp(g_signalk_state.data_cache[i].data.path, data->path) == 0) {
            if (g_signalk_state.data_cache[i].source == SIGNALK_SOURCE_WS &&
                (now_ms - g_signalk_state.data_cache[i].last_update_ms) < SIGNALK_DUPLICATE_WINDOW_MS) {
                return true;
            }
            return false;
        }
    }

    return false;
}

static void signalk_fire_callbacks(const signalk_data_t *data) {
    for (int i = 0; i < g_signalk_state.callback_count; i++) {
        const char *filter = g_signalk_state.callbacks[i].path_filter;
        if (filter[0] == '\0' ||
            strncmp(data->path, filter, strlen(filter)) == 0) {
            g_signalk_state.callbacks[i].fn(data, g_signalk_state.callbacks[i].ctx);
        }
    }
}

static void signalk_handle_hello(cJSON *json) {
    cJSON *self = cJSON_GetObjectItem(json, "self");
    if (self && cJSON_IsString(self)) {
        strncpy(g_signalk_state.self_context, self->valuestring,
                sizeof(g_signalk_state.self_context) - 1);
        g_signalk_state.self_context[sizeof(g_signalk_state.self_context) - 1] = '\0';
    }
    cJSON *name = cJSON_GetObjectItem(json, "name");
    g_signalk_state.hello_received = true;
    ESP_LOGI(TAG, "Hello from server: %s, self=%s",
             (name && cJSON_IsString(name)) ? name->valuestring : "?",
             g_signalk_state.self_context);
}

static void signalk_handle_delta(cJSON *json) {
    cJSON *updates = cJSON_GetObjectItem(json, "updates");
    if (!updates || !cJSON_IsArray(updates)) {
        return;
    }

    cJSON *update;
    cJSON_ArrayForEach(update, updates) {
        char source_label[32] = {0};
        cJSON *source = cJSON_GetObjectItem(update, "source");
        if (source) {
            cJSON *label = cJSON_GetObjectItem(source, "label");
            if (label && cJSON_IsString(label)) {
                strncpy(source_label, label->valuestring, sizeof(source_label) - 1);
            }
        }

        cJSON *values = cJSON_GetObjectItem(update, "values");
        if (!values || !cJSON_IsArray(values)) {
            continue;
        }

        cJSON *val_entry;
        cJSON_ArrayForEach(val_entry, values) {
            cJSON *path_item = cJSON_GetObjectItem(val_entry, "path");
            cJSON *value_item = cJSON_GetObjectItem(val_entry, "value");
            if (!path_item || !cJSON_IsString(path_item)) {
                continue;
            }

            signalk_data_t data = {0};
            strncpy(data.path, path_item->valuestring, sizeof(data.path) - 1);
            strncpy(data.source_label, source_label, sizeof(data.source_label) - 1);

            signalk_parse_value(value_item, &data);
            signalk_update_cache(&data, SIGNALK_SOURCE_WS);
            signalk_fire_callbacks(&data);
        }
    }
}

static void signalk_udp_data_cb(const signalk_data_t *data, void *user_ctx) {
    (void)user_ctx;
    if (!data) {
        return;
    }

    if (signalk_should_drop_udp(data)) {
        return;
    }

    signalk_update_cache(data, SIGNALK_SOURCE_UDP);
    signalk_fire_callbacks(data);
    g_signalk_state.status.messages_received++;
    g_signalk_state.status.last_message_time = signalk_now_ms();
}

// --- Subscription helpers ---

static esp_err_t signalk_send_subscribe_msg(const char *path, uint32_t period_ms) {
    if (!g_signalk_state.ws_client || !g_signalk_state.ws_connected) {
        return ESP_ERR_INVALID_STATE;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "context", "vessels.self");

    cJSON *subscribe = cJSON_CreateArray();
    cJSON *entry = cJSON_CreateObject();
    cJSON_AddStringToObject(entry, "path", path);
    if (period_ms > 0) {
        cJSON_AddNumberToObject(entry, "period", period_ms);
    }
    cJSON_AddItemToArray(subscribe, entry);
    cJSON_AddItemToObject(root, "subscribe", subscribe);

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!payload) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Subscribe: %s", payload);
    int sent = esp_websocket_client_send_text(
        g_signalk_state.ws_client, payload, (int)strlen(payload), pdMS_TO_TICKS(1000));
    free(payload);

    return (sent >= 0) ? ESP_OK : ESP_FAIL;
}

static esp_err_t signalk_send_unsubscribe_msg(const char *path) {
    if (!g_signalk_state.ws_client || !g_signalk_state.ws_connected) {
        return ESP_ERR_INVALID_STATE;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "context", "vessels.self");

    cJSON *unsubscribe = cJSON_CreateArray();
    cJSON *entry = cJSON_CreateObject();
    cJSON_AddStringToObject(entry, "path", path);
    cJSON_AddItemToArray(unsubscribe, entry);
    cJSON_AddItemToObject(root, "unsubscribe", unsubscribe);

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!payload) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Unsubscribe: %s", payload);
    int sent = esp_websocket_client_send_text(
        g_signalk_state.ws_client, payload, (int)strlen(payload), pdMS_TO_TICKS(1000));
    free(payload);

    return (sent >= 0) ? ESP_OK : ESP_FAIL;
}

// --- WebSocket event handler ---

static void signalk_ws_event_handler(void *handler_args,
                                     esp_event_base_t base,
                                     int32_t event_id,
                                     void *event_data) {
    (void)handler_args;
    (void)base;

    if (event_id == WEBSOCKET_EVENT_CONNECTED) {
        g_signalk_state.ws_connected = true;
        g_signalk_state.hello_received = false;
        g_signalk_state.subscriptions_sent = false;
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
        g_signalk_state.hello_received = false;
        g_signalk_state.subscriptions_sent = false;
        g_signalk_state.status.state = SIGNALK_STATE_DISCONNECTED;
        strncpy(g_signalk_state.status.error_message,
                "WebSocket disconnected",
                sizeof(g_signalk_state.status.error_message) - 1);
        signalk_ws_schedule_reconnect(signalk_now_ms());
        ESP_LOGW(TAG, "WebSocket disconnected");
        return;
    }

    if (event_id == WEBSOCKET_EVENT_DATA) {
        esp_websocket_event_data_t *ws_data = (esp_websocket_event_data_t *)event_data;
        if (!ws_data || ws_data->data_len <= 0) {
            return;
        }

        // Only handle complete frames
        if (ws_data->payload_len != ws_data->data_len) {
            ESP_LOGW(TAG, "Fragmented WS message (%d/%d), skipping",
                     ws_data->data_len, ws_data->payload_len);
            return;
        }

        // Null-terminate for JSON parsing
        char *buf = malloc((size_t)ws_data->data_len + 1);
        if (!buf) {
            return;
        }
        memcpy(buf, ws_data->data_ptr, (size_t)ws_data->data_len);
        buf[ws_data->data_len] = '\0';

        cJSON *json = cJSON_Parse(buf);
        free(buf);

        if (json) {
            if (cJSON_GetObjectItem(json, "updates")) {
                signalk_handle_delta(json);
                g_signalk_state.status.state = SIGNALK_STATE_STREAMING;
            } else if (cJSON_GetObjectItem(json, "version")) {
                signalk_handle_hello(json);
            }
            cJSON_Delete(json);
        }

        g_signalk_state.status.messages_received++;
        g_signalk_state.status.last_message_time = signalk_now_ms();
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
    const char *scheme = g_signalk_state.config.use_ssl ? "wss" : "ws";
    snprintf(g_signalk_state.ws_url, sizeof(g_signalk_state.ws_url),
             "%s://%s:%d/signalk/v1/stream?subscribe=none",
             scheme,
             g_signalk_state.config.hostname,
             g_signalk_state.config.port);

    if (g_signalk_state.config.token[0] != '\0') {
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
        .buffer_size = 2048,
        .disable_auto_reconnect = true
    };

    g_signalk_state.ws_client = esp_websocket_client_init(&cfg);
    if (!g_signalk_state.ws_client) {
        return ESP_ERR_NO_MEM;
    }

    esp_websocket_register_events(g_signalk_state.ws_client,
                                  WEBSOCKET_EVENT_ANY,
                                  signalk_ws_event_handler,
                                  NULL);

    g_signalk_state.status.state = SIGNALK_STATE_CONNECTING;
    return esp_websocket_client_start(g_signalk_state.ws_client);
}

static esp_err_t signalk_udp_start_from_config(void) {
    signalk_udp_config_t cfg = {0};
    strncpy(cfg.target_ip, g_signalk_state.config.udp_target_ip, sizeof(cfg.target_ip) - 1);
    cfg.target_ip[sizeof(cfg.target_ip) - 1] = '\0';
    cfg.broadcast_port = g_signalk_state.config.udp_broadcast_port;
    cfg.listen_port = g_signalk_state.config.udp_listen_port;
    return signalk_udp_start(&cfg);
}

// --- Main client task ---

static void signalk_client_task(void *pvParameters) {
    ESP_LOGI(TAG, "SignalK client task started");

    signalk_auth_request_t auth_request = {0};
    bool auth_pending = false;
    int64_t next_request_ms = 0;
    int64_t next_poll_ms = 0;

    while (1) {
        int64_t now_ms = signalk_now_ms();

        bool ws_enabled = signalk_ws_enabled(&g_signalk_state.config);
        bool udp_enabled = signalk_udp_enabled(&g_signalk_state.config);
        bool udp_running = signalk_udp_is_running();

        if (!g_signalk_state.config.enabled) {
            g_signalk_state.status.state = SIGNALK_STATE_DISABLED;
            g_signalk_state.status.authenticated = false;
            g_signalk_state.status.error_message[0] = '\0';
        } else if (!ws_enabled) {
            g_signalk_state.status.authenticated = true;
            g_signalk_state.status.error_message[0] = '\0';
            if (udp_enabled && udp_running) {
                g_signalk_state.status.state = SIGNALK_STATE_CONNECTED;
            } else {
                g_signalk_state.status.state = SIGNALK_STATE_DISCONNECTED;
            }
        } else {
            if (!g_signalk_state.ws_connected) {
                g_signalk_state.status.state =
                    (udp_enabled && udp_running) ? SIGNALK_STATE_CONNECTED : SIGNALK_STATE_DISCONNECTED;
            }

            if (g_signalk_state.config.hostname[0] == '\0') {
                snprintf(g_signalk_state.status.error_message,
                         sizeof(g_signalk_state.status.error_message),
                         "Hostname not set");
            } else if (g_signalk_state.config.token[0] == '\0') {
                if (!(udp_enabled && udp_running)) {
                    g_signalk_state.status.state = SIGNALK_STATE_AUTH_PENDING;
                } else {
                    g_signalk_state.status.state = SIGNALK_STATE_CONNECTED;
                }
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
                    signalk_auth_request_t updated = auth_request;
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

                if (!g_signalk_state.ws_connected) {
                    if (g_signalk_state.next_reconnect_ms == 0 ||
                        now_ms >= g_signalk_state.next_reconnect_ms) {
                        esp_err_t err = signalk_ws_start();
                        if (err != ESP_OK) {
                            snprintf(g_signalk_state.status.error_message,
                                     sizeof(g_signalk_state.status.error_message),
                                     "WebSocket connect failed");
                            signalk_ws_schedule_reconnect(now_ms);
                        } else {
                            signalk_ws_reset_reconnect();
                        }
                    }
                }

                // Send subscriptions after hello received
                if (g_signalk_state.ws_connected &&
                    g_signalk_state.hello_received &&
                    !g_signalk_state.subscriptions_sent &&
                    g_signalk_state.subscription_count > 0) {

                    ESP_LOGI(TAG, "Sending %d subscriptions",
                             g_signalk_state.subscription_count);
                    for (int i = 0; i < g_signalk_state.subscription_count; i++) {
                        signalk_send_subscribe_msg(
                            g_signalk_state.subscriptions[i].path,
                            g_signalk_state.subscriptions[i].period_ms);
                        vTaskDelay(pdMS_TO_TICKS(50));
                    }
                    g_signalk_state.subscriptions_sent = true;
                }
            }
        }

        // Update uptime
        g_signalk_state.status.uptime_seconds++;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// --- Public API ---

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
    if (!g_signalk_state.config.enabled) {
        g_signalk_state.config.enabled = true;
        signalk_storage_save_config(&g_signalk_state.config);
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

    signalk_udp_init();
    signalk_udp_set_rx_callback(signalk_udp_data_cb, NULL);

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

    if (signalk_udp_enabled(&g_signalk_state.config)) {
        esp_err_t udp_err = signalk_udp_start_from_config();
        if (udp_err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to start UDP transport");
        }
    } else {
        signalk_udp_stop();
    }

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
    signalk_udp_stop();

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
    signalk_config_t adjusted = *config;

    // Save to NVS
    esp_err_t err = signalk_storage_save_config(&adjusted);
    if (err != ESP_OK) {
        return err;
    }

    // Update in-memory config
    memcpy(&g_signalk_state.config, &adjusted, sizeof(signalk_config_t));

    bool server_changed =
        strcmp(previous.hostname, adjusted.hostname) != 0 ||
        previous.port != adjusted.port ||
        previous.use_ssl != adjusted.use_ssl ||
        strcmp(previous.token, adjusted.token) != 0;

    bool udp_changed =
        strcmp(previous.udp_target_ip, adjusted.udp_target_ip) != 0 ||
        previous.udp_broadcast_port != adjusted.udp_broadcast_port ||
        previous.udp_listen_port != adjusted.udp_listen_port;

    bool enabled_changed = previous.enabled != adjusted.enabled;
    bool transport_changed = previous.transport_mode != adjusted.transport_mode;

    if (server_changed || !signalk_ws_enabled(&adjusted)) {
        signalk_ws_stop();
        signalk_ws_reset_reconnect();
    }

    if (!adjusted.enabled) {
        signalk_udp_stop();
    } else if (signalk_udp_enabled(&adjusted)) {
        if (udp_changed || transport_changed || !signalk_udp_is_running()) {
            signalk_udp_stop();
            signalk_udp_start_from_config();
        }
    } else {
        signalk_udp_stop();
    }

    if (enabled_changed) {
        if (adjusted.enabled) {
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

static esp_err_t signalk_send_ws_data(const signalk_data_t *data) {
    if (!g_signalk_state.ws_connected) {
        return ESP_ERR_INVALID_STATE;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "context", "vessels.self");

    if (data->priority == SIGNALK_PRIORITY_INSTANT) {
        cJSON *meta_source = cJSON_CreateObject();
        cJSON_AddStringToObject(meta_source, "priority", "instant");
        cJSON_AddItemToObject(root, "$source", meta_source);
    }

    cJSON *updates = cJSON_CreateArray();
    cJSON *update = cJSON_CreateObject();

    cJSON *source = cJSON_CreateObject();
    cJSON_AddStringToObject(source, "label",
        data->source_label[0] ? data->source_label : "sailorguard");
    cJSON_AddItemToObject(update, "source", source);

    cJSON *values = cJSON_CreateArray();
    cJSON *val = cJSON_CreateObject();
    cJSON_AddStringToObject(val, "path", data->path);

    switch (data->type) {
        case SIGNALK_VALUE_NULL:
            cJSON_AddNullToObject(val, "value");
            break;
        case SIGNALK_VALUE_BOOL:
            cJSON_AddBoolToObject(val, "value", data->value.b);
            break;
        case SIGNALK_VALUE_INT:
            cJSON_AddNumberToObject(val, "value", data->value.i);
            break;
        case SIGNALK_VALUE_FLOAT:
            cJSON_AddNumberToObject(val, "value", (double)data->value.f);
            break;
        case SIGNALK_VALUE_STRING:
            cJSON_AddStringToObject(val, "value", data->value.s);
            break;
        case SIGNALK_VALUE_POSITION: {
            cJSON *pos = cJSON_CreateObject();
            cJSON_AddNumberToObject(pos, "latitude", data->value.pos.latitude);
            cJSON_AddNumberToObject(pos, "longitude", data->value.pos.longitude);
            if (data->value.pos.altitude != 0.0) {
                cJSON_AddNumberToObject(pos, "altitude", data->value.pos.altitude);
            }
            cJSON_AddItemToObject(val, "value", pos);
            break;
        }
    }

    cJSON_AddItemToArray(values, val);
    cJSON_AddItemToObject(update, "values", values);
    cJSON_AddItemToArray(updates, update);
    cJSON_AddItemToObject(root, "updates", updates);

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!payload) {
        return ESP_ERR_NO_MEM;
    }

    int sent = esp_websocket_client_send_text(
        g_signalk_state.ws_client, payload, (int)strlen(payload), pdMS_TO_TICKS(1000));
    free(payload);

    if (sent < 0) {
        ESP_LOGE(TAG, "Failed to send delta");
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t signalk_send_data(const signalk_data_t *data) {
    if (!data) {
        return ESP_ERR_INVALID_ARG;
    }

    bool ws_ok = false;
    if (signalk_ws_enabled(&g_signalk_state.config)) {
        if (signalk_send_ws_data(data) == ESP_OK) {
            ws_ok = true;
        }
    }

    if (ws_ok) {
        g_signalk_state.status.messages_sent++;
        return ESP_OK;
    }

    if (signalk_udp_enabled(&g_signalk_state.config) && signalk_udp_is_running()) {
        esp_err_t err = signalk_udp_send(data);
        if (err == ESP_OK) {
            g_signalk_state.status.messages_sent++;
        }
        return err;
    }

    return ESP_ERR_INVALID_STATE;
}

esp_err_t signalk_subscribe(const char *path, uint32_t period_ms) {
    if (!path || path[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    // Update existing subscription
    for (int i = 0; i < g_signalk_state.subscription_count; i++) {
        if (strcmp(g_signalk_state.subscriptions[i].path, path) == 0) {
            g_signalk_state.subscriptions[i].period_ms = period_ms;
            if (g_signalk_state.ws_connected) {
                signalk_send_subscribe_msg(path, period_ms);
            }
            return ESP_OK;
        }
    }

    if (g_signalk_state.subscription_count >= SIGNALK_MAX_SUBSCRIPTIONS) {
        return ESP_ERR_NO_MEM;
    }

    signalk_subscription_t *sub =
        &g_signalk_state.subscriptions[g_signalk_state.subscription_count++];
    strncpy(sub->path, path, sizeof(sub->path) - 1);
    sub->path[sizeof(sub->path) - 1] = '\0';
    sub->period_ms = period_ms;

    if (g_signalk_state.ws_connected) {
        signalk_send_subscribe_msg(path, period_ms);
    }

    ESP_LOGI(TAG, "Subscribed to %s (period=%lu ms)", path, (unsigned long)period_ms);
    return ESP_OK;
}

esp_err_t signalk_unsubscribe(const char *path) {
    if (!path || path[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    for (int i = 0; i < g_signalk_state.subscription_count; i++) {
        if (strcmp(g_signalk_state.subscriptions[i].path, path) == 0) {
            if (g_signalk_state.ws_connected) {
                signalk_send_unsubscribe_msg(path);
            }

            // Shift remaining subscriptions
            for (int j = i; j < g_signalk_state.subscription_count - 1; j++) {
                g_signalk_state.subscriptions[j] = g_signalk_state.subscriptions[j + 1];
            }
            g_signalk_state.subscription_count--;

            ESP_LOGI(TAG, "Unsubscribed from %s", path);
            return ESP_OK;
        }
    }

    return ESP_ERR_NOT_FOUND;
}

esp_err_t signalk_register_callback(const char *path_filter,
                                     signalk_data_callback_t callback,
                                     void *user_ctx) {
    if (!callback) {
        return ESP_ERR_INVALID_ARG;
    }

    if (g_signalk_state.callback_count >= SIGNALK_MAX_CALLBACKS) {
        return ESP_ERR_NO_MEM;
    }

    int idx = g_signalk_state.callback_count++;
    g_signalk_state.callbacks[idx].fn = callback;
    g_signalk_state.callbacks[idx].ctx = user_ctx;
    if (path_filter && path_filter[0]) {
        strncpy(g_signalk_state.callbacks[idx].path_filter, path_filter,
                sizeof(g_signalk_state.callbacks[idx].path_filter) - 1);
        g_signalk_state.callbacks[idx].path_filter[sizeof(g_signalk_state.callbacks[idx].path_filter) - 1] = '\0';
    } else {
        g_signalk_state.callbacks[idx].path_filter[0] = '\0';
    }

    return ESP_OK;
}

esp_err_t signalk_get_cached_value(const char *path, signalk_data_t *data) {
    if (!path || !data) {
        return ESP_ERR_INVALID_ARG;
    }

    for (int i = 0; i < g_signalk_state.data_cache_count; i++) {
        if (strcmp(g_signalk_state.data_cache[i].data.path, path) == 0) {
            memcpy(data, &g_signalk_state.data_cache[i].data, sizeof(signalk_data_t));
            return ESP_OK;
        }
    }

    return ESP_ERR_NOT_FOUND;
}

esp_err_t signalk_get_subscriptions(signalk_subscription_t *subs, size_t max, size_t *count) {
    if (!subs || !count) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t n = g_signalk_state.subscription_count;
    if (n > max) {
        n = max;
    }

    for (size_t i = 0; i < n; i++) {
        memcpy(&subs[i], &g_signalk_state.subscriptions[i], sizeof(signalk_subscription_t));
    }
    *count = n;
    return ESP_OK;
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

    return ESP_OK;
}

esp_err_t signalk_disconnect(void) {
    ESP_LOGI(TAG, "Disconnecting from SignalK server");
    g_signalk_state.status.state = SIGNALK_STATE_DISCONNECTED;

    signalk_ws_stop();
    signalk_ws_reset_reconnect();

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
