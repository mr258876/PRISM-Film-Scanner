/*
 * Copyright (c) 2026 mr258876
 * SPDX-License-Identifier: MIT
 */

#include "exposure_sync.h"

#include <stdint.h>

#include "control_protocol.h"
#include "led_pwm.h"

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"

#include "Pinouts.h"
#include "exposure_sync.pio.h"

#define EXPOSURE_SYNC_VALID_LED_MASK  ((1u << LED_CHANNEL_COUNT) - 1u)
#define EXPOSURE_SYNC_MIN_PULSE_US    2u

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
static uint8_t g_mode = CONTROL_LED_SYNC_MODE_DISABLED;
static uint8_t g_led_mask = 0u;
static uint32_t g_pulse_us = 0u;
static led_sync_sm_state_t g_sm_state[LED_CHANNEL_COUNT] = {0};

static bool any_sync_led_active(void)
{
    uint8_t enabled_mask = led_pwm_get_enabled_mask();
    for (uint32_t i = 0; i < LED_CHANNEL_COUNT; i++) {
        bool led_sync = (g_mode == CONTROL_LED_SYNC_MODE_EXPOSURE_PULSE) &&
                        (((g_led_mask >> i) & 0x01u) != 0u) &&
                        (((enabled_mask >> i) & 0x01u) != 0u);
        if (led_sync && gpio_get(g_led_pins[i])) {
            return true;
        }
    }
    return false;
}

static uint32_t pulse_ticks_from_us(uint32_t pulse_us)
{
    uint64_t sys_hz = clock_get_hz(clk_sys);
    uint64_t ticks = (sys_hz * pulse_us) / 1000000u;
    if (ticks == 0u) {
        ticks = 1u;
    }
    if (ticks > 0xFFFFFFFFu) {
        return 0u;
    }
    return (uint32_t)(ticks - 1u);
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
    uint8_t enabled_mask = led_pwm_get_enabled_mask();
    bool sync_mode = (g_mode == CONTROL_LED_SYNC_MODE_EXPOSURE_PULSE);
    uint32_t pulse_ticks = pulse_ticks_from_us(g_pulse_us);

    for (uint32_t i = 0; i < LED_CHANNEL_COUNT; i++) {
        bool led_enabled = ((enabled_mask >> i) & 0x01u) != 0u;
        bool led_sync = sync_mode && (((g_led_mask >> i) & 0x01u) != 0u) && led_enabled;

        if (led_sync) {
            start_led_sm(i, pulse_ticks);
        } else {
            stop_led_sm(i, led_enabled);
        }
    }
}

void exposure_sync_init(void)
{
    gpio_init(EXPOSURE_SYNC_PIN);
    gpio_set_dir(EXPOSURE_SYNC_PIN, GPIO_IN);
    gpio_pull_down(EXPOSURE_SYNC_PIN);

    g_program_offset = pio_add_program(g_pio, &exposure_sync_pulse_program);
    g_mode = CONTROL_LED_SYNC_MODE_DISABLED;
    g_led_mask = 0u;
    g_pulse_us = 0u;
    g_initialized = true;
    exposure_sync_refresh_outputs();
}

bool exposure_sync_configure(uint8_t mode, uint8_t led_mask, uint32_t pulse_us)
{
    led_mask = (uint8_t)(led_mask & EXPOSURE_SYNC_VALID_LED_MASK);
    if (mode != CONTROL_LED_SYNC_MODE_DISABLED && mode != CONTROL_LED_SYNC_MODE_EXPOSURE_PULSE) {
        return false;
    }

    if (mode == CONTROL_LED_SYNC_MODE_EXPOSURE_PULSE &&
        (led_mask == 0u || pulse_us < EXPOSURE_SYNC_MIN_PULSE_US || pulse_ticks_from_us(pulse_us) == 0u))
    {
        return false;
    }

    if (mode == CONTROL_LED_SYNC_MODE_DISABLED) {
        led_mask = 0u;
        pulse_us = 0u;
    }

    g_mode = mode;
    g_led_mask = led_mask;
    g_pulse_us = pulse_us;
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

    status_out->mode = g_mode;
    status_out->active = any_sync_led_active() ? 1u : 0u;
    status_out->led_mask = g_led_mask;
    status_out->pulse_us = g_pulse_us;
}
