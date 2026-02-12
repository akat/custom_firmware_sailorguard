#include "signalk_udp.h"
#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "signalk_udp";

#define SIGNALK_UDP_BUFFER_SIZE 8192
#define SIGNALK_UDP_RECV_TIMEOUT_MS 1000

typedef struct {
    signalk_udp_config_t config;
    int rx_sock;
    int tx_sock;
    struct sockaddr_in tx_addr;
    TaskHandle_t task;
    bool running;
    signalk_udp_rx_callback_t rx_cb;
    void *rx_ctx;
    char *rx_buffer;
    size_t rx_buffer_size;
} signalk_udp_state_t;

static signalk_udp_state_t g_udp = {
    .rx_sock = -1,
    .tx_sock = -1,
    .task = NULL,
    .running = false,
    .rx_cb = NULL,
    .rx_ctx = NULL,
    .rx_buffer = NULL,
    .rx_buffer_size = 0,
};

static void signalk_udp_parse_value(cJSON *value_item, signalk_data_t *data) {
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

static void signalk_udp_emit_value(const char *path, cJSON *value_item, const char *source_label) {
    if (!path || !path[0] || !g_udp.rx_cb) {
        return;
    }

    signalk_data_t data = {0};
    strncpy(data.path, path, sizeof(data.path) - 1);
    if (source_label && source_label[0]) {
        strncpy(data.source_label, source_label, sizeof(data.source_label) - 1);
    } else {
        strncpy(data.source_label, "udp", sizeof(data.source_label) - 1);
    }

    signalk_udp_parse_value(value_item, &data);
    g_udp.rx_cb(&data, g_udp.rx_ctx);
}

static void signalk_udp_process_json(cJSON *root) {
    if (!root || !cJSON_IsObject(root)) {
        return;
    }

    cJSON *updates = cJSON_GetObjectItem(root, "updates");
    if (updates && cJSON_IsArray(updates)) {
        cJSON *update;
        cJSON_ArrayForEach(update, updates) {
            const char *source_label = NULL;
            cJSON *source = cJSON_GetObjectItem(update, "source");
            if (source) {
                cJSON *label = cJSON_GetObjectItem(source, "label");
                if (label && cJSON_IsString(label)) {
                    source_label = label->valuestring;
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
                signalk_udp_emit_value(path_item->valuestring, value_item, source_label);
            }
        }
        return;
    }

    cJSON *path_item = cJSON_GetObjectItem(root, "path");
    cJSON *value_item = cJSON_GetObjectItem(root, "value");
    if (path_item && cJSON_IsString(path_item) && value_item) {
        signalk_udp_emit_value(path_item->valuestring, value_item, NULL);
    }
}

static void signalk_udp_task(void *arg) {
    (void)arg;
    char *buffer = g_udp.rx_buffer;
    size_t buffer_size = g_udp.rx_buffer_size;

    if (!buffer || buffer_size < 2) {
        ESP_LOGE(TAG, "UDP RX buffer not initialized");
        g_udp.running = false;
        vTaskDelete(NULL);
        return;
    }

    while (g_udp.running) {
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);

        int len = recvfrom(g_udp.rx_sock, buffer, (int)buffer_size - 1, 0,
                           (struct sockaddr *)&from_addr, &from_len);
        if (len < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                continue;
            }
            ESP_LOGW(TAG, "recvfrom failed: errno=%d", errno);
            continue;
        }

        buffer[len] = '\0';
        cJSON *json = cJSON_Parse(buffer);
        if (!json) {
            ESP_LOGW(TAG, "UDP JSON parse failed");
            continue;
        }

        signalk_udp_process_json(json);
        cJSON_Delete(json);
    }

    vTaskDelete(NULL);
}

esp_err_t signalk_udp_init(void) {
    return ESP_OK;
}

static void signalk_udp_close_socket(int *sock) {
    if (sock && *sock >= 0) {
        close(*sock);
        *sock = -1;
    }
}

esp_err_t signalk_udp_start(const signalk_udp_config_t *config) {
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    if (g_udp.running) {
        signalk_udp_stop();
    }

    memcpy(&g_udp.config, config, sizeof(signalk_udp_config_t));

    g_udp.rx_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (g_udp.rx_sock < 0) {
        ESP_LOGE(TAG, "Failed to create RX socket: errno=%d", errno);
        return ESP_FAIL;
    }

    int reuse = 1;
    setsockopt(g_udp.rx_sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct timeval timeout = {
        .tv_sec = SIGNALK_UDP_RECV_TIMEOUT_MS / 1000,
        .tv_usec = (SIGNALK_UDP_RECV_TIMEOUT_MS % 1000) * 1000
    };
    setsockopt(g_udp.rx_sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    struct sockaddr_in rx_addr = {0};
    rx_addr.sin_family = AF_INET;
    rx_addr.sin_port = htons(g_udp.config.listen_port);
    rx_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(g_udp.rx_sock, (struct sockaddr *)&rx_addr, sizeof(rx_addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind RX socket: errno=%d", errno);
        signalk_udp_close_socket(&g_udp.rx_sock);
        return ESP_FAIL;
    }

    g_udp.tx_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (g_udp.tx_sock < 0) {
        ESP_LOGE(TAG, "Failed to create TX socket: errno=%d", errno);
        signalk_udp_close_socket(&g_udp.rx_sock);
        return ESP_FAIL;
    }

    int broadcast = 1;
    setsockopt(g_udp.tx_sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));

    memset(&g_udp.tx_addr, 0, sizeof(g_udp.tx_addr));
    g_udp.tx_addr.sin_family = AF_INET;
    g_udp.tx_addr.sin_port = htons(g_udp.config.broadcast_port);
    {
        struct in_addr addr;
        if (inet_aton(g_udp.config.target_ip, &addr) != 0) {
            g_udp.tx_addr.sin_addr = addr;
        } else {
            g_udp.tx_addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
        }
    }

    if (!g_udp.rx_buffer) {
        g_udp.rx_buffer = (char *)malloc(SIGNALK_UDP_BUFFER_SIZE);
        if (!g_udp.rx_buffer) {
            ESP_LOGE(TAG, "Failed to allocate UDP RX buffer");
            signalk_udp_close_socket(&g_udp.rx_sock);
            signalk_udp_close_socket(&g_udp.tx_sock);
            return ESP_ERR_NO_MEM;
        }
        g_udp.rx_buffer_size = SIGNALK_UDP_BUFFER_SIZE;
    }

    g_udp.running = true;

    BaseType_t result = xTaskCreate(
        signalk_udp_task,
        "signalk_udp",
        4096,
        NULL,
        4,
        &g_udp.task
    );

    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create UDP task");
        g_udp.running = false;
        signalk_udp_close_socket(&g_udp.rx_sock);
        signalk_udp_close_socket(&g_udp.tx_sock);
        g_udp.task = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "UDP transport started (listen=%u, target=%s:%u)",
             (unsigned)g_udp.config.listen_port,
             g_udp.config.target_ip,
             (unsigned)g_udp.config.broadcast_port);
    return ESP_OK;
}

void signalk_udp_stop(void) {
    if (!g_udp.running) {
        return;
    }

    g_udp.running = false;
    if (g_udp.task) {
        vTaskDelete(g_udp.task);
        g_udp.task = NULL;
    }

    signalk_udp_close_socket(&g_udp.rx_sock);
    signalk_udp_close_socket(&g_udp.tx_sock);
    if (g_udp.rx_buffer) {
        free(g_udp.rx_buffer);
        g_udp.rx_buffer = NULL;
        g_udp.rx_buffer_size = 0;
    }
    ESP_LOGI(TAG, "UDP transport stopped");
}

bool signalk_udp_is_running(void) {
    return g_udp.running;
}

esp_err_t signalk_udp_send(const signalk_data_t *data) {
    if (!data) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!g_udp.running || g_udp.tx_sock < 0) {
        return ESP_ERR_INVALID_STATE;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "context", "vessels.self");

    cJSON *updates = cJSON_CreateArray();
    cJSON *update = cJSON_CreateObject();
    cJSON_AddNumberToObject(update, "timestamp",
        (double)((uint32_t)xTaskGetTickCount() * (uint32_t)portTICK_PERIOD_MS));

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

    int sent = sendto(g_udp.tx_sock, payload, strlen(payload), 0,
                      (struct sockaddr *)&g_udp.tx_addr, sizeof(g_udp.tx_addr));
    free(payload);

    if (sent < 0) {
        ESP_LOGW(TAG, "UDP send failed: errno=%d", errno);
        return ESP_FAIL;
    }

    return ESP_OK;
}

void signalk_udp_set_rx_callback(signalk_udp_rx_callback_t cb, void *ctx) {
    g_udp.rx_cb = cb;
    g_udp.rx_ctx = ctx;
}
