#pragma once

#include "esp_http_server.h"
#include "esp_err.h"
#include <stdbool.h>

// Register config API endpoints with HTTP server
void config_api_register(httpd_handle_t server);

// Helper functions to read config values from other components
// Returns ESP_OK if value found, ESP_ERR_NOT_FOUND otherwise

/**
 * @brief Get an integer config value (for "number" or "gpio" type fields)
 * @param key Configuration key (e.g., "anchor_gpio", "report_interval")
 * @param out_value Pointer to store the retrieved value
 * @return ESP_OK if found, ESP_ERR_NOT_FOUND otherwise
 */
esp_err_t config_get_int(const char *key, int *out_value);

/**
 * @brief Get a boolean config value (for "bool" type fields)
 * @param key Configuration key (e.g., "buzzer_enabled")
 * @param out_value Pointer to store the retrieved value
 * @return ESP_OK if found, ESP_ERR_NOT_FOUND otherwise
 */
esp_err_t config_get_bool(const char *key, bool *out_value);

/**
 * @brief Get a string config value (for "text" or "select" type fields)
 * @param key Configuration key (e.g., "device_label", "device_mode")
 * @param out_value Buffer to store the retrieved value
 * @param max_len Maximum length of the output buffer
 * @return ESP_OK if found, ESP_ERR_NOT_FOUND otherwise
 */
esp_err_t config_get_string(const char *key, char *out_value, size_t max_len);

/**
 * @brief Get an integer config value with fallback to default
 * @param key Configuration key
 * @param default_value Default value if key not found
 * @return The stored value or default_value if not found
 */
int config_get_int_or_default(const char *key, int default_value);

/**
 * @brief Get a boolean config value with fallback to default
 * @param key Configuration key
 * @param default_value Default value if key not found
 * @return The stored value or default_value if not found
 */
bool config_get_bool_or_default(const char *key, bool default_value);
