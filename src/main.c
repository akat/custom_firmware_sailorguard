#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "http_server.h"
#include "spiffs_store.h"
#include "wifi_manager.h"
#include "signalk_client.h"
#include "signalk_api.h"
#include "anchor_guard.h"

static const char *TAG = "app";

#define TWDT_TIMEOUT_S 15

static const char *reset_reason_str(esp_reset_reason_t reason) {
	switch (reason) {
		case ESP_RST_POWERON:   return "POWER_ON";
		case ESP_RST_EXT:       return "EXTERNAL";
		case ESP_RST_SW:        return "SOFTWARE";
		case ESP_RST_PANIC:     return "PANIC";
		case ESP_RST_INT_WDT:   return "INT_WATCHDOG";
		case ESP_RST_TASK_WDT:  return "TASK_WATCHDOG";
		case ESP_RST_WDT:       return "OTHER_WATCHDOG";
		case ESP_RST_DEEPSLEEP: return "DEEP_SLEEP";
		case ESP_RST_BROWNOUT:  return "BROWNOUT";
		case ESP_RST_SDIO:      return "SDIO";
		default:                return "UNKNOWN";
	}
}

void app_main(void) {
	// Log restart reason for field debugging
	esp_reset_reason_t reason = esp_reset_reason();
	ESP_LOGW(TAG, "=== Boot reason: %s (%d) ===", reset_reason_str(reason), reason);

	esp_err_t err = nvs_flash_init();
	if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		err = nvs_flash_init();
	}
	ESP_ERROR_CHECK(err);

	// Initialize task watchdog (auto-restart on hung tasks)
	esp_task_wdt_config_t twdt_config = {
		.timeout_ms = TWDT_TIMEOUT_S * 1000,
		.idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
		.trigger_panic = true,
	};
	ESP_ERROR_CHECK(esp_task_wdt_reconfigure(&twdt_config));
	ESP_LOGI(TAG, "Task watchdog configured: %ds timeout, panic on trigger", TWDT_TIMEOUT_S);

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

	// Initialize anchor guard controller
	anchor_guard_init();

	// Register static file handler AFTER all API handlers
	http_server_register_static(server, "/spiffs");

	ESP_LOGI(TAG, "Open http://192.168.4.1/ after connecting to ESP32-DASH");
}
