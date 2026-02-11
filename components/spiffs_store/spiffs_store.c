#include "esp_err.h"
#include "esp_log.h"
#include "esp_spiffs.h"

static const char *TAG = "spiffs";

void spiffs_init(const char *base_path, const char *partition_label) {
    esp_vfs_spiffs_conf_t conf = {
        .base_path = base_path,
        .partition_label = partition_label,
        .max_files = 8,
        .format_if_mount_failed = true,
    };

    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(err));
        return;
    }

    size_t total = 0;
    size_t used = 0;
    if (esp_spiffs_info(partition_label, &total, &used) == ESP_OK) {
        ESP_LOGI(TAG, "SPIFFS: %u/%u bytes", (unsigned)used, (unsigned)total);
    }
}
