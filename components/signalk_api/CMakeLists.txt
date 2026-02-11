idf_component_register(
    SRCS "signalk_api.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server 
             signalk_client
)
