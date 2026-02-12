#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "http_server.h"
#include "spiffs_store.h"
#include "wifi_manager.h"
#include "signalk_client.h"
#include "signalk_api.h"
#include "signalk_subscriber.h"

static const char *TAG = "app";

// WebSocket subscription callback for navigation position
static void on_position_update(const char *path, 
                               const signalk_data_t *data, 
                               void *user_data) {
	if (data->type == SIGNALK_VALUE_POSITION) {
		ESP_LOGI(TAG, "📍 Position Update: %.6f, %.6f (alt: %.1f m)",
			data->value.pos.latitude,
			data->value.pos.longitude,
			data->value.pos.altitude);
	}
}

/**
 * WiFi event handler to initialize subscriptions after WiFi connects
 */
static void wifi_connect_handler(void *handler_args, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data) {
	if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
		// AP mode: a client just connected to our AP
		ESP_LOGI(TAG, "WiFi: Client connected to AP, initializing Signal K subscriptions");
		
		// Start polling task (only when WebSocket unavailable)
		signalk_start_polling_task();
		
		// Subscribe to navigation position updates
		ESP_LOGI(TAG, "Subscribing to navigation.position...");
		esp_err_t err = signalk_subscribe("navigation.position", 
		                                   on_position_update, 
		                                   5000,  // 5 second update period
		                                   NULL);
		if (err != ESP_OK) {
			ESP_LOGW(TAG, "Failed to subscribe: %s", esp_err_to_name(err));
		}
	}
}

void app_main(void) {
	esp_err_t err = nvs_flash_init();
	if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		err = nvs_flash_init();
	}
	ESP_ERROR_CHECK(err);

	ESP_ERROR_CHECK(esp_netif_init());
	ESP_ERROR_CHECK(esp_event_loop_create_default());

	// Register WiFi disconnect event handler (before starting WiFi)
	ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_AP_STACONNECTED,
	                                           wifi_connect_handler, NULL));

	wifi_manager_init("ESP32-DASH", "esp32pass", 15000);
	wifi_manager_start();
	spiffs_init("/spiffs", "spiffs");
	
	// Get HTTP server handle
	httpd_handle_t server = start_http_server("/spiffs", wifi_manager_get_ap_netif());
	
	// Initialize SignalK components BEFORE wifi connection
	// Subscriptions will be registered via WiFi event handler
	signalk_client_init();
	signalk_api_register(server);
	signalk_client_start();
	
	// Note: signalk_subscribe() is called from wifi_connect_handler()
	// after WiFi actually connects, so HTTP polling doesn't fail
	
	// Register static file handler AFTER all API handlers
	http_server_register_static(server, "/spiffs");

	ESP_LOGI(TAG, "Open http://192.168.4.1/ after connecting to ESP32-DASH");
}