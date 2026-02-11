#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"

#include "http_server.h"
#include "spiffs_store.h"
#include "wifi_manager.h"

static const char *TAG = "app";

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
	start_http_server("/spiffs", wifi_manager_get_ap_netif());

	ESP_LOGI(TAG, "Open http://192.168.4.1/ after connecting to ESP32-DASH");
}