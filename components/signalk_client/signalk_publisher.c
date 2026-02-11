#include "signalk_publisher.h"
#include "signalk_client.h"
#include "esp_log.h"
#include <string.h>

esp_err_t signalk_publish_bool(const char *path, bool value, const char *source_label) {
    if (!path) {
        return ESP_ERR_INVALID_ARG;
    }

    signalk_data_t data = {
        .type = SIGNALK_VALUE_BOOL,
        .value.b = value
    };
    
    strncpy(data.path, path, sizeof(data.path) - 1);
    if (source_label) {
        strncpy(data.source_label, source_label, sizeof(data.source_label) - 1);
    } else {
        data.source_label[0] = '\0';
    }

    return signalk_send_data(&data);
}

esp_err_t signalk_publish_int(const char *path, int32_t value, const char *source_label) {
    if (!path) {
        return ESP_ERR_INVALID_ARG;
    }

    signalk_data_t data = {
        .type = SIGNALK_VALUE_INT,
        .value.i = value
    };
    
    strncpy(data.path, path, sizeof(data.path) - 1);
    if (source_label) {
        strncpy(data.source_label, source_label, sizeof(data.source_label) - 1);
    } else {
        data.source_label[0] = '\0';
    }

    return signalk_send_data(&data);
}

esp_err_t signalk_publish_float(const char *path, float value, const char *source_label) {
    if (!path) {
        return ESP_ERR_INVALID_ARG;
    }

    signalk_data_t data = {
        .type = SIGNALK_VALUE_FLOAT,
        .value.f = value
    };
    
    strncpy(data.path, path, sizeof(data.path) - 1);
    if (source_label) {
        strncpy(data.source_label, source_label, sizeof(data.source_label) - 1);
    } else {
        data.source_label[0] = '\0';
    }

    return signalk_send_data(&data);
}

esp_err_t signalk_publish_string(const char *path, const char *value, const char *source_label) {
    if (!path || !value) {
        return ESP_ERR_INVALID_ARG;
    }

    signalk_data_t data = {
        .type = SIGNALK_VALUE_STRING
    };
    
    strncpy(data.path, path, sizeof(data.path) - 1);
    strncpy(data.value.s, value, sizeof(data.value.s) - 1);
    
    if (source_label) {
        strncpy(data.source_label, source_label, sizeof(data.source_label) - 1);
    } else {
        data.source_label[0] = '\0';
    }

    return signalk_send_data(&data);
}

esp_err_t signalk_publish_position(double latitude, double longitude, double altitude, const char *source_label) {
    signalk_data_t data = {
        .type = SIGNALK_VALUE_POSITION,
        .value.pos = {
            .latitude = latitude,
            .longitude = longitude,
            .altitude = altitude
        }
    };
    
    strncpy(data.path, "navigation.position", sizeof(data.path) - 1);
    
    if (source_label) {
        strncpy(data.source_label, source_label, sizeof(data.source_label) - 1);
    } else {
        data.source_label[0] = '\0';
    }

    return signalk_send_data(&data);
}

esp_err_t signalk_publish_temperature(const char *path, float celsius, const char *source_label) {
    // Signal K uses Kelvin for temperature
    float kelvin = celsius + 273.15f;
    return signalk_publish_float(path, kelvin, source_label);
}

esp_err_t signalk_publish_voltage(const char *path, float volts, const char *source_label) {
    return signalk_publish_float(path, volts, source_label);
}

esp_err_t signalk_publish_speed(const char *path, float meters_per_second, const char *source_label) {
    return signalk_publish_float(path, meters_per_second, source_label);
}
