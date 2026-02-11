#pragma once

#include "signalk_types.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Publish boolean value to Signal K
 * @param path Signal K path (e.g., "electrical.switches.cabin.state")
 * @param value Boolean value
 * @param source_label Optional source label
 * @return ESP_OK on success
 */
esp_err_t signalk_publish_bool(const char *path, bool value, const char *source_label);

/**
 * @brief Publish integer value to Signal K
 * @param path Signal K path (e.g., "electrical.batteries.house.voltage")
 * @param value Integer value
 * @param source_label Optional source label
 * @return ESP_OK on success
 */
esp_err_t signalk_publish_int(const char *path, int32_t value, const char *source_label);

/**
 * @brief Publish float value to Signal K
 * @param path Signal K path (e.g., "environment.outside.temperature")
 * @param value Float value
 * @param source_label Optional source label
 * @return ESP_OK on success
 */
esp_err_t signalk_publish_float(const char *path, float value, const char *source_label);

/**
 * @brief Publish string value to Signal K
 * @param path Signal K path (e.g., "navigation.state")
 * @param value String value
 * @param source_label Optional source label
 * @return ESP_OK on success
 */
esp_err_t signalk_publish_string(const char *path, const char *value, const char *source_label);

/**
 * @brief Publish position to Signal K
 * @param latitude Latitude in degrees
 * @param longitude Longitude in degrees
 * @param altitude Altitude in meters (0 if not available)
 * @param source_label Optional source label
 * @return ESP_OK on success
 */
esp_err_t signalk_publish_position(double latitude, double longitude, double altitude, const char *source_label);

/**
 * @brief Publish temperature in Kelvin to Signal K
 * @param path Signal K path (e.g., "environment.outside.temperature")
 * @param celsius Temperature in Celsius
 * @param source_label Optional source label
 * @return ESP_OK on success
 */
esp_err_t signalk_publish_temperature(const char *path, float celsius, const char *source_label);

/**
 * @brief Publish voltage to Signal K
 * @param path Signal K path (e.g., "electrical.batteries.house.voltage")
 * @param volts Voltage in volts
 * @param source_label Optional source label
 * @return ESP_OK on success
 */
esp_err_t signalk_publish_voltage(const char *path, float volts, const char *source_label);

/**
 * @brief Publish speed to Signal K
 * @param path Signal K path (e.g., "navigation.speedThroughWater")
 * @param meters_per_second Speed in m/s
 * @param source_label Optional source label
 * @return ESP_OK on success
 */
esp_err_t signalk_publish_speed(const char *path, float meters_per_second, const char *source_label);

#ifdef __cplusplus
}
#endif
