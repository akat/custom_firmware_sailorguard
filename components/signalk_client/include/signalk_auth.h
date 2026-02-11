#pragma once

#include "signalk_types.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Request access token from SignalK server
 * @param hostname Server hostname or IP
 * @param port Server port
 * @param use_ssl Whether to use HTTPS
 * @param client_id Client identifier
 * @param description Description shown to user
 * @param request Pointer to store request details
 * @return ESP_OK on success
 */
esp_err_t signalk_auth_request_token(const char *hostname,
                                      uint16_t port,
                                      bool use_ssl,
                                      const char *client_id,
                                      const char *description,
                                      signalk_auth_request_t *request);

/**
 * @brief Check status of pending auth request
 * @param hostname Server hostname or IP
 * @param port Server port
 * @param use_ssl Whether to use HTTPS
 * @param request_id Request ID from previous request
 * @param request Pointer to store updated request status
 * @return ESP_OK on success
 */
esp_err_t signalk_auth_check_request(const char *hostname,
                                      uint16_t port,
                                      bool use_ssl,
                                      const char *request_id,
                                      signalk_auth_request_t *request);

/**
 * @brief Clear stored authentication token
 * @return ESP_OK on success
 */
esp_err_t signalk_auth_clear_token(void);

/**
 * @brief Validate an access token
 * @param hostname Server hostname or IP
 * @param port Server port
 * @param use_ssl Whether to use HTTPS
 * @param token Token to validate
 * @return ESP_OK if token is valid
 */
esp_err_t signalk_auth_validate_token(const char *hostname,
                                       uint16_t port,
                                       bool use_ssl,
                                       const char *token);

#ifdef __cplusplus
}
#endif
