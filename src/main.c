#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "http_server.h"
#include "spiffs_store.h"
#include "wifi_manager.h"
#include "signalk_client.h"
#include "signalk_api.h"

static const char *TAG = "app";

// Callback for incoming SignalK delta data
static void signalk_data_cb(const signalk_data_t *data, void *user_ctx) {
	(void)user_ctx;
	switch (data->type) {
		case SIGNALK_VALUE_FLOAT:
			ESP_LOGI(TAG, "[SK] %s = %.4f (src: %s)", data->path, data->value.f, data->source_label);
			break;
		case SIGNALK_VALUE_POSITION:
			ESP_LOGI(TAG, "[SK] %s = lat:%.6f lon:%.6f (src: %s)",
			         data->path, data->value.pos.latitude, data->value.pos.longitude, data->source_label);
			break;
		case SIGNALK_VALUE_BOOL:
			ESP_LOGI(TAG, "[SK] %s = %s (src: %s)", data->path, data->value.b ? "true" : "false", data->source_label);
			break;
		case SIGNALK_VALUE_INT:
			ESP_LOGI(TAG, "[SK] %s = %ld (src: %s)", data->path, (long)data->value.i, data->source_label);
			break;
		case SIGNALK_VALUE_STRING:
			ESP_LOGI(TAG, "[SK] %s = \"%s\" (src: %s)", data->path, data->value.s, data->source_label);
			break;
		default:
			ESP_LOGI(TAG, "[SK] %s = (null) (src: %s)", data->path, data->source_label);
			break;
	}
}

// Task that sends a test value to navigation.anchor.akat every 2 seconds
static void signalk_test_sender_task(void *pvParameters) {
	(void)pvParameters;
	float test_value = 0.0f;

	while (1) {
		vTaskDelay(pdMS_TO_TICKS(2000));

		signalk_data_t data = {0};
		strncpy(data.path, "navigation.anchor.akat", sizeof(data.path) - 1);
		data.type = SIGNALK_VALUE_FLOAT;
		data.value.f = test_value;
		strncpy(data.source_label, "sailorguard", sizeof(data.source_label) - 1);

		esp_err_t err = signalk_send_data(&data);
		if (err == ESP_OK) {
			ESP_LOGI(TAG, "[SK-TX] Sent navigation.anchor.akat = %.1f", test_value);
		}

		test_value += 1.0f;
		if (test_value > 100.0f) {
			test_value = 0.0f;
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

	wifi_manager_init("ESP32-DASH", "esp32pass", 15000);
	wifi_manager_start();
	spiffs_init("/spiffs", "spiffs");

	// Get HTTP server handle
	httpd_handle_t server = start_http_server("/spiffs", wifi_manager_get_ap_netif());

	// Initialize and register SignalK components (before static file handler)
	signalk_client_init();
	signalk_api_register(server);
	signalk_client_start();

	// Register callback for all incoming delta data
	signalk_register_callback(NULL, signalk_data_cb, NULL);

	// Subscribe to SignalK paths
	signalk_subscribe("navigation.anchor.akat", 2000);
	signalk_subscribe("navigation.position", 1000);
	signalk_subscribe("environment.depth.belowTransducer", 1000);
	signalk_subscribe("environment.wind.angleApparent", 1000);
	signalk_subscribe("environment.wind.speedApparent", 1000);

	// Start test sender task (sends navigation.anchor.akat every 2s)
	xTaskCreate(signalk_test_sender_task, "sk_test_tx", 4096, NULL, 3, NULL);

	// Register static file handler AFTER all API handlers
	http_server_register_static(server, "/spiffs");

	ESP_LOGI(TAG, "Open http://192.168.4.1/ after connecting to ESP32-DASH");
}