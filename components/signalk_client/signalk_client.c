#include "signalk_types.h"
#include "signalk_client.h"
#include "signalk_mdns.h"
#include "signalk_auth.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>

static const char *TAG = "signalk_client";

// Global state
static struct {
    signalk_config_t config;
    signalk_status_t status;
    TaskHandle_t client_task;
    QueueHandle_t command_queue;
    bool initialized;
} g_signalk_state = {
    .initialized = false,
    .client_task = NULL,
    .command_queue = NULL,
};

// Forward declarations
extern esp_err_t signalk_storage_load_config(signalk_config_t *config);
extern esp_err_t signalk_storage_save_config(const signalk_config_t *config);

static void signalk_client_task(void *pvParameters) {
    ESP_LOGI(TAG, "SignalK client task started");

    while (1) {
        // Main client loop will be populated in phases
        vTaskDelay(pdMS_TO_TICKS(1000));

        // Update uptime
        g_signalk_state.status.uptime_seconds++;
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

    // Save to NVS
    esp_err_t err = signalk_storage_save_config(config);
    if (err != ESP_OK) {
        return err;
    }

    // Update in-memory config
    memcpy(&g_signalk_state.config, config, sizeof(signalk_config_t));

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

    if (g_signalk_state.status.state != SIGNALK_STATE_STREAMING) {
        ESP_LOGW(TAG, "Not connected, cannot send data");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Queueing data for path: %s", data->path);
    g_signalk_state.status.messages_sent++;

    // Actual sending will be implemented in WebSocket phase
    return ESP_OK;
}

esp_err_t signalk_connect(const char *hostname, uint16_t port) {
    if (!hostname) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Attempting to connect to %s:%d", hostname, port);
    g_signalk_state.status.state = SIGNALK_STATE_CONNECTING;

    // Connection logic will be implemented in WebSocket phase
    return ESP_OK;
}

esp_err_t signalk_disconnect(void) {
    ESP_LOGI(TAG, "Disconnecting from SignalK server");
    g_signalk_state.status.state = SIGNALK_STATE_DISCONNECTED;

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
