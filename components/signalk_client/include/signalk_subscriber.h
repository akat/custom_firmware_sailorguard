#pragma once

#include "signalk_types.h"
#include "esp_err.h"
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Subscribe callback for Signal K data updates
 * @param path Signal K path that changed
 * @param data New data value
 * @param user_data User data passed during subscription
 */
typedef void (*signalk_subscribe_callback_t)(const char *path, 
                                             const signalk_data_t *data, 
                                             void *user_data);

/**
 * @brief Subscribe to a Signal K path (WebSocket real-time updates)
 * @param path Signal K path to monitor (e.g., "environment.outside.temperature")
 * @param callback Function to call when value changes
 * @param poll_interval_ms Update interval in milliseconds (for server rate limiting)
 * @param user_data Optional user data passed to callback
 * @return ESP_OK on success
 * 
 * Note: Uses WebSocket if available for real-time deltas. When WebSocket delta
 * arrives, the callback is invoked with the new value.
 */
esp_err_t signalk_subscribe(const char *path, 
                           signalk_subscribe_callback_t callback,
                           uint32_t poll_interval_ms,
                           void *user_data);

/**
 * @brief Unsubscribe from a Signal K path
 * @param path Signal K path to stop monitoring
 * @return ESP_OK on success
 */
esp_err_t signalk_unsubscribe(const char *path);

/**
 * @brief Initialize subscription system (called automatically)
 */
esp_err_t signalk_subscriber_init(void);

/**
 * @brief Handle incoming delta message (internal, called by WebSocket handler)
 * @param delta cJSON object containing Signal K delta message
 */
void signalk_subscriber_handle_delta(cJSON *delta);

/**
 * @brief Read a Signal K value via HTTP GET
 * @param path Signal K path to read
 * @param data Pointer to store the value
 * @return ESP_OK on success
 */
esp_err_t signalk_read_value(const char *path, signalk_data_t *data);

/**
 * @brief Get raw JSON from Signal K API
 * @param path Signal K path (e.g., "navigation/position")
 * @param json_out Buffer to store JSON response
 * @param max_len Maximum buffer size
 * @return ESP_OK on success
 */
esp_err_t signalk_get_json(const char *path, char *json_out, size_t max_len);

#ifdef __cplusplus
}
#endif
