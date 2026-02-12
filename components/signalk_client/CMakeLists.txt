idf_component_register(
    SRCS "signalk_client.c" 
         "signalk_storage.c"
         "signalk_mdns.c"
         "signalk_auth.c"
         "signalk_publisher.c"
         "signalk_subscriber.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash
             esp_http_client
             config_api
    PRIV_REQUIRES esp_event
                  freertos
)
