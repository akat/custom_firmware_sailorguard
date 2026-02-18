#include <stdio.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"

#include "status_api.h"
#include "wifi_api.h"
#include "config_api.h"

static const char *TAG = "http_server";

typedef struct {
    const char *base_path;
} static_ctx_t;

static const char *content_type_from_path(const char *path) {
    const char *ext = strrchr(path, '.');
    if (!ext) {
        return "text/plain";
    }
    if (strcmp(ext, ".html") == 0) {
        return "text/html";
    }
    if (strcmp(ext, ".css") == 0) {
        return "text/css";
    }
    if (strcmp(ext, ".js") == 0) {
        return "application/javascript";
    }
    if (strcmp(ext, ".json") == 0) {
        return "application/json";
    }
    if (strcmp(ext, ".svg") == 0) {
        return "image/svg+xml";
    }
    if (strcmp(ext, ".png") == 0) {
        return "image/png";
    }
    return "application/octet-stream";
}

static esp_err_t static_file_handler(httpd_req_t *req) {
    static_ctx_t *ctx = (static_ctx_t *)req->user_ctx;
    const char *base_path = ctx ? ctx->base_path : "/spiffs";

    if (strstr(req->uri, "..")) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid path");
    }

    char filepath[256];
    const char *uri = req->uri;
    if (strcmp(uri, "/") == 0) {
        uri = "/index.html";
    }

    int needed = snprintf(filepath, sizeof(filepath), "%s%s", base_path, uri);
    if (needed < 0 || needed >= (int)sizeof(filepath)) {
        return httpd_resp_send_err(req, HTTPD_414_URI_TOO_LONG, "path too long");
    }

    ESP_LOGI(TAG, "GET %s -> %s", req->uri, filepath);

    FILE *file = fopen(filepath, "rb");
    if (!file) {
        ESP_LOGW(TAG, "File not found: %s", filepath);
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "not found");
    }

    httpd_resp_set_type(req, content_type_from_path(filepath));
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");

    char buffer[1024];
    size_t read_bytes;
    while ((read_bytes = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        if (httpd_resp_send_chunk(req, buffer, read_bytes) != ESP_OK) {
            fclose(file);
            httpd_resp_sendstr_chunk(req, NULL);
            return ESP_FAIL;
        }
    }

    fclose(file);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

httpd_handle_t start_http_server(const char *base_path, esp_netif_t *ap_netif) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 256;  // Increased to accommodate all API handlers + static
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.stack_size = 6144;
    config.max_open_sockets = 3;    // Reduced from default 7 to avoid socket exhaustion

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "HTTP server start failed");
        return NULL;
    }

    status_api_register(server, ap_netif);
    wifi_api_register(server);
    config_api_register(server);

    return server;
}

void http_server_register_static(httpd_handle_t server, const char *base_path) {
    static static_ctx_t ctx = {0};
    ctx.base_path = base_path;

    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = static_file_handler,
        .user_ctx = &ctx,
    };

    httpd_uri_t index_html_uri = {
        .uri = "/index.html",
        .method = HTTP_GET,
        .handler = static_file_handler,
        .user_ctx = &ctx,
    };

    httpd_uri_t assets_uri = {
        .uri = "/assets/*",
        .method = HTTP_GET,
        .handler = static_file_handler,
        .user_ctx = &ctx,
    };

    httpd_uri_t config_uri = {
        .uri = "/config.json",
        .method = HTTP_GET,
        .handler = static_file_handler,
        .user_ctx = &ctx,
    };

    if (httpd_register_uri_handler(server, &index_uri) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register / handler");
    }
    if (httpd_register_uri_handler(server, &index_html_uri) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register /index.html handler");
    }
    if (httpd_register_uri_handler(server, &assets_uri) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register /assets/* handler");
    }
    if (httpd_register_uri_handler(server, &config_uri) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register /config.json handler");
    }
}
