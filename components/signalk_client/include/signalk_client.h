#pragma once

#include "signalk_types.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize SignalK client system
 * @return ESP_OK on success
 */
esp_err_t signalk_client_init(void);

/**
 * @brief Start SignalK client (connect if enabled)
 * @return ESP_OK on success
 */
esp_err_t signalk_client_start(void);

/**
 * @brief Stop SignalK client (disconnect)
 * @return ESP_OK on success
 */
esp_err_t signalk_client_stop(void);

/**
 * @brief Get current configuration
 * @param config Pointer to store configuration
 * @return ESP_OK on success
 */
esp_err_t signalk_get_config(signalk_config_t *config);

/**
 * @brief Set configuration
 * @param config New configuration
 * @return ESP_OK on success
 */
esp_err_t signalk_set_config(const signalk_config_t *config);

/**
 * @brief Get current connection status
 * @param status Pointer to store status
 * @return ESP_OK on success
 */
esp_err_t signalk_get_status(signalk_status_t *status);

/**
 * @brief Send data to SignalK server
 * @param data Data to send
 * @return ESP_OK on success
 */
esp_err_t signalk_send_data(const signalk_data_t *data);

/**
 * @brief Connect to SignalK server manually
 * @param hostname Server hostname or IP
 * @param port Server port
 * @return ESP_OK on success
 */
esp_err_t signalk_connect(const char *hostname, uint16_t port);

/**
 * @brief Disconnect from SignalK server
 * @return ESP_OK on success
 */
esp_err_t signalk_disconnect(void);

/**
 * @brief Test connection to SignalK server
 * @param hostname Server hostname or IP
 * @param port Server port
 * @return ESP_OK if server responds
 */
esp_err_t signalk_test_connection(const char *hostname, uint16_t port);

/**
 * @brief Subscribe to a SignalK data path
 * @param path SignalK path (e.g., "navigation.position")
 * @param period_ms Desired update period in ms (0 = server default)
 * @return ESP_OK on success, ESP_ERR_NO_MEM if slots full
 */
esp_err_t signalk_subscribe(const char *path, uint32_t period_ms);

/**
 * @brief Unsubscribe from a SignalK data path
 * @param path The path to unsubscribe from
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if not subscribed
 */
esp_err_t signalk_unsubscribe(const char *path);

/**
 * @brief Register a callback for incoming delta data.
 *        Callbacks fire from the WS event context - keep them fast.
 * @param path_filter If non-NULL, only deltas with this path prefix trigger the callback. NULL = all.
 * @param callback Function to call
 * @param user_ctx Opaque pointer passed to callback
 * @return ESP_OK on success, ESP_ERR_NO_MEM if slots full
 */
esp_err_t signalk_register_callback(const char *path_filter,
                                     signalk_data_callback_t callback,
                                     void *user_ctx);

/**
 * @brief Get the latest cached value for a path
 * @param path The SignalK path to look up
 * @param data Output: populated with latest value if found
 * @return ESP_OK if found, ESP_ERR_NOT_FOUND if no cached data
 */
esp_err_t signalk_get_cached_value(const char *path, signalk_data_t *data);

/**
 * @brief Get list of active subscriptions
 * @param subs Output array
 * @param max Maximum entries to return
 * @param count Output: number of entries written
 * @return ESP_OK on success
 */
esp_err_t signalk_get_subscriptions(signalk_subscription_t *subs, size_t max, size_t *count);

#ifdef __cplusplus
}
#endif
