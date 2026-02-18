idf_component_register(SRCS "device_api.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_http_server esp_https_ota esp_http_client nvs_flash app_update json esp_timer mbedtls)
