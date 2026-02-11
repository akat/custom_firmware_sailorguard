idf_component_register(
    SRCS "config_api.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server nvs_flash cjson
)
