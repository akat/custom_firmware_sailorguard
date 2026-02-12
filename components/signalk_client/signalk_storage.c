#include "signalk_types.h"
#include "signalk_client.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "signalk_storage";
static const char *SIGNALK_NVS_NAMESPACE = "signalk";

// NVS Keys
#define KEY_ENABLED         "enabled"
#define KEY_AUTO_DISCOVERY  "auto_disc"
#define KEY_HOSTNAME        "hostname"
#define KEY_PORT            "port"
#define KEY_USE_SSL         "use_ssl"
#define KEY_TRANSPORT_MODE  "transport"
#define KEY_UDP_IP          "udp_ip"
#define KEY_UDP_BCAST       "udp_bcast"
#define KEY_UDP_LISTEN      "udp_listen"
#define KEY_TOKEN           "token"
#define KEY_VESSEL_NAME     "vessel_name"
#define KEY_CLIENT_ID       "client_id"

esp_err_t signalk_storage_init(void) {
    // NVS is already initialized by main app
    return ESP_OK;
}

esp_err_t signalk_storage_load_config(signalk_config_t *config) {
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(SIGNALK_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No saved config, using defaults");
        // Set defaults
        memset(config, 0, sizeof(signalk_config_t));
        config->enabled = false;
        config->auto_discovery = true;
        config->transport_mode = SIGNALK_TRANSPORT_BOTH;
        config->port = 3000;
        config->use_ssl = false;
        strcpy(config->udp_target_ip, "255.255.255.255");
        config->udp_broadcast_port = 5555;
        config->udp_listen_port = 5556;
        strcpy(config->vessel_name, "SailorGuard");
        strcpy(config->client_id, "sailorguard-device");
        return ESP_OK;
    }

    // Load all values with defaults
    uint8_t u8_val;
    uint16_t u16_val;
    size_t len;

    // enabled
    if (nvs_get_u8(handle, KEY_ENABLED, &u8_val) == ESP_OK) {
        config->enabled = (bool)u8_val;
    } else {
        config->enabled = false;
    }

    // auto_discovery
    if (nvs_get_u8(handle, KEY_AUTO_DISCOVERY, &u8_val) == ESP_OK) {
        config->auto_discovery = (bool)u8_val;
    } else {
        config->auto_discovery = true;
    }

    // hostname
    len = sizeof(config->hostname);
    if (nvs_get_str(handle, KEY_HOSTNAME, config->hostname, &len) != ESP_OK) {
        config->hostname[0] = '\0';
    }

    // port
    if (nvs_get_u16(handle, KEY_PORT, &u16_val) == ESP_OK) {
        config->port = u16_val;
    } else {
        config->port = 3000;
    }

    // use_ssl
    if (nvs_get_u8(handle, KEY_USE_SSL, &u8_val) == ESP_OK) {
        config->use_ssl = (bool)u8_val;
    } else {
        config->use_ssl = false;
    }

    // transport_mode
    if (nvs_get_u8(handle, KEY_TRANSPORT_MODE, &u8_val) == ESP_OK) {
        config->transport_mode = (signalk_transport_mode_t)u8_val;
    } else {
        config->transport_mode = SIGNALK_TRANSPORT_BOTH;
    }

    // udp_target_ip
    len = sizeof(config->udp_target_ip);
    if (nvs_get_str(handle, KEY_UDP_IP, config->udp_target_ip, &len) != ESP_OK) {
        strcpy(config->udp_target_ip, "255.255.255.255");
    }

    // udp_broadcast_port
    if (nvs_get_u16(handle, KEY_UDP_BCAST, &u16_val) == ESP_OK) {
        config->udp_broadcast_port = u16_val;
    } else {
        config->udp_broadcast_port = 5555;
    }

    // udp_listen_port
    if (nvs_get_u16(handle, KEY_UDP_LISTEN, &u16_val) == ESP_OK) {
        config->udp_listen_port = u16_val;
    } else {
        config->udp_listen_port = 5556;
    }

    // token
    len = sizeof(config->token);
    if (nvs_get_str(handle, KEY_TOKEN, config->token, &len) != ESP_OK) {
        config->token[0] = '\0';
    }

    // vessel_name
    len = sizeof(config->vessel_name);
    if (nvs_get_str(handle, KEY_VESSEL_NAME, config->vessel_name, &len) != ESP_OK) {
        strcpy(config->vessel_name, "SailorGuard");
    }

    // client_id
    len = sizeof(config->client_id);
    if (nvs_get_str(handle, KEY_CLIENT_ID, config->client_id, &len) != ESP_OK) {
        strcpy(config->client_id, "sailorguard-device");
    }

    nvs_close(handle);
    ESP_LOGI(TAG, "Configuration loaded from NVS");
    return ESP_OK;
}

esp_err_t signalk_storage_save_config(const signalk_config_t *config) {
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(SIGNALK_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return err;
    }

    // Save all values
    nvs_set_u8(handle, KEY_ENABLED, (uint8_t)config->enabled);
    nvs_set_u8(handle, KEY_AUTO_DISCOVERY, (uint8_t)config->auto_discovery);
    nvs_set_str(handle, KEY_HOSTNAME, config->hostname);
    nvs_set_u16(handle, KEY_PORT, config->port);
    nvs_set_u8(handle, KEY_USE_SSL, (uint8_t)config->use_ssl);
    nvs_set_u8(handle, KEY_TRANSPORT_MODE, (uint8_t)config->transport_mode);
    nvs_set_str(handle, KEY_UDP_IP, config->udp_target_ip);
    nvs_set_u16(handle, KEY_UDP_BCAST, config->udp_broadcast_port);
    nvs_set_u16(handle, KEY_UDP_LISTEN, config->udp_listen_port);
    nvs_set_str(handle, KEY_TOKEN, config->token);
    nvs_set_str(handle, KEY_VESSEL_NAME, config->vessel_name);
    nvs_set_str(handle, KEY_CLIENT_ID, config->client_id);

    err = nvs_commit(handle);
    nvs_close(handle);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Configuration saved to NVS");
    } else {
        ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(err));
    }

    return err;
}

esp_err_t signalk_storage_clear_config(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(SIGNALK_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_erase_all(handle);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    ESP_LOGI(TAG, "Configuration cleared from NVS");
    return err;
}
