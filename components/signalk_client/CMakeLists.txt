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

# Try to link WebSocket client if available (optional)
if(COMPONENT_FOUND)
    idf_component_get_property(websocket_lib esp_websocket_client COMPONENT_LIB QUIET)
    if(websocket_lib)
        target_link_libraries(${COMPONENT_LIB} PUBLIC ${websocket_lib})
    endif()
endif()
