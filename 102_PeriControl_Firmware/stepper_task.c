/*
 * Copyright (c) 2026 mr258876
 * SPDX-License-Identifier: MIT
 */

#include "stepper_task.h"

#include <string.h>

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

#include "stepper_task.pio.h"

#define STEPPER_MIN_INTERVAL_NS           750u
#define STEPPER_EVENT_QUEUE_DEPTH         8u
#define STEPPER_PIO                       pio1
#define STEPPER_PIO_IRQ_INDEX_BASE        0u
#define STEPPER_PIO_SM_FREQ_HZ            20000000u
#define STEPPER_PULSE_HIGH_CYCLES         10u
#define STEPPER_PULSE_FIXED_PERIOD_CYCLES 14u

typedef struct {
    bool enabled;
    bool running;
    bool armed_on_sync;
    bool direction;
    uint32_t remaining_steps;
    uint32_t interval_ns;
} motor_runtime_t;

static const uint8_t g_dir_pins[MOTOR_COUNT] = {M1_DIR_PIN, M2_DIR_PIN, M3_DIR_PIN};
static const uint8_t g_step_pins[MOTOR_COUNT] = {M1_STEP_PIN, M2_STEP_PIN, M3_STEP_PIN};
static const uint8_t g_en_pins[MOTOR_COUNT] = {M1_EN_PIN, M2_EN_PIN, M3_EN_PIN};
static const uint8_t g_diag_pins[MOTOR_COUNT] = {M1_DIAG_PIN, M2_DIAG_PIN, M3_DIAG_PIN};

static PIO g_stepper_pio = STEPPER_PIO;
static uint g_stepper_immediate_offset = 0u;
static uint g_stepper_sync_offset = 0u;
static volatile motor_runtime_t g_motor_runtime[MOTOR_COUNT];
static volatile stepper_motion_event_t g_motion_event_queue[STEPPER_EVENT_QUEUE_DEPTH];
static volatile uint8_t g_motion_event_head = 0u;
static volatile uint8_t g_motion_event_tail = 0u;
static volatile uint8_t g_motion_event_count = 0u;

static inline uint stepper_sm_index(uint8_t motor_index)
{
    return (uint)motor_index;
}

static void stepper_push_motion_complete_event(uint8_t motor_index,
                                               const volatile motor_runtime_t *motor)
{
    volatile stepper_motion_event_t *event = &g_motion_event_queue[g_motion_event_head];
    event->motor_index = motor_index;
    event->status.enabled = motor->enabled;
    event->status.running = false;
    event->status.direction = motor->direction;
    event->status.remaining_steps = 0u;
    event->status.configured_interval_ns = motor->interval_ns;
    event->status.diag_state = 0u;

    g_motion_event_head = (uint8_t)((g_motion_event_head + 1u) % STEPPER_EVENT_QUEUE_DEPTH);
    if (g_motion_event_count < STEPPER_EVENT_QUEUE_DEPTH) {
        g_motion_event_count++;
    } else {
        g_motion_event_tail = (uint8_t)((g_motion_event_tail + 1u) % STEPPER_EVENT_QUEUE_DEPTH);
    }
}

static void stepper_release_step_pin_to_gpio(uint8_t motor_index)
{
    gpio_set_function(g_step_pins[motor_index], GPIO_FUNC_SIO);
    gpio_set_dir(g_step_pins[motor_index], GPIO_OUT);
    gpio_put(g_step_pins[motor_index], 0);
}

static void stepper_stop_sm(uint8_t motor_index)
{
    uint sm = stepper_sm_index(motor_index);
    pio_sm_set_enabled(g_stepper_pio, sm, false);
    pio_sm_clear_fifos(g_stepper_pio, sm);
    pio_sm_restart(g_stepper_pio, sm);
    pio_interrupt_clear(g_stepper_pio, STEPPER_PIO_IRQ_INDEX_BASE + sm);
    stepper_release_step_pin_to_gpio(motor_index);
}

static float stepper_sm_clkdiv(void)
{
    return (float)clock_get_hz(clk_sys) / (float)STEPPER_PIO_SM_FREQ_HZ;
}

static bool stepper_compute_delay_count(uint32_t interval_ns, uint32_t *delay_count_out, uint32_t *effective_interval_ns_out)
{
    if (delay_count_out == NULL || effective_interval_ns_out == NULL || interval_ns < STEPPER_MIN_INTERVAL_NS) {
        return false;
    }

    uint64_t total_cycles = (((uint64_t)interval_ns * (uint64_t)STEPPER_PIO_SM_FREQ_HZ) + 999999999u) / 1000000000u;
    if (total_cycles <= STEPPER_PULSE_FIXED_PERIOD_CYCLES) {
        return false;
    }

    uint64_t delay_count = total_cycles - STEPPER_PULSE_FIXED_PERIOD_CYCLES;
    if (delay_count > UINT32_MAX) {
        return false;
    }

    uint64_t effective_interval_ns = (1000000000u * total_cycles) / STEPPER_PIO_SM_FREQ_HZ;
    if (effective_interval_ns > UINT32_MAX) {
        return false;
    }

    *delay_count_out = (uint32_t)delay_count;
    *effective_interval_ns_out = (uint32_t)effective_interval_ns;
    return true;
}

static bool stepper_start_sm(uint8_t motor_index, uint32_t steps, uint32_t delay_count, bool wait_for_sync)
{
    uint sm = stepper_sm_index(motor_index);
    uint offset = wait_for_sync ? g_stepper_sync_offset : g_stepper_immediate_offset;
    pio_sm_config cfg = wait_for_sync
        ? stepper_pulse_sync_program_get_default_config(offset)
        : stepper_pulse_immediate_program_get_default_config(offset);

    pio_sm_set_enabled(g_stepper_pio, sm, false);
    pio_sm_clear_fifos(g_stepper_pio, sm);
    pio_sm_restart(g_stepper_pio, sm);
    pio_interrupt_clear(g_stepper_pio, STEPPER_PIO_IRQ_INDEX_BASE + sm);

    pio_gpio_init(g_stepper_pio, g_step_pins[motor_index]);
    sm_config_set_set_pins(&cfg, g_step_pins[motor_index], 1u);
    sm_config_set_clkdiv(&cfg, stepper_sm_clkdiv());
    if (wait_for_sync) {
        sm_config_set_in_pins(&cfg, EXPOSURE_SYNC_PIN);
    }

    pio_sm_init(g_stepper_pio, sm, offset, &cfg);
    pio_sm_set_consecutive_pindirs(g_stepper_pio, sm, g_step_pins[motor_index], 1u, true);
    pio_sm_put_blocking(g_stepper_pio, sm, steps - 1u);
    pio_sm_put_blocking(g_stepper_pio, sm, delay_count);
    pio_sm_set_enabled(g_stepper_pio, sm, true);
    return true;
}

static void stepper_pio_irq_handler(void)
{
    uint32_t irq_state = save_and_disable_interrupts();
    if (pio_interrupt_get(g_stepper_pio, 3u)) {
        pio_interrupt_clear(g_stepper_pio, 3u);
        for (uint8_t i = 0; i < MOTOR_COUNT; i++) {
            motor_runtime_t *motor = (motor_runtime_t *)&g_motor_runtime[i];
            if (!motor->armed_on_sync || !motor->enabled) {
                continue;
            }

            motor->armed_on_sync = false;
            motor->running = true;
        }
    }

    for (uint8_t i = 0; i < MOTOR_COUNT; i++) {
        uint irq_index = STEPPER_PIO_IRQ_INDEX_BASE + stepper_sm_index(i);
        if (!pio_interrupt_get(g_stepper_pio, irq_index)) {
            continue;
        }

        pio_interrupt_clear(g_stepper_pio, irq_index);
        motor_runtime_t *motor = (motor_runtime_t *)&g_motor_runtime[i];
        if (!motor->running) {
            continue;
        }

        motor->running = false;
        motor->armed_on_sync = false;
        motor->remaining_steps = 0u;
        stepper_stop_sm(i);
        stepper_push_motion_complete_event(i, motor);
    }
    restore_interrupts(irq_state);
}

void stepper_task_init(void)
{
    memset((void *)g_motor_runtime, 0, sizeof(g_motor_runtime));
    g_motion_event_head = 0u;
    g_motion_event_tail = 0u;
    g_motion_event_count = 0u;

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

    g_stepper_immediate_offset = pio_add_program(g_stepper_pio, &stepper_pulse_immediate_program);
    g_stepper_sync_offset = pio_add_program(g_stepper_pio, &stepper_pulse_sync_program);

    pio_set_irq0_source_enabled(g_stepper_pio, pis_interrupt0, true);
    pio_set_irq0_source_enabled(g_stepper_pio, pis_interrupt1, true);
    pio_set_irq0_source_enabled(g_stepper_pio, pis_interrupt2, true);
    pio_set_irq0_source_enabled(g_stepper_pio, pis_interrupt3, true);
    irq_set_exclusive_handler(PIO1_IRQ_0, stepper_pio_irq_handler);
    irq_set_enabled(PIO1_IRQ_0, true);
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
        g_motor_runtime[motor_index].remaining_steps = 0u;
        stepper_stop_sm(motor_index);
    }
    restore_interrupts(irq_state);

    gpio_put(g_en_pins[motor_index], enabled ? 0 : 1);
    return true;
}

static bool stepper_task_begin_move(uint8_t motor_index,
                                    bool direction,
                                    uint32_t steps,
                                    uint32_t interval_ns,
                                    bool wait_for_sync)
{
    if (motor_index >= MOTOR_COUNT || steps == 0u) {
        return false;
    }

    if (!g_motor_runtime[motor_index].enabled || g_motor_runtime[motor_index].running || g_motor_runtime[motor_index].armed_on_sync) {
        return false;
    }

    uint32_t delay_count = 0u;
    uint32_t effective_interval_ns = 0u;
    if (!stepper_compute_delay_count(interval_ns, &delay_count, &effective_interval_ns)) {
        return false;
    }

    uint32_t irq_state = save_and_disable_interrupts();
    motor_runtime_t *motor = (motor_runtime_t *)&g_motor_runtime[motor_index];
    motor->direction = direction;
    motor->remaining_steps = steps;
    motor->interval_ns = effective_interval_ns;
    motor->armed_on_sync = wait_for_sync;
    motor->running = !wait_for_sync;
    restore_interrupts(irq_state);

    gpio_put(g_dir_pins[motor_index], direction ? 1u : 0u);
    if (!wait_for_sync) {
        busy_wait_us_32(1u);
    }

    if (!stepper_start_sm(motor_index, steps, delay_count, wait_for_sync)) {
        irq_state = save_and_disable_interrupts();
        motor->running = false;
        motor->armed_on_sync = false;
        motor->remaining_steps = 0u;
        restore_interrupts(irq_state);
        stepper_stop_sm(motor_index);
        return false;
    }

    return true;
}

bool stepper_task_start_move(uint8_t motor_index, bool direction, uint32_t steps, uint32_t interval_ns)
{
    return stepper_task_begin_move(motor_index, direction, steps, interval_ns, false);
}

bool stepper_task_prepare_move_on_sync(uint8_t motor_index, bool direction, uint32_t steps, uint32_t interval_ns)
{
    return stepper_task_begin_move(motor_index, direction, steps, interval_ns, true);
}

bool stepper_task_stop(uint8_t motor_index)
{
    if (motor_index >= MOTOR_COUNT) {
        return false;
    }

    uint32_t irq_state = save_and_disable_interrupts();
    g_motor_runtime[motor_index].running = false;
    g_motor_runtime[motor_index].armed_on_sync = false;
    g_motor_runtime[motor_index].remaining_steps = 0u;
    restore_interrupts(irq_state);

    stepper_stop_sm(motor_index);
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
    status_out->configured_interval_ns = motor->interval_ns;
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

bool stepper_task_has_active_motion(void)
{
    uint32_t irq_state = save_and_disable_interrupts();
    bool active = false;
    for (uint32_t i = 0; i < MOTOR_COUNT; i++) {
        if (g_motor_runtime[i].running || g_motor_runtime[i].armed_on_sync) {
            active = true;
            break;
        }
    }
    restore_interrupts(irq_state);
    return active;
}
