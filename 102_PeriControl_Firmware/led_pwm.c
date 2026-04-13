/*
 * Copyright (c) 2026 mr258876
 * SPDX-License-Identifier: MIT
 */

#include "led_pwm.h"

#include "hardware/gpio.h"
#include "Pinouts.h"

static const uint8_t g_led_pins[LED_CHANNEL_COUNT] = {
    LED_PWM1_PIN,
    LED_PWM2_PIN,
    LED_PWM3_PIN,
    LED_PWM4_PIN,
};

static uint16_t g_wrap = 0;
static uint16_t g_levels[LED_CHANNEL_COUNT] = {0};

void led_pwm_init(uint16_t wrap)
{
    g_wrap = wrap ? wrap : 1u;

    for (uint32_t i = 0; i < LED_CHANNEL_COUNT; i++) {
        gpio_init(g_led_pins[i]);
        gpio_set_dir(g_led_pins[i], GPIO_OUT);
        gpio_put(g_led_pins[i], 0);
    }
}

void led_pwm_set_wrap(uint16_t wrap)
{
    g_wrap = wrap ? wrap : 1u;
    for (uint32_t i = 0; i < LED_CHANNEL_COUNT; i++) {
        if (g_levels[i] > g_wrap) {
            g_levels[i] = g_wrap;
        }
    }
}

uint16_t led_pwm_get_wrap(void)
{
    return g_wrap;
}

void led_pwm_set_level(uint32_t channel, uint16_t level)
{
    if (channel >= LED_CHANNEL_COUNT) {
        return;
    }

    g_levels[channel] = (level > g_wrap) ? g_wrap : level;
}

uint16_t led_pwm_get_level(uint32_t channel)
{
    return (channel < LED_CHANNEL_COUNT) ? g_levels[channel] : 0u;
}

void led_pwm_set_levels(const uint16_t *levels, uint32_t count)
{
    uint32_t limit = (count < LED_CHANNEL_COUNT) ? count : LED_CHANNEL_COUNT;
    for (uint32_t i = 0; i < limit; i++) {
        led_pwm_set_level(i, levels[i]);
    }
}

uint8_t led_pwm_get_enabled_mask(void)
{
    uint8_t mask = 0u;
    for (uint32_t i = 0; i < LED_CHANNEL_COUNT; i++) {
        if (g_levels[i] != 0u) {
            mask |= (uint8_t)(1u << i);
        }
    }
    return mask;
}
