/*
 * Copyright (c) 2026 mr258876
 * SPDX-License-Identifier: MIT
 */

#include "stepper_task.h"

#include <string.h>

#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

#define STEPPER_SERVICE_PERIOD_US  10u
#define STEPPER_EVENT_QUEUE_DEPTH   8u

typedef struct {
    bool enabled;
    bool running;
    bool armed_on_sync;
    bool sync_rise_seen;
    bool direction;
    bool pulse_high;
    uint32_t remaining_steps;
    uint32_t interval_us;
    uint32_t tick_target;
    uint32_t tick_counter;
} motor_runtime_t;

static const uint8_t g_dir_pins[MOTOR_COUNT] = {M1_DIR_PIN, M2_DIR_PIN, M3_DIR_PIN};
static const uint8_t g_step_pins[MOTOR_COUNT] = {M1_STEP_PIN, M2_STEP_PIN, M3_STEP_PIN};
static const uint8_t g_en_pins[MOTOR_COUNT] = {M1_EN_PIN, M2_EN_PIN, M3_EN_PIN};
static const uint8_t g_diag_pins[MOTOR_COUNT] = {M1_DIAG_PIN, M2_DIAG_PIN, M3_DIAG_PIN};

static repeating_timer_t g_stepper_timer;
static volatile motor_runtime_t g_motor_runtime[MOTOR_COUNT];
static volatile stepper_motion_event_t g_motion_event_queue[STEPPER_EVENT_QUEUE_DEPTH];
static volatile uint8_t g_motion_event_head = 0u;
static volatile uint8_t g_motion_event_tail = 0u;
static volatile uint8_t g_motion_event_count = 0u;

static void stepper_exposure_sync_irq_handler(uint gpio, uint32_t events)
{
    if (gpio != EXPOSURE_SYNC_PIN) {
        return;
    }

    uint32_t irq_state = save_and_disable_interrupts();

    if ((events & GPIO_IRQ_EDGE_RISE) != 0u) {
        for (uint32_t i = 0; i < MOTOR_COUNT; i++) {
            if (g_motor_runtime[i].armed_on_sync) {
                g_motor_runtime[i].sync_rise_seen = true;
            }
        }
    }

    if ((events & GPIO_IRQ_EDGE_FALL) != 0u) {
        for (uint32_t i = 0; i < MOTOR_COUNT; i++) {
            motor_runtime_t *motor = (motor_runtime_t *)&g_motor_runtime[i];
            if (!motor->armed_on_sync || !motor->sync_rise_seen) {
                continue;
            }

            motor->armed_on_sync = false;
            motor->sync_rise_seen = false;
            if (!motor->enabled) {
                motor->running = false;
                motor->remaining_steps = 0u;
                continue;
            }

            motor->pulse_high = false;
            motor->tick_counter = motor->tick_target;
            motor->running = true;
        }
    }

    restore_interrupts(irq_state);
}

static void stepper_push_motion_complete_event_from_timer(uint8_t motor_index,
                                                          const volatile motor_runtime_t *motor)
{
    volatile stepper_motion_event_t *event = &g_motion_event_queue[g_motion_event_head];
    event->motor_index = motor_index;
    event->status.enabled = motor->enabled;
    event->status.running = false;
    event->status.direction = motor->direction;
    event->status.remaining_steps = 0u;
    event->status.configured_interval_us = motor->interval_us;
    event->status.diag_state = 0u;

    g_motion_event_head = (uint8_t)((g_motion_event_head + 1u) % STEPPER_EVENT_QUEUE_DEPTH);
    if (g_motion_event_count < STEPPER_EVENT_QUEUE_DEPTH) {
        g_motion_event_count++;
    } else {
        g_motion_event_tail = (uint8_t)((g_motion_event_tail + 1u) % STEPPER_EVENT_QUEUE_DEPTH);
    }
}

static bool stepper_service_timer_cb(repeating_timer_t *rt)
{
    (void)rt;

    for (uint32_t i = 0; i < MOTOR_COUNT; i++) {
        volatile motor_runtime_t *motor = &g_motor_runtime[i];
        if (!motor->running || !motor->enabled) {
            continue;
        }

        if (motor->pulse_high) {
            gpio_put(g_step_pins[i], 0);
            motor->pulse_high = false;
            if (motor->remaining_steps > 0u) {
                motor->remaining_steps--;
                if (motor->remaining_steps == 0u) {
                    motor->running = false;
                    stepper_push_motion_complete_event_from_timer((uint8_t)i, motor);
                    continue;
                }
            }
        }

        motor->tick_counter++;
        if (motor->tick_counter >= motor->tick_target) {
            motor->tick_counter = 0;
            gpio_put(g_step_pins[i], 1);
            motor->pulse_high = true;
        }
    }

    return true;
}

void stepper_task_init(void)
{
    memset((void *)g_motor_runtime, 0, sizeof(g_motor_runtime));

    for (uint32_t i = 0; i < MOTOR_COUNT; i++) {
        gpio_init(g_dir_pins[i]);
        gpio_set_dir(g_dir_pins[i], GPIO_OUT);
        gpio_put(g_dir_pins[i], 0);

        gpio_init(g_step_pins[i]);
        gpio_set_dir(g_step_pins[i], GPIO_OUT);
        gpio_put(g_step_pins[i], 0);

        gpio_init(g_en_pins[i]);
        gpio_set_dir(g_en_pins[i], GPIO_OUT);
        gpio_put(g_en_pins[i], 1);

        gpio_init(g_diag_pins[i]);
        gpio_set_dir(g_diag_pins[i], GPIO_IN);
        gpio_pull_up(g_diag_pins[i]);
    }

    gpio_init(EXPOSURE_SYNC_PIN);
    gpio_set_dir(EXPOSURE_SYNC_PIN, GPIO_IN);
    gpio_pull_down(EXPOSURE_SYNC_PIN);
    gpio_set_irq_enabled_with_callback(
        EXPOSURE_SYNC_PIN,
        GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL,
        true,
        &stepper_exposure_sync_irq_handler);

    (void)add_repeating_timer_us(-(int64_t)STEPPER_SERVICE_PERIOD_US, stepper_service_timer_cb, NULL, &g_stepper_timer);
}

bool stepper_task_set_enable(uint8_t motor_index, bool enabled)
{
    if (motor_index >= MOTOR_COUNT) {
        return false;
    }

    uint32_t irq_state = save_and_disable_interrupts();
    g_motor_runtime[motor_index].enabled = enabled;
    if (!enabled) {
        g_motor_runtime[motor_index].running = false;
        g_motor_runtime[motor_index].armed_on_sync = false;
        g_motor_runtime[motor_index].sync_rise_seen = false;
        g_motor_runtime[motor_index].pulse_high = false;
        g_motor_runtime[motor_index].remaining_steps = 0u;
        gpio_put(g_step_pins[motor_index], 0);
    }
    restore_interrupts(irq_state);

    gpio_put(g_en_pins[motor_index], enabled ? 0 : 1);
    return true;
}

bool stepper_task_start_move(uint8_t motor_index, bool direction, uint32_t steps, uint32_t interval_us)
{
    if (motor_index >= MOTOR_COUNT || steps == 0u || interval_us < STEPPER_SERVICE_PERIOD_US) {
        return false;
    }

    if (!g_motor_runtime[motor_index].enabled) {
        return false;
    }

    uint32_t tick_target = (interval_us + (STEPPER_SERVICE_PERIOD_US - 1u)) / STEPPER_SERVICE_PERIOD_US;
    if (tick_target == 0u) {
        tick_target = 1u;
    }

    uint32_t irq_state = save_and_disable_interrupts();
    motor_runtime_t *motor = (motor_runtime_t *)&g_motor_runtime[motor_index];
    motor->direction = direction;
    motor->remaining_steps = steps;
    motor->interval_us = interval_us;
    motor->tick_target = tick_target;
    motor->tick_counter = tick_target;
    motor->pulse_high = false;
    motor->armed_on_sync = false;
    motor->sync_rise_seen = false;
    motor->running = motor->enabled;
    restore_interrupts(irq_state);

    gpio_put(g_dir_pins[motor_index], direction ? 1 : 0);
    return true;
}

bool stepper_task_prepare_move_on_sync(uint8_t motor_index, bool direction, uint32_t steps, uint32_t interval_us)
{
    if (motor_index >= MOTOR_COUNT || steps == 0u || interval_us < STEPPER_SERVICE_PERIOD_US) {
        return false;
    }

    if (!g_motor_runtime[motor_index].enabled) {
        return false;
    }

    if (g_motor_runtime[motor_index].running) {
        return false;
    }

    uint32_t tick_target = (interval_us + (STEPPER_SERVICE_PERIOD_US - 1u)) / STEPPER_SERVICE_PERIOD_US;
    if (tick_target == 0u) {
        tick_target = 1u;
    }

    uint32_t irq_state = save_and_disable_interrupts();
    motor_runtime_t *motor = (motor_runtime_t *)&g_motor_runtime[motor_index];
    motor->direction = direction;
    motor->remaining_steps = steps;
    motor->interval_us = interval_us;
    motor->tick_target = tick_target;
    motor->tick_counter = tick_target;
    motor->pulse_high = false;
    motor->running = false;
    motor->armed_on_sync = true;
    motor->sync_rise_seen = false;
    restore_interrupts(irq_state);

    gpio_put(g_dir_pins[motor_index], direction ? 1 : 0);
    gpio_put(g_step_pins[motor_index], 0);
    return true;
}

bool stepper_task_stop(uint8_t motor_index)
{
    if (motor_index >= MOTOR_COUNT) {
        return false;
    }

    uint32_t irq_state = save_and_disable_interrupts();
    g_motor_runtime[motor_index].running = false;
    g_motor_runtime[motor_index].armed_on_sync = false;
    g_motor_runtime[motor_index].sync_rise_seen = false;
    g_motor_runtime[motor_index].pulse_high = false;
    g_motor_runtime[motor_index].remaining_steps = 0u;
    restore_interrupts(irq_state);

    gpio_put(g_step_pins[motor_index], 0);
    return true;
}

void stepper_task_get_status(uint8_t motor_index, stepper_status_t *status_out)
{
    if (motor_index >= MOTOR_COUNT || status_out == NULL) {
        return;
    }

    uint32_t irq_state = save_and_disable_interrupts();
    const volatile motor_runtime_t *motor = &g_motor_runtime[motor_index];
    status_out->enabled = motor->enabled;
    status_out->running = motor->running;
    status_out->direction = motor->direction;
    status_out->remaining_steps = motor->remaining_steps;
    status_out->configured_interval_us = motor->interval_us;
    restore_interrupts(irq_state);

    status_out->diag_state = gpio_get(g_diag_pins[motor_index]) ? 1u : 0u;
}

bool stepper_task_try_pop_motion_complete_event(stepper_motion_event_t *event_out)
{
    if (event_out == NULL) {
        return false;
    }

    stepper_motion_event_t event = {0};
    uint32_t irq_state = save_and_disable_interrupts();
    if (g_motion_event_count == 0u) {
        restore_interrupts(irq_state);
        return false;
    }
    event = *(const stepper_motion_event_t *)&g_motion_event_queue[g_motion_event_tail];
    g_motion_event_tail = (uint8_t)((g_motion_event_tail + 1u) % STEPPER_EVENT_QUEUE_DEPTH);
    g_motion_event_count--;
    restore_interrupts(irq_state);

    if (event.motor_index >= MOTOR_COUNT) {
        return false;
    }

    event.status.diag_state = gpio_get(g_diag_pins[event.motor_index]) ? 1u : 0u;
    *event_out = event;
    return true;
}
