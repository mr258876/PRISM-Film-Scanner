/*
 * Copyright (c) 2026 mr258876
 * SPDX-License-Identifier: MIT
 */

#ifndef _PERSISTENT_PARAMS_H_
#define _PERSISTENT_PARAMS_H_

#include <stdbool.h>
#include <stdint.h>

#include "Pinouts.h"

typedef struct {
    uint32_t sys_clock_khz;
    uint16_t led_pwm_wrap;
    uint16_t led_level[LED_CHANNEL_COUNT];
    uint8_t motor_irun[MOTOR_COUNT];
    uint8_t motor_ihold[MOTOR_COUNT];
    uint16_t motor_microsteps[MOTOR_COUNT];
    uint8_t motor_stealthchop_enable[MOTOR_COUNT];
    uint32_t motor_step_interval_us[MOTOR_COUNT];
} prism_params_t;

void prism_params_set_defaults(prism_params_t *params);
bool prism_params_load(prism_params_t *params);
bool prism_params_save(const prism_params_t *params);
uint32_t prism_param_hash_key(const char *key);
bool prism_param_meta_by_hash(uint32_t key_hash, uint8_t *type, uint8_t *len);
bool prism_param_get_by_hash(const prism_params_t *params, uint32_t key_hash, uint8_t *type, uint8_t *len, uint8_t *value);
bool prism_param_set_by_hash(prism_params_t *params, uint32_t key_hash, uint8_t type, uint8_t len, const uint8_t *value);

#endif
