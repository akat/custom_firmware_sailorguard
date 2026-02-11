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

#ifdef __cplusplus
}
#endif
