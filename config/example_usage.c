/**
 * Example Component: Using Device Configuration Values
 * 
 * This example demonstrates how to read configuration values
 * from the config_api in your own firmware components.
 * 
 * To use this in your own code:
 * 1. Include "config_api.h"
 * 2. Add config_api to your component's REQUIRES in CMakeLists.txt
 * 3. Call config_get_* functions to retrieve values
 */

#include "esp_log.h"
#include "config_api.h"
#include <string.h>

static const char *TAG = "example_config_usage";

void example_init(void) {
    ESP_LOGI(TAG, "=== Configuration Usage Example ===");
    
    // ===================================================================
    // Example 1: Reading GPIO configuration with default fallback
    // ===================================================================
    int anchor_gpio = config_get_int_or_default("anchor_gpio", 4);
    ESP_LOGI(TAG, "Anchor GPIO configured to: %d", anchor_gpio);
    
    // You can now use this GPIO in your code
    // gpio_set_direction(anchor_gpio, GPIO_MODE_OUTPUT);
    
    // ===================================================================
    // Example 2: Reading boolean configuration
    // ===================================================================
    bool buzzer_enabled = config_get_bool_or_default("buzzer_enabled", true);
    ESP_LOGI(TAG, "Buzzer is %s", buzzer_enabled ? "ENABLED" : "DISABLED");
    
    if (buzzer_enabled) {
        // Initialize buzzer
        ESP_LOGI(TAG, "Initializing buzzer...");
    }
    
    // ===================================================================
    // Example 3: Reading numeric values with validation
    // ===================================================================
    int report_interval = config_get_int_or_default("report_interval", 30);
    ESP_LOGI(TAG, "Report interval: %d seconds", report_interval);
    
    // Use this interval for your reporting logic
    // xTaskCreate(report_task, "report", 2048, &report_interval, 5, NULL);
    
    // ===================================================================
    // Example 4: Reading string/select values
    // ===================================================================
    char device_mode[32] = {0};
    if (config_get_string("device_mode", device_mode, sizeof(device_mode)) == ESP_OK) {
        ESP_LOGI(TAG, "Device mode: %s", device_mode);
        
        // Use the mode string to control behavior
        if (strcmp(device_mode, "auto") == 0) {
            ESP_LOGI(TAG, "Running in AUTO mode");
        } else if (strcmp(device_mode, "manual") == 0) {
            ESP_LOGI(TAG, "Running in MANUAL mode");
        }
    } else {
        ESP_LOGI(TAG, "Device mode not configured, using default");
    }
    
    // ===================================================================
    // Example 5: Reading text field (device label)
    // ===================================================================
    char device_label[64] = {0};
    if (config_get_string("device_label", device_label, sizeof(device_label)) == ESP_OK) {
        ESP_LOGI(TAG, "Device label: %s", device_label);
    } else {
        strcpy(device_label, "SailorGuard");
        ESP_LOGI(TAG, "Using default label: %s", device_label);
    }
    
    // ===================================================================
    // Example 6: Checking if a value exists before using
    // ===================================================================
    int custom_value;
    if (config_get_int("custom_setting", &custom_value) == ESP_OK) {
        ESP_LOGI(TAG, "Custom setting found: %d", custom_value);
        // Use the custom value
    } else {
        ESP_LOGW(TAG, "Custom setting not configured");
        // Handle missing value case
        custom_value = 100; // Use hardcoded default
    }
    
    // ===================================================================
    // Example 7: Dynamic configuration reload
    // ===================================================================
    /* 
     * Configuration values are stored in NVS and can be updated at runtime
     * through the web UI (/api/config/values POST endpoint).
     * 
     * To react to configuration changes:
     * - Implement a configuration change callback/event
     * - Periodically re-read values if needed
     * - OR require a device restart after config changes
     */
    
    ESP_LOGI(TAG, "=== End of Configuration Examples ===");
}

/**
 * To add config_api to your component's dependencies:
 * 
 * In your component's CMakeLists.txt, add:
 * 
 * idf_component_register(
 *     SRCS "your_component.c"
 *     INCLUDE_DIRS "include"
 *     REQUIRES config_api
 * )
 */
