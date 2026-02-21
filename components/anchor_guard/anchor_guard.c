#include "anchor_guard.h"
#include "config_api.h"
#include "signalk_client.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <math.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <time.h>

#define SAFETY_DISCONNECT_DEBOUNCE_MS 3000

/* Fixed hardware pin assignments — not user-configurable. */
#define MOTOR_EN_PIN    26   /* IO26: motor power (active HIGH)              */
#define MOTOR_DIR_PIN   27   /* IO27: direction — HIGH = UP, LOW = DOWN      */
#define EXT_UP_PIN      16   /* IO16: external UP sense (active LOW)         */
#define EXT_DOWN_PIN    17   /* IO17: external DOWN sense (active LOW)       */
#define STATUS_LED_PIN   2   /* IO2:  onboard status LED (active HIGH)       */

static const char *TAG = "anchor_guard";
static const char *ANCHOR_NVS_NS = "anchor_guard";

static const char *SK_CMD_PATH = "sensors.akat.anchor.command";
static const char *SK_CHAIN_PATH = "sensors.akat.anchor.chainOut";
static const char *SK_RESET_PATH = "sensors.akat.anchor.resetChainCounter";

typedef enum {
    RUN_IDLE = 0,
    RUN_UP,
    RUN_DOWN,
    RUN_FAULT
} run_state_t;

typedef enum {
    EXT_NONE = 0,
    EXT_UP,
    EXT_DOWN,
    EXT_CONFLICT
} external_state_t;

typedef enum {
    BEEP_DOWN = 0,
    BEEP_UP,
    BEEP_BOTH
} beep_dir_t;

typedef struct {
    bool enabled;
    int neutral_ms;

    int chain_sensor_pin;
    bool chain_sensor_pullup;
    float chain_calibration;
    int pulse_debounce_ms;

    int ext_input_debounce_ms;

    bool freefall_use_meters;
    float freefall_value;

    float base_threshold_m;
    float step_m;
    int base_beeps;
    int beeps_per_step;
    beep_dir_t beep_on_direction;
    float hysteresis_m;
} anchor_config_t;

typedef struct {
    bool raw;
    bool filtered;
    bool last_state;
    uint32_t stable_ms;
} debounce_t;

typedef struct {
    float chain_out_meters;
    int chain_pulse_count;
    bool last_sensor_state;
    uint32_t sensor_stable_since_ms;
    bool sensor_stable_state;
    uint32_t last_pulse_ms;
    float last_saved_chain_meters;
    uint32_t last_chain_save_ms;
} chain_state_t;

typedef struct {
    external_state_t state;
    external_state_t last_valid_state;  /* last confirmed UP or DOWN (not CONFLICT/NONE) */
    bool external_active;
    char source[12];
    debounce_t up_db;
    debounce_t down_db;
    uint32_t last_sample_ms;
} external_state_data_t;

typedef struct {
    float last_alert_threshold;
    int last_alert_beeps;
    char last_alert_time[32];
} buzzer_state_t;

typedef struct {
    run_state_t state;
    uint32_t op_end_ms;
    uint32_t op_start_ms;

    bool neutral_waiting;
    uint32_t neutral_until_ms;
    run_state_t queued_dir;
    float queued_dur_s;

    uint32_t last_command_ms;
    char last_command_state[16];
    bool processing_command;
    bool state_changed;

    uint32_t last_save_check_ms;
    uint32_t last_heartbeat_ms;
    uint32_t connection_start_ms;

    uint32_t disconnect_since_ms;  // 0 = connected, >0 = timestamp of first disconnect

    float chain_target_meters;     // >0 = stop when chain reaches this value
} anchor_runtime_t;

typedef enum {
    CMD_SET_STATE = 0,
    CMD_SET_CHAIN,
    CMD_RESET_CHAIN
} anchor_cmd_type_t;

typedef struct {
    anchor_cmd_type_t type;
    char str[32];
    float value;
} anchor_cmd_t;

static anchor_config_t g_cfg;
static chain_state_t g_chain;
static external_state_data_t g_external;
static buzzer_state_t g_buzzer;
static anchor_runtime_t g_rt;

static QueueHandle_t g_cmd_queue;
static TaskHandle_t g_anchor_task;

static uint32_t now_ms(void) {
    return (uint32_t)((uint64_t)xTaskGetTickCount() * (uint64_t)portTICK_PERIOD_MS);
}

static float config_get_float_or_default(const char *key, float def_val) {
    char buffer[32] = {0};
    if (config_get_string(key, buffer, sizeof(buffer)) == ESP_OK) {
        char *end = NULL;
        float v = strtof(buffer, &end);
        if (end != buffer) {
            return v;
        }
    }
    return def_val;
}

static void config_get_string_or_default(const char *key, char *out, size_t out_len,
                                         const char *def_val) {
    if (config_get_string(key, out, out_len) == ESP_OK) {
        return;
    }
    if (def_val) {
        strncpy(out, def_val, out_len - 1);
        out[out_len - 1] = '\0';
    }
}

static beep_dir_t parse_beep_dir(const char *value) {
    if (!value) {
        return BEEP_DOWN;
    }
    if (strcasecmp(value, "UP") == 0) {
        return BEEP_UP;
    }
    if (strcasecmp(value, "BOTH") == 0) {
        return BEEP_BOTH;
    }
    return BEEP_DOWN;
}

static const char *beep_dir_to_string(beep_dir_t dir) {
    switch (dir) {
        case BEEP_UP:
            return "UP";
        case BEEP_BOTH:
            return "BOTH";
        default:
            return "DOWN";
    }
}

static void load_default_config(anchor_config_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->enabled = true;
    cfg->neutral_ms = 400;

    cfg->chain_sensor_pin = 25;
    cfg->chain_sensor_pullup = true;
    cfg->chain_calibration = 1.0f;
    cfg->pulse_debounce_ms = 150;

    cfg->freefall_use_meters = false;
    cfg->freefall_value = 5.0f;

    cfg->ext_input_debounce_ms = 50;

    cfg->base_threshold_m = 20.0f;
    cfg->step_m = 10.0f;
    cfg->base_beeps = 1;
    cfg->beeps_per_step = 1;
    cfg->beep_on_direction = BEEP_DOWN;
    cfg->hysteresis_m = 0.2f;
}

static void load_config(anchor_config_t *cfg) {
    load_default_config(cfg);

    cfg->enabled = config_get_bool_or_default("enabled", cfg->enabled);
    cfg->neutral_ms = config_get_int_or_default("neutral_ms", cfg->neutral_ms);

    cfg->chain_sensor_pin = config_get_int_or_default("chain_sen_pin", cfg->chain_sensor_pin);
    cfg->chain_sensor_pullup = config_get_bool_or_default("chain_pullup", cfg->chain_sensor_pullup);
    cfg->chain_calibration = config_get_float_or_default("chain_cal", cfg->chain_calibration);
    cfg->pulse_debounce_ms = config_get_int_or_default("pulse_dbnc_ms", cfg->pulse_debounce_ms);

    cfg->freefall_use_meters = config_get_bool_or_default("ff_use_meters", cfg->freefall_use_meters);
    cfg->freefall_value = config_get_float_or_default("ff_value", cfg->freefall_value);

    cfg->ext_input_debounce_ms = config_get_int_or_default("ext_dbnc_ms", cfg->ext_input_debounce_ms);

    cfg->base_threshold_m = config_get_float_or_default("base_thresh_m", cfg->base_threshold_m);
    cfg->step_m = config_get_float_or_default("step_m", cfg->step_m);
    cfg->base_beeps = config_get_int_or_default("base_beeps", cfg->base_beeps);
    cfg->beeps_per_step = config_get_int_or_default("beeps_per_step", cfg->beeps_per_step);

    char dir_buf[16] = {0};
    config_get_string_or_default("beep_on_dir", dir_buf, sizeof(dir_buf),
                                 beep_dir_to_string(cfg->beep_on_direction));
    cfg->beep_on_direction = parse_beep_dir(dir_buf);
}

static inline float clamp_f(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline int clamp_i(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static void validate_config(anchor_config_t *cfg) {
    if (cfg->chain_calibration <= 0.0f) {
        ESP_LOGW(TAG, "chain_calibration <= 0 (%.3f), clamping to 1.0",
                 cfg->chain_calibration);
        cfg->chain_calibration = 1.0f;
    }
    cfg->chain_calibration = clamp_f(cfg->chain_calibration, 0.001f, 100.0f);
    cfg->neutral_ms = clamp_i(cfg->neutral_ms, 0, 5000);
    cfg->pulse_debounce_ms = clamp_i(cfg->pulse_debounce_ms, 10, 1000);
    cfg->ext_input_debounce_ms = clamp_i(cfg->ext_input_debounce_ms, 10, 1000);
    cfg->base_threshold_m = clamp_f(cfg->base_threshold_m, 1.0f, 500.0f);
    cfg->step_m = clamp_f(cfg->step_m, 1.0f, 100.0f);
    cfg->hysteresis_m = clamp_f(cfg->hysteresis_m, 0.0f, 10.0f);

    if (cfg->chain_sensor_pin < -1 || cfg->chain_sensor_pin > 48) {
        ESP_LOGW(TAG, "Invalid chain_sensor_pin %d, disabling", cfg->chain_sensor_pin);
        cfg->chain_sensor_pin = -1;
    }
}

static void save_chain_to_nvs(float meters) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(ANCHOR_NVS_NS, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS open failed: %s", esp_err_to_name(err));
        return;
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%.3f", meters);
    err = nvs_set_str(handle, "chain_meters", buf);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS write chain_meters failed: %s", esp_err_to_name(err));
    }
    err = nvs_commit(handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS commit failed: %s", esp_err_to_name(err));
    }
    nvs_close(handle);
}

static void load_chain_from_nvs(chain_state_t *chain, float calibration) {
    nvs_handle_t handle;
    if (nvs_open(ANCHOR_NVS_NS, NVS_READONLY, &handle) != ESP_OK) {
        return;
    }
    size_t len = 0;
    if (nvs_get_str(handle, "chain_meters", NULL, &len) == ESP_OK && len > 0) {
        char *buf = (char *)malloc(len + 1);
        if (buf && nvs_get_str(handle, "chain_meters", buf, &len) == ESP_OK) {
            buf[len] = '\0';
            chain->chain_out_meters = strtof(buf, NULL);
            if (chain->chain_out_meters < 0.0f) {
                chain->chain_out_meters = 0.0f;
            }
            chain->chain_pulse_count = (int)(chain->chain_out_meters / calibration);
            chain->last_saved_chain_meters = chain->chain_out_meters;
            chain->last_chain_save_ms = now_ms();
        }
        free(buf);
    }
    nvs_close(handle);
}

static void motor_off(void) {
    gpio_set_level(MOTOR_EN_PIN, 0);   /* power off  */
    gpio_set_level(MOTOR_DIR_PIN, 0);  /* direction LOW = DOWN (safe default when disabled) */
}

static void motor_up_on(void) {
    gpio_set_level(MOTOR_EN_PIN, 0);   /* 1. disable power first   */
    gpio_set_level(MOTOR_DIR_PIN, 1);  /* 2. direction: HIGH = UP  */
    gpio_set_level(MOTOR_EN_PIN, 1);   /* 3. enable power          */
}

static void motor_down_on(void) {
    gpio_set_level(MOTOR_EN_PIN, 0);   /* 1. disable power first   */
    gpio_set_level(MOTOR_DIR_PIN, 0);  /* 2. direction: LOW = DOWN */
    gpio_set_level(MOTOR_EN_PIN, 1);   /* 3. enable power          */
}

static void status_led_set(bool on) {
    gpio_set_level(STATUS_LED_PIN, on ? 1 : 0);
}

static void setup_output_pin(int pin) {
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&cfg);
}

static void setup_input_pin(int pin, bool pullup, bool pulldown) {
    if (pin < 0) {
        return;
    }
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = pullup ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = pulldown ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&cfg);
}

static void setup_pins(void) {
    /* Motor outputs: IO26 = enable (active HIGH), IO27 = direction (LOW=UP, HIGH=DOWN) */
    setup_output_pin(MOTOR_EN_PIN);
    setup_output_pin(MOTOR_DIR_PIN);
    motor_off();

    /* Status LED: IO2, active HIGH */
    setup_output_pin(STATUS_LED_PIN);
    status_led_set(false);

    /* Chain sensor: configurable pin with optional pull-up */
    if (g_cfg.chain_sensor_pin >= 0) {
        setup_input_pin(g_cfg.chain_sensor_pin, g_cfg.chain_sensor_pullup, !g_cfg.chain_sensor_pullup);
        g_chain.last_sensor_state = gpio_get_level(g_cfg.chain_sensor_pin);
        g_chain.sensor_stable_state = g_chain.last_sensor_state;
        g_chain.sensor_stable_since_ms = now_ms();
    }

    /* External sense inputs: IO16 = UP, IO17 = DOWN, active LOW (optocoupler open-collector) → pull-up */
    setup_input_pin(EXT_UP_PIN,   true, false);
    setup_input_pin(EXT_DOWN_PIN, true, false);
}

static void sk_send_bool(const char *path, bool value) {
    signalk_data_t data = {0};
    strncpy(data.path, path, sizeof(data.path) - 1);
    data.type = SIGNALK_VALUE_BOOL;
    data.value.b = value;
    data.priority = SIGNALK_PRIORITY_INSTANT;
    signalk_send_data(&data);
}

static void sk_send_int(const char *path, int value) {
    signalk_data_t data = {0};
    strncpy(data.path, path, sizeof(data.path) - 1);
    data.type = SIGNALK_VALUE_INT;
    data.value.i = value;
    data.priority = SIGNALK_PRIORITY_INSTANT;
    signalk_send_data(&data);
}

static void sk_send_float(const char *path, float value) {
    signalk_data_t data = {0};
    strncpy(data.path, path, sizeof(data.path) - 1);
    data.type = SIGNALK_VALUE_FLOAT;
    data.value.f = value;
    data.priority = SIGNALK_PRIORITY_INSTANT;
    signalk_send_data(&data);
}

static void sk_send_string(const char *path, const char *value) {
    signalk_data_t data = {0};
    strncpy(data.path, path, sizeof(data.path) - 1);
    data.type = SIGNALK_VALUE_STRING;
    if (value) {
        strncpy(data.value.s, value, sizeof(data.value.s) - 1);
    }
    data.priority = SIGNALK_PRIORITY_INSTANT;
    signalk_send_data(&data);
}

static const char *state_to_string(run_state_t state) {
    switch (state) {
        case RUN_UP:
            return "running_up";
        case RUN_DOWN:
            return "running_down";
        case RUN_FAULT:
            return "fault";
        default:
            return "idle";
    }
}

/* Returns the run state driven by external sense inputs (passive, no motor control).
 * During a CONFLICT (both inputs briefly active due to back-EMF / electrical coupling),
 * falls back to the last confirmed direction so the chain counter keeps working. */
static run_state_t external_run_state(void) {
    external_state_t ext = g_external.state;
    if (ext == EXT_CONFLICT) ext = g_external.last_valid_state;
    if (ext == EXT_UP)   return RUN_UP;
    if (ext == EXT_DOWN) return RUN_DOWN;
    return RUN_IDLE;
}

/*
 * Effective state for SK reporting and chain counter direction.
 * SK-commanded state takes absolute priority.
 * When SK is idle, external sense fills in the direction
 * (motor may be running via physical boat switches).
 */
static run_state_t effective_run_state(void) {
    return (g_rt.state != RUN_IDLE) ? g_rt.state : external_run_state();
}

static void publish_state(void) {
    /* State reflects the SK-commanded motor state only.
     * External sense inputs are internal — they drive the chain counter direction
     * but are never exposed through the main state path. */
    sk_send_string("sensors.akat.anchor.state", state_to_string(g_rt.state));
}

static void send_chain_update(void) {
    sk_send_float("sensors.akat.anchor.chainOut", g_chain.chain_out_meters);
    sk_send_int("sensors.akat.anchor.chainPulses", g_chain.chain_pulse_count);
}

static void buzzer_fire_alert(int beeps, float threshold) {
    g_buzzer.last_alert_beeps = beeps;
    g_buzzer.last_alert_threshold = threshold;

    time_t now;
    time(&now);
    struct tm *tm_info = gmtime(&now);
    char ts[32] = {0};
    if (tm_info) {
        strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", tm_info);
    }
    strncpy(g_buzzer.last_alert_time, ts, sizeof(g_buzzer.last_alert_time) - 1);

    char payload[96];
    snprintf(payload, sizeof(payload),
             "{\"beeps\":%d,\"threshold\":%.1f,\"time\":\"%s\"}",
             beeps, threshold, ts);

    sk_send_string("sensors.akat.anchor.alert.buzzerEvent", payload);
    sk_send_float("sensors.akat.anchor.alert.lastThreshold", threshold);
    sk_send_int("sensors.akat.anchor.alert.lastBeeps", beeps);
    sk_send_string("sensors.akat.anchor.alert.firedAt", ts);
}

static void buzzer_check_thresholds(float prev_chain, float curr_chain, bool direction_down) {
    bool dir_match = (g_cfg.beep_on_direction == BEEP_DOWN && direction_down) ||
                     (g_cfg.beep_on_direction == BEEP_UP && !direction_down) ||
                     (g_cfg.beep_on_direction == BEEP_BOTH);
    if (!dir_match) {
        return;
    }

    if (g_cfg.step_m <= 0.0f) {
        return;
    }

    int last_step = (int)((prev_chain - g_cfg.base_threshold_m + g_cfg.hysteresis_m) / g_cfg.step_m);
    int curr_step = (int)((curr_chain - g_cfg.base_threshold_m) / g_cfg.step_m);

    if (prev_chain < g_cfg.base_threshold_m && curr_chain >= g_cfg.base_threshold_m) {
        buzzer_fire_alert(g_cfg.base_beeps, curr_chain);
    } else if (curr_step > last_step && curr_step >= 0) {
        int n_beeps = g_cfg.base_beeps + curr_step * g_cfg.beeps_per_step;
        float thresh = g_cfg.base_threshold_m + curr_step * g_cfg.step_m;
        buzzer_fire_alert(n_beeps, thresh);
    }
}

static bool chain_update(int direction) {
    if (g_cfg.chain_sensor_pin < 0) {
        return false;
    }
    bool current_state = gpio_get_level(g_cfg.chain_sensor_pin);
    uint32_t now = now_ms();

    if (current_state != g_chain.last_sensor_state) {
        g_chain.last_sensor_state = current_state;
        g_chain.sensor_stable_since_ms = now;
        return false;
    }

    if (now - g_chain.sensor_stable_since_ms < (uint32_t)g_cfg.pulse_debounce_ms) {
        return false;
    }

    if (current_state != g_chain.sensor_stable_state) {
        bool old_stable = g_chain.sensor_stable_state;
        g_chain.sensor_stable_state = current_state;

        if (!old_stable && g_chain.sensor_stable_state) {
            if (now - g_chain.last_pulse_ms < (uint32_t)g_cfg.pulse_debounce_ms * 2U) {
                return false;
            }
            g_chain.last_pulse_ms = now;

            if (direction > 0) {
                g_chain.chain_out_meters += g_cfg.chain_calibration;
                g_chain.chain_pulse_count++;
            } else if (direction < 0) {
                g_chain.chain_out_meters -= g_cfg.chain_calibration;
                if (g_chain.chain_out_meters < 0.0f) g_chain.chain_out_meters = 0.0f;
                g_chain.chain_pulse_count--;
                if (g_chain.chain_pulse_count < 0) g_chain.chain_pulse_count = 0;
            }

            send_chain_update();
            return true;
        }
    }

    return false;
}

static void chain_reset(void) {
    g_chain.chain_out_meters = 0.0f;
    g_chain.chain_pulse_count = 0;
    g_chain.last_saved_chain_meters = 0.0f;
    g_chain.last_chain_save_ms = now_ms();
    send_chain_update();
}

static void chain_set_meters(float meters) {
    if (meters < 0.0f) {
        meters = 0.0f;
    }
    g_chain.chain_out_meters = meters;
    g_chain.chain_pulse_count = (int)(meters / g_cfg.chain_calibration);
    g_chain.last_saved_chain_meters = meters;
    g_chain.last_chain_save_ms = now_ms();
    send_chain_update();
}

static bool chain_needs_save(float threshold_m, uint32_t periodic_ms) {
    uint32_t now = now_ms();
    float change = fabsf(g_chain.chain_out_meters - g_chain.last_saved_chain_meters);
    if (change >= threshold_m) {
        return true;
    }
    if (g_chain.chain_out_meters != g_chain.last_saved_chain_meters) {
        if (g_chain.last_chain_save_ms == 0 ||
            (now - g_chain.last_chain_save_ms >= periodic_ms)) {
            return true;
        }
    }
    return false;
}

static void chain_mark_saved(void) {
    g_chain.last_saved_chain_meters = g_chain.chain_out_meters;
    g_chain.last_chain_save_ms = now_ms();
}

static void external_debounce_update(debounce_t *db, bool input, uint32_t now, uint32_t debounce_ms) {
    db->raw = input;
    if (db->raw != db->last_state) {
        db->stable_ms = now;
        db->last_state = db->raw;
    } else if (now - db->stable_ms >= debounce_ms) {
        db->filtered = db->raw;
    }
}

static external_state_t external_update(void) {
    uint32_t now = now_ms();
    if (now - g_external.last_sample_ms < 10) {
        return g_external.state;
    }
    g_external.last_sample_ms = now;

    /* Debug: log raw GPIO levels on any change */
    static int prev_raw_up   = -1;
    static int prev_raw_down = -1;
    int raw_up   = gpio_get_level(EXT_UP_PIN);
    int raw_down = gpio_get_level(EXT_DOWN_PIN);
    if (raw_up != prev_raw_up || raw_down != prev_raw_down) {
        ESP_LOGI(TAG, "RAW GPIO: IO16(UP)=%d  IO17(DOWN)=%d", raw_up, raw_down);
        prev_raw_up   = raw_up;
        prev_raw_down = raw_down;
    }

    external_debounce_update(&g_external.up_db, (raw_up == 0),
                             now, (uint32_t)g_cfg.ext_input_debounce_ms);
    external_debounce_update(&g_external.down_db, (raw_down == 0),
                             now, (uint32_t)g_cfg.ext_input_debounce_ms);

    external_state_t new_state = EXT_NONE;
    const char *new_source = "NONE";

    /* Hardware encoding (active LOW optocouplers, filtered=true means pin is LOW):
     *   IO16=0, IO17=0  →  UP   (up_db=true,  down_db=true)
     *   IO16=1, IO17=0  →  DOWN (up_db=false, down_db=true)
     *   IO16=1, IO17=1  →  IDLE (up_db=false, down_db=false)
     *   IO16=0, IO17=1  →  transition, treat as UP */
    if (g_external.up_db.filtered && g_external.down_db.filtered) {
        /* Both LOW = UP */
        new_state = EXT_UP;
        new_source = "UP";
    } else if (!g_external.up_db.filtered && g_external.down_db.filtered) {
        /* IO16=1, IO17=0 = DOWN */
        new_state = EXT_DOWN;
        new_source = "DOWN";
    } else if (g_external.up_db.filtered && !g_external.down_db.filtered) {
        /* IO16=0, IO17=1 = transition, treat as UP */
        new_state = EXT_UP;
        new_source = "UP";
    }
    /* else: IO16=1, IO17=1 = NONE (already set above) */

    /* Remember the last clean direction so conflict can fall back to it. */
    if (new_state == EXT_UP || new_state == EXT_DOWN) {
        g_external.last_valid_state = new_state;
    }

    bool state_changed = (g_external.external_active != (new_state != EXT_NONE)) ||
                         (strcmp(g_external.source, new_source) != 0);

    if (state_changed) {
        strncpy(g_external.source, new_source, sizeof(g_external.source) - 1);
        g_external.external_active = (new_state != EXT_NONE);
        g_external.state = new_state;
    }

    return new_state;
}

static void external_publish_state(void) {
    /* CONFLICT is a back-EMF / electrical coupling artifact — never expose it.
     * Only publish when there is a clean, single-direction signal. */
    bool active = (g_external.state == EXT_UP || g_external.state == EXT_DOWN);
    sk_send_bool("sensors.akat.anchor.externalControl.active", active);
    sk_send_string("sensors.akat.anchor.externalControl.source", active ? g_external.source : "NONE");
}

static void stop_now(const char *reason) {
    motor_off();
    status_led_set(false);
    g_rt.state = RUN_IDLE;
    g_rt.op_end_ms = 0;
    g_rt.op_start_ms = 0;
    g_rt.neutral_waiting = false;
    g_rt.chain_target_meters = 0.0f;
    g_rt.state_changed = true;
    ESP_LOGI(TAG, "Motor stopped: %s", reason ? reason : "stop");
}

static void start_run(run_state_t dir, float seconds) {
    uint32_t now = now_ms();
    g_rt.op_start_ms = now;
    g_rt.op_end_ms = now + (uint32_t)(seconds * 1000.0f);

    if (dir == RUN_UP) {
        motor_up_on();
        status_led_set(true);
        g_rt.state = RUN_UP;
        ESP_LOGI(TAG, "Motor START: UP for %.1fs", seconds);
    } else if (dir == RUN_DOWN) {
        motor_down_on();
        status_led_set(true);
        g_rt.state = RUN_DOWN;
        ESP_LOGI(TAG, "Motor START: DOWN for %.1fs", seconds);
    }

    g_rt.state_changed = true;
}

static void run_direction(run_state_t dir, float seconds) {
    if (!g_cfg.enabled) {
        return;
    }

    if (g_rt.processing_command) {
        return;
    }

    g_rt.processing_command = true;
    uint32_t now = now_ms();

    const char *current_cmd = (dir == RUN_UP) ? "up" : (dir == RUN_DOWN) ? "down" : "idle";
    if (strcmp(current_cmd, g_rt.last_command_state) == 0 &&
        (now - g_rt.last_command_ms < 250)) {
        g_rt.processing_command = false;
        return;
    }

    g_rt.last_command_ms = now;
    strncpy(g_rt.last_command_state, current_cmd, sizeof(g_rt.last_command_state) - 1);

    float dur = seconds;

    if ((dir == RUN_UP && g_rt.state == RUN_DOWN) ||
        (dir == RUN_DOWN && g_rt.state == RUN_UP)) {
        motor_off();
        status_led_set(false);
        g_rt.neutral_waiting = true;
        g_rt.neutral_until_ms = now + (uint32_t)g_cfg.neutral_ms;
        g_rt.queued_dir = dir;
        g_rt.queued_dur_s = dur;
        g_rt.processing_command = false;
        ESP_LOGI(TAG, "Direction change: neutral delay %dms", g_cfg.neutral_ms);
        return;
    }

    if ((dir == RUN_UP && g_rt.state == RUN_UP) ||
        (dir == RUN_DOWN && g_rt.state == RUN_DOWN)) {
        uint32_t remaining = (g_rt.op_end_ms > now) ? (g_rt.op_end_ms - now) : 0;
        g_rt.op_end_ms = now + remaining + (uint32_t)(dur * 1000.0f);
        g_rt.processing_command = false;
        return;
    }

    if (g_rt.neutral_waiting && now < g_rt.neutral_until_ms) {
        g_rt.queued_dir = dir;
        g_rt.queued_dur_s = dur;
        g_rt.processing_command = false;
        return;
    }

    if (dir == RUN_IDLE) {
        stop_now("command:idle");
    } else {
        start_run(dir, dur);
    }

    g_rt.processing_command = false;
}

static void handle_neutral_queue(void) {
    if (g_rt.neutral_waiting && now_ms() >= g_rt.neutral_until_ms) {
        g_rt.neutral_waiting = false;
        if (g_rt.queued_dir != RUN_IDLE) {
            run_state_t dir = g_rt.queued_dir;
            float dur = g_rt.queued_dur_s;
            g_rt.queued_dir = RUN_IDLE;
            g_rt.queued_dur_s = 0.0f;
            start_run(dir, dur);
        }
    }
}

static void check_timeout(void) {
    if ((g_rt.state == RUN_UP || g_rt.state == RUN_DOWN) &&
        now_ms() >= g_rt.op_end_ms && g_rt.op_end_ms > 0) {
        stop_now(g_rt.state == RUN_UP ? "timeout:up" : "timeout:down");
    }
}

static void check_auto_save(void) {
    uint32_t now = now_ms();
    if (now - g_rt.last_save_check_ms < 5000) {
        return;
    }
    g_rt.last_save_check_ms = now;

    if (chain_needs_save(5.0f, 30000)) {
        save_chain_to_nvs(g_chain.chain_out_meters);
        chain_mark_saved();
    }
}

static bool is_signalk_connected(void) {
    signalk_status_t status = {0};
    if (signalk_get_status(&status) != ESP_OK) {
        return false;
    }
    return status.state == SIGNALK_STATE_CONNECTED ||
           status.state == SIGNALK_STATE_STREAMING;
}

static bool should_process_signalk(void) {
    if (!is_signalk_connected()) {
        g_rt.connection_start_ms = 0;
        return false;
    }

    if (g_rt.connection_start_ms == 0) {
        g_rt.connection_start_ms = now_ms();
        return false;
    }

    return (now_ms() - g_rt.connection_start_ms) >= 2000;
}

static void handle_command(const anchor_cmd_t *cmd) {
    if (!cmd) {
        return;
    }

    if (cmd->type == CMD_SET_STATE) {
        if (strcmp(cmd->str, "running_up") == 0) {
            if (g_rt.state != RUN_UP) run_direction(RUN_UP, 3600.0f);
        } else if (strcmp(cmd->str, "running_down") == 0) {
            if (g_rt.state != RUN_DOWN) run_direction(RUN_DOWN, 3600.0f);
        } else if (strcmp(cmd->str, "freefall") == 0) {
            if (g_cfg.freefall_use_meters) {
                /* Run DOWN until chain_out reaches target meters */
                g_rt.chain_target_meters = g_chain.chain_out_meters + g_cfg.freefall_value;
                run_direction(RUN_DOWN, 3600.0f);
                ESP_LOGI(TAG, "Freefall: target %.1fm (current %.1fm + %.1fm)",
                         g_rt.chain_target_meters, g_chain.chain_out_meters, g_cfg.freefall_value);
            } else {
                /* Run DOWN for configured seconds */
                run_direction(RUN_DOWN, g_cfg.freefall_value);
            }
        } else if (strcmp(cmd->str, "idle") == 0) {
            if (g_rt.state != RUN_IDLE) stop_now("command:idle");
        } else if (strcmp(cmd->str, "reset_counter") == 0) {
            chain_reset();
            save_chain_to_nvs(g_chain.chain_out_meters);
            chain_mark_saved();
            g_buzzer.last_alert_beeps = 0;
            g_buzzer.last_alert_threshold = 0.0f;
            g_buzzer.last_alert_time[0] = '\0';
        }
    } else if (cmd->type == CMD_SET_CHAIN) {
        if (fabsf(g_chain.chain_out_meters - cmd->value) >= 0.01f) {
            chain_set_meters(cmd->value);
            save_chain_to_nvs(g_chain.chain_out_meters);
            chain_mark_saved();
        }
    } else if (cmd->type == CMD_RESET_CHAIN) {
        chain_reset();
        save_chain_to_nvs(g_chain.chain_out_meters);
        chain_mark_saved();
        g_buzzer.last_alert_beeps = 0;
        g_buzzer.last_alert_threshold = 0.0f;
        g_buzzer.last_alert_time[0] = '\0';
    }
}

static void send_heartbeat(void) {
    if (!is_signalk_connected()) {
        return;
    }

    time_t now;
    time(&now);
    struct tm *tm_info = gmtime(&now);
    char timestamp[30] = {0};
    if (tm_info) {
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", tm_info);
    }

    sk_send_bool("sensors.akat.anchor.enabled", g_cfg.enabled);
    sk_send_string("sensors.akat.anchor.lastUpdate", timestamp);
    sk_send_float("sensors.akat.anchor.chainOut", g_chain.chain_out_meters);
    sk_send_string("sensors.akat.anchor.state", state_to_string(g_rt.state));
}

static void anchor_tick(void) {
    if (g_rt.state_changed) {
        g_rt.state_changed = false;
        publish_state();
    }

    if (!is_signalk_connected()) {
        /* Safety stop only for SK-commanded motor runs (motor GPIO is active). */
        if (g_rt.state == RUN_UP || g_rt.state == RUN_DOWN) {
            uint32_t t = now_ms();
            if (g_rt.disconnect_since_ms == 0) {
                g_rt.disconnect_since_ms = t;
                ESP_LOGW(TAG, "SignalK disconnected while running, debouncing...");
            } else if ((t - g_rt.disconnect_since_ms) >= SAFETY_DISCONNECT_DEBOUNCE_MS) {
                ESP_LOGE(TAG, "Safety stop: SignalK disconnected for %lums",
                         (unsigned long)(t - g_rt.disconnect_since_ms));
                stop_now("safety:disconnected");
                return;
            }
        }
    } else {
        g_rt.disconnect_since_ms = 0;
    }

    external_state_t prev_ext = g_external.state;
    external_state_t ext_state = external_update();
    if (ext_state != prev_ext) {
        external_publish_state();
        /*
         * External sense inputs (IO16=UP, IO17=DOWN) are PASSIVE observers.
         * They detect when physical boat switches are driving the motor through
         * the same output path, so the chain counter knows the direction.
         * Motor GPIO pins (IO26 enable, IO27 direction) are controlled ONLY
         * by SK delta commands (running_up / running_down / idle).
         */
        ESP_LOGI(TAG, "External sense: %s (raw IO16=%d IO17=%d)",
                 ext_state == EXT_UP ? "UP" : ext_state == EXT_DOWN ? "DOWN" : "NONE",
                 gpio_get_level(EXT_UP_PIN), gpio_get_level(EXT_DOWN_PIN));
        /* publish_state() sends g_rt.state only — external changes don't affect it. */
    }

    float prev_chain = g_chain.chain_out_meters;
    /*
     * Chain counter direction: SK-commanded state takes priority.
     * When SK is idle, external sense provides the direction so the
     * chain counter works during physical-switch-driven motor runs.
     */
    int direction = 0;
    run_state_t eff_state = effective_run_state();
    if (eff_state == RUN_DOWN) direction = 1;
    else if (eff_state == RUN_UP) direction = -1;

    bool pulsed = chain_update(direction);
    if (pulsed) {
        buzzer_check_thresholds(prev_chain, g_chain.chain_out_meters, direction > 0);

        /* Check chain target (freefall by meters) */
        if (g_rt.chain_target_meters > 0.0f &&
            g_rt.state == RUN_DOWN &&
            g_chain.chain_out_meters >= g_rt.chain_target_meters) {
            ESP_LOGI(TAG, "Chain target reached: %.1fm", g_chain.chain_out_meters);
            stop_now("chain_target");
        }
    }

    handle_neutral_queue();
    check_timeout();
    check_auto_save();

    uint32_t now = now_ms();
    if (now - g_rt.last_heartbeat_ms >= 10000) {
        g_rt.last_heartbeat_ms = now;
        send_heartbeat();
    }
}

static void anchor_task(void *arg) {
    (void)arg;
    anchor_cmd_t cmd;
    uint32_t last_config_check_ms = 0;

    // Subscribe to task watchdog
    esp_task_wdt_add(NULL);

    while (1) {
        esp_task_wdt_reset();

        while (xQueueReceive(g_cmd_queue, &cmd, 0) == pdTRUE) {
            if (should_process_signalk()) {
                handle_command(&cmd);
            }
        }

        uint32_t now = now_ms();
        if (now - last_config_check_ms >= 5000) {
            last_config_check_ms = now;
            anchor_config_t next_cfg;
            load_config(&next_cfg);
            validate_config(&next_cfg);
            if (memcmp(&next_cfg, &g_cfg, sizeof(g_cfg)) != 0) {
                bool was_enabled = g_cfg.enabled;
                bool pins_changed =
                    next_cfg.chain_sensor_pin != g_cfg.chain_sensor_pin;
                g_cfg = next_cfg;
                if (pins_changed) {
                    setup_pins();
                }
                g_chain.chain_pulse_count = (int)(g_chain.chain_out_meters / g_cfg.chain_calibration);
                if (!g_cfg.enabled && was_enabled) {
                    stop_now("config:disable");
                }
                ESP_LOGI(TAG, "Configuration reloaded");
            }
        }

        anchor_tick();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void signalk_cb(const signalk_data_t *data, void *user_ctx) {
    (void)user_ctx;
    if (!data || !data->path[0]) {
        return;
    }

    anchor_cmd_t cmd = {0};

    if (strcmp(data->path, SK_CMD_PATH) == 0) {
        if (data->type == SIGNALK_VALUE_STRING) {
            cmd.type = CMD_SET_STATE;
            strncpy(cmd.str, data->value.s, sizeof(cmd.str) - 1);
        } else {
            return;
        }
    } else if (strcmp(data->path, SK_CHAIN_PATH) == 0) {
        cmd.type = CMD_SET_CHAIN;
        if (data->type == SIGNALK_VALUE_FLOAT) {
            cmd.value = data->value.f;
        } else if (data->type == SIGNALK_VALUE_INT) {
            cmd.value = (float)data->value.i;
        } else {
            return;
        }
    } else if (strcmp(data->path, SK_RESET_PATH) == 0) {
        cmd.type = CMD_RESET_CHAIN;
        float v = 0.0f;
        if (data->type == SIGNALK_VALUE_BOOL) {
            v = data->value.b ? 1.0f : 0.0f;
        } else if (data->type == SIGNALK_VALUE_FLOAT) {
            v = data->value.f;
        } else if (data->type == SIGNALK_VALUE_INT) {
            v = (float)data->value.i;
        } else {
            return;
        }
        if (v < 0.5f) {
            return;
        }
    } else {
        return;
    }

    if (g_cmd_queue) {
        if (xQueueSend(g_cmd_queue, &cmd, 0) != pdTRUE) {
            ESP_LOGW(TAG, "Command queue full, dropping command for path: %s", data->path);
        }
    }
}

static void subscribe_paths(void) {
    // Only subscribe to INBOUND command paths (paths we receive commands on).
    // Do NOT subscribe to paths we send data on - that creates a feedback loop
    // where SignalK echoes our own data back, generating spurious commands.
    signalk_subscribe(SK_CMD_PATH, 1000);
    signalk_subscribe(SK_RESET_PATH, 1000);
}

esp_err_t anchor_guard_init(void) {
    load_config(&g_cfg);
    validate_config(&g_cfg);
    setup_pins();

    memset(&g_chain, 0, sizeof(g_chain));
    load_chain_from_nvs(&g_chain, g_cfg.chain_calibration);

    memset(&g_external, 0, sizeof(g_external));
    strncpy(g_external.source, "NONE", sizeof(g_external.source) - 1);

    memset(&g_buzzer, 0, sizeof(g_buzzer));
    memset(&g_rt, 0, sizeof(g_rt));
    g_rt.state = RUN_IDLE;

    g_cmd_queue = xQueueCreate(16, sizeof(anchor_cmd_t));
    if (!g_cmd_queue) {
        return ESP_ERR_NO_MEM;
    }

    signalk_register_callback("sensors.akat.anchor", signalk_cb, NULL);
    subscribe_paths();

    BaseType_t res = xTaskCreate(anchor_task, "anchor_guard", 6144, NULL, 4, &g_anchor_task);
    if (res != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Anchor guard initialized");
    return ESP_OK;
}
