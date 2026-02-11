#include "signalk_auth.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "signalk_auth";

// Stub implementations - to be implemented in Phase B

esp_err_t signalk_auth_request_token(const char *hostname,
                                      uint16_t port,
                                      bool use_ssl,
                                      const char *client_id,
                                      const char *description,
                                      signalk_auth_request_t *request) {
    if (!hostname || !client_id || !request) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Requesting token from %s:%d (stub - Phase B)", hostname, port);

    // TODO: Implement token request flow
    // POST /signalk/v1/access/requests
    // {
    //   "clientId": client_id,
    //   "description": description
    // }
    // Returns requestId and requires user approval

    memset(request, 0, sizeof(signalk_auth_request_t));
    request->state = AUTH_REQUEST_PENDING;

    return ESP_OK;
}

esp_err_t signalk_auth_check_request(const char *hostname,
                                      uint16_t port,
                                      bool use_ssl,
                                      const char *request_id,
                                      signalk_auth_request_t *request) {
    if (!hostname || !request_id || !request) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Checking auth request %s (stub - Phase B)", request_id);

    // TODO: Poll GET /signalk/v1/access/requests/{request_id}
    // to check if user has approved the request

    memset(request, 0, sizeof(signalk_auth_request_t));
    strcpy(request->request_id, request_id);
    request->state = AUTH_REQUEST_PENDING;

    return ESP_OK;
}

esp_err_t signalk_auth_validate_token(const char *hostname,
                                       uint16_t port,
                                       bool use_ssl,
                                       const char *token) {
    if (!hostname || !token) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Validating token with %s:%d (stub - Phase B)", hostname, port);

    // TODO: Send WebSocket connection with token
    // or HTTP request with Authorization header

    return ESP_OK;
}
