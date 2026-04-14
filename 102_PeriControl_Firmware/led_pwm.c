/*
 * Copyright (c) 2026 mr258876
 * SPDX-License-Identifier: MIT
 */

#include "led_pwm.h"

#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "Pinouts.h"

static const uint8_t g_led_pins[LED_CHANNEL_COUNT] = {
    LED_PWM1_PIN,
    LED_PWM2_PIN,
    LED_PWM3_PIN,
    LED_PWM4_PIN,
};

static uint16_t g_wrap = 0;
static uint16_t g_levels[LED_CHANNEL_COUNT] = {0};
static uint8_t g_output_mask = 0u;

static void apply_channel_output(uint32_t channel)
{
    if (channel >= LED_CHANNEL_COUNT) {
        return;
    }

    uint pin = g_led_pins[channel];
    if (((g_output_mask >> channel) & 0x01u) != 0u) {
        gpio_set_function(pin, GPIO_FUNC_PWM);
        uint slice = pwm_gpio_to_slice_num(pin);
        uint chan = pwm_gpio_to_channel(pin);
        pwm_set_chan_level(slice, chan, g_levels[channel]);
    } else {
        gpio_set_function(pin, GPIO_FUNC_SIO);
        gpio_set_dir(pin, GPIO_OUT);
        gpio_put(pin, 0u);
    }
}

void led_pwm_init(uint16_t wrap)
{
    g_wrap = wrap ? wrap : 1u;

    for (uint32_t i = 0; i < LED_CHANNEL_COUNT; i++) {
        gpio_set_function(g_led_pins[i], GPIO_FUNC_PWM);
        uint slice = pwm_gpio_to_slice_num(g_led_pins[i]);
        pwm_config cfg = pwm_get_default_config();
        pwm_config_set_clkdiv(&cfg, 1.0f);
        pwm_config_set_wrap(&cfg, g_wrap);
        pwm_init(slice, &cfg, true);
    }

    g_output_mask = 0u;
    for (uint32_t i = 0; i < LED_CHANNEL_COUNT; i++) {
        apply_channel_output(i);
    }
}

void led_pwm_set_wrap(uint16_t wrap)
{
    g_wrap = wrap ? wrap : 1u;
    for (uint32_t i = 0; i < LED_CHANNEL_COUNT; i++) {
        uint slice = pwm_gpio_to_slice_num(g_led_pins[i]);
        pwm_set_wrap(slice, g_wrap);
        if (g_levels[i] > g_wrap) {
            g_levels[i] = g_wrap;
        }
        apply_channel_output(i);
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
    apply_channel_output(channel);
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

void led_pwm_set_output_mask(uint8_t mask)
{
    g_output_mask = (uint8_t)(mask & ((1u << LED_CHANNEL_COUNT) - 1u));
    for (uint32_t i = 0; i < LED_CHANNEL_COUNT; i++) {
        apply_channel_output(i);
    }
}
