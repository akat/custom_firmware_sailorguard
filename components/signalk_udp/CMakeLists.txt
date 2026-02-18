idf_component_register(
    SRCS "signalk_udp.c"
    INCLUDE_DIRS "include"
    REQUIRES json
             signalk_types
    PRIV_REQUIRES freertos lwip esp_system
)
