idf_component_register(
    SRCS "anchor_guard.c"
    INCLUDE_DIRS "include"
    REQUIRES config_api signalk_client nvs_flash
    PRIV_REQUIRES freertos driver
)
