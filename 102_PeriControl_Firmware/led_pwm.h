/*
 * Copyright (c) 2026 mr258876
 * SPDX-License-Identifier: MIT
 */

#ifndef LED_PWM_H
#define LED_PWM_H

#include <stdbool.h>
#include <stdint.h>

void led_pwm_init(uint16_t wrap);
void led_pwm_set_wrap(uint16_t wrap);
uint16_t led_pwm_get_wrap(void);
void led_pwm_set_level(uint32_t channel, uint16_t level);
uint16_t led_pwm_get_level(uint32_t channel);
void led_pwm_set_levels(const uint16_t *levels, uint32_t count);
uint8_t led_pwm_get_enabled_mask(void);

#endif
