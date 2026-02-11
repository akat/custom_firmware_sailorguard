idf_component_register(SRCS "status_api.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_http_server esp_netif esp_app_format esp_timer)
