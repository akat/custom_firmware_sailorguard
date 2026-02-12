#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize anchor guard controller, load config, and start task.
 */
esp_err_t anchor_guard_init(void);

#ifdef __cplusplus
}
#endif
