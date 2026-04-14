/*
 * Copyright (c) 2026 mr258876
 * SPDX-License-Identifier: MIT
 */

#include "exposure_sync.h"

#include <string.h>
#include <stdint.h>

#include "led_pwm.h"

#include "hardware/gpio.h"
#include "hardware/pio.h"

#include "Pinouts.h"
#include "exposure_sync.pio.h"

#define EXPOSURE_SYNC_VALID_LED_MASK  ((1u << LED_CHANNEL_COUNT) - 1u)
#define EXPOSURE_SYNC_MIN_PULSE_CLK   1u

typedef struct {
    bool enabled;
} led_sync_sm_state_t;

static const uint8_t g_led_pins[LED_CHANNEL_COUNT] = {
    LED_PWM1_PIN,
    LED_PWM2_PIN,
    LED_PWM3_PIN,
    LED_PWM4_PIN,
};

static PIO g_pio = pio0;
static uint g_program_offset = 0;
static bool g_initialized = false;
static uint8_t g_steady_mask = 0u;
static uint8_t g_sync_mask = 0u;
static uint32_t g_pulse_clk[LED_CHANNEL_COUNT] = {0};
static led_sync_sm_state_t g_sm_state[LED_CHANNEL_COUNT] = {0};

static bool any_sync_led_active(void)
{
    for (uint32_t i = 0; i < LED_CHANNEL_COUNT; i++) {
        bool led_sync = ((g_sync_mask >> i) & 0x01u) != 0u;
        if (led_sync && gpio_get(g_led_pins[i])) {
            return true;
        }
    }
    return false;
}

static void clear_pulse_widths(void)
{
    memset(g_pulse_clk, 0, sizeof(g_pulse_clk));
}

static void stop_led_sm(uint32_t channel, bool drive_high)
{
    uint sm = (uint)channel;
    if (g_sm_state[channel].enabled) {
        pio_sm_set_enabled(g_pio, sm, false);
        pio_sm_clear_fifos(g_pio, sm);
        pio_sm_restart(g_pio, sm);
        g_sm_state[channel].enabled = false;
    }

    gpio_set_function(g_led_pins[channel], GPIO_FUNC_SIO);
    gpio_set_dir(g_led_pins[channel], GPIO_OUT);
    gpio_put(g_led_pins[channel], drive_high ? 1u : 0u);
}

static void start_led_sm(uint32_t channel, uint32_t pulse_ticks)
{
    uint sm = (uint)channel;
    uint pin = g_led_pins[channel];

    pio_sm_set_enabled(g_pio, sm, false);
    pio_sm_clear_fifos(g_pio, sm);
    pio_sm_restart(g_pio, sm);

    pio_gpio_init(g_pio, pin);
    gpio_pull_down(EXPOSURE_SYNC_PIN);

    pio_sm_config cfg = exposure_sync_pulse_program_get_default_config(g_program_offset);
    sm_config_set_set_pins(&cfg, pin, 1u);
    sm_config_set_in_pins(&cfg, EXPOSURE_SYNC_PIN);
    sm_config_set_clkdiv(&cfg, 1.0f);

    pio_sm_init(g_pio, sm, g_program_offset, &cfg);
    pio_sm_set_consecutive_pindirs(g_pio, sm, pin, 1u, true);
    pio_sm_put_blocking(g_pio, sm, pulse_ticks);
    pio_sm_set_enabled(g_pio, sm, true);
    g_sm_state[channel].enabled = true;
}

void exposure_sync_refresh_outputs(void)
{
    for (uint32_t i = 0; i < LED_CHANNEL_COUNT; i++) {
        bool led_sync = ((g_sync_mask >> i) & 0x01u) != 0u;
        if (!led_sync) {
            stop_led_sm(i, false);
        }
    }

    led_pwm_set_output_mask(g_steady_mask);

    for (uint32_t i = 0; i < LED_CHANNEL_COUNT; i++) {
        bool led_sync = ((g_sync_mask >> i) & 0x01u) != 0u;
        uint32_t pulse_ticks = (g_pulse_clk[i] > 0u) ? (g_pulse_clk[i] - 1u) : 0u;
        if (led_sync && pulse_ticks != 0u) {
            start_led_sm(i, pulse_ticks);
        }
    }
}

void exposure_sync_init(void)
{
    gpio_init(EXPOSURE_SYNC_PIN);
    gpio_set_dir(EXPOSURE_SYNC_PIN, GPIO_IN);
    gpio_pull_down(EXPOSURE_SYNC_PIN);

    g_program_offset = pio_add_program(g_pio, &exposure_sync_pulse_program);
    g_steady_mask = 0u;
    g_sync_mask = 0u;
    clear_pulse_widths();
    g_initialized = true;
    exposure_sync_refresh_outputs();
}

bool exposure_sync_set_steady_mask(uint8_t steady_mask)
{
    steady_mask = (uint8_t)(steady_mask & EXPOSURE_SYNC_VALID_LED_MASK);
    if ((steady_mask & g_sync_mask) != 0u) {
        return false;
    }

    g_steady_mask = steady_mask;
    if (g_initialized) {
        exposure_sync_refresh_outputs();
    }
    return true;
}

bool exposure_sync_set_sync_mask(uint8_t sync_mask)
{
    sync_mask = (uint8_t)(sync_mask & EXPOSURE_SYNC_VALID_LED_MASK);
    if ((sync_mask & g_steady_mask) != 0u) {
        return false;
    }

    for (uint32_t i = 0; i < LED_CHANNEL_COUNT; i++) {
        if (((sync_mask >> i) & 0x01u) != 0u && g_pulse_clk[i] < EXPOSURE_SYNC_MIN_PULSE_CLK) {
            return false;
        }
    }

    g_sync_mask = sync_mask;
    if (g_initialized) {
        exposure_sync_refresh_outputs();
    }
    return true;
}

bool exposure_sync_set_pulse_clk(const uint32_t *pulse_clk)
{
    if (pulse_clk == NULL) {
        return false;
    }

    uint32_t requested_pulses[LED_CHANNEL_COUNT] = {0};
    memcpy(requested_pulses, pulse_clk, sizeof(requested_pulses));
    for (uint32_t i = 0; i < LED_CHANNEL_COUNT; i++) {
        if (((g_sync_mask >> i) & 0x01u) != 0u && requested_pulses[i] < EXPOSURE_SYNC_MIN_PULSE_CLK) {
            return false;
        }
    }

    memcpy(g_pulse_clk, requested_pulses, sizeof(g_pulse_clk));
    if (g_initialized) {
        exposure_sync_refresh_outputs();
    }
    return true;
}

void exposure_sync_get_status(exposure_sync_status_t *status_out)
{
    if (status_out == NULL) {
        return;
    }

    status_out->steady_mask = g_steady_mask;
    status_out->sync_mask = g_sync_mask;
    status_out->active = any_sync_led_active() ? 1u : 0u;
    memcpy(status_out->pulse_clk, g_pulse_clk, sizeof(status_out->pulse_clk));
}
