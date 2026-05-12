/*
 * Copyright (c) 2026 mr258876
 * SPDX-License-Identifier: MIT
 */

#include "command_dispatcher.h"

#include <string.h>

#include "exposure_sync.h"
#include "pico/flash.h"
#include "pico/stdlib.h"

#include "hardware/clocks.h"

#include "Pinouts.h"
#include "led_pwm.h"
#include "pericontrol_defaults.h"
#include "persistent_params.h"
#include "stepper_task.h"
#include "tmc2209_bus.h"

#define STATUS_PAYLOAD_LEN  (4u + (LED_CHANNEL_COUNT * 2u) + (MOTOR_COUNT * 14u) + 8u)
#define ILLUMINATION_STATUS_PAYLOAD_LEN ((LED_CHANNEL_COUNT * 2u) + 4u + (LED_CHANNEL_COUNT * 4u))
#define MOTION_STATUS_PAYLOAD_LEN (MOTOR_COUNT * 14u)
#define TMC2209_PRESENCE_ABSENT 0x40u
#define TMC2209_PROBE_REG_ADDR  0x00u
#define TMC2209_INIT_IRUN        3u
#define TMC2209_INIT_IHOLD       3u

static prism_params_t g_params;
static bool g_params_dirty = false;
static uint64_t g_params_save_deadline_us = 0;
static uint8_t g_tmc_presence_status[MOTOR_COUNT] = {0};

static void encode_u16_le(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)(value & 0xFFu);
    out[1] = (uint8_t)((value >> 8) & 0xFFu);
}

static void encode_u32_le(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)(value & 0xFFu);
    out[1] = (uint8_t)((value >> 8) & 0xFFu);
    out[2] = (uint8_t)((value >> 16) & 0xFFu);
    out[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static uint16_t decode_u16_le(const uint8_t *in)
{
    return (uint16_t)(((uint16_t)in[0]) | ((uint16_t)in[1] << 8));
}

static uint32_t decode_u32_le(const uint8_t *in)
{
    return ((uint32_t)in[0]) |
           ((uint32_t)in[1] << 8) |
           ((uint32_t)in[2] << 16) |
           ((uint32_t)in[3] << 24);
}

static bool system_clock_supported_khz(uint32_t sys_clock_khz)
{
    if (sys_clock_khz < PRISM_MIN_SYS_CLOCK_KHZ || sys_clock_khz > PRISM_MAX_SYS_CLOCK_KHZ) {
        return false;
    }

    unsigned int vco = 0;
    unsigned int post_div1 = 0;
    unsigned int post_div2 = 0;
    return check_sys_clock_khz(sys_clock_khz, &vco, &post_div1, &post_div2);
}

static bool apply_system_clock_khz(uint32_t sys_clock_khz)
{
    if (!system_clock_supported_khz(sys_clock_khz)) {
        return false;
    }
    return set_sys_clock_khz(sys_clock_khz, false);
}

static void schedule_params_save(void)
{
    g_params_dirty = true;
    g_params_save_deadline_us = time_us_64() + ((uint64_t)PRISM_PARAM_SAVE_DEBOUNCE_MS * 1000u);
}

void command_dispatcher_background_step(void)
{
    if (!g_params_dirty) {
        return;
    }

    uint64_t now = time_us_64();
    if ((int64_t)(now - g_params_save_deadline_us) < 0) {
        return;
    }

    if (prism_params_save(&g_params)) {
        g_params_dirty = false;
        g_params_save_deadline_us = 0;
    } else {
        g_params_save_deadline_us = now + ((uint64_t)PRISM_PARAM_SAVE_DEBOUNCE_MS * 1000u);
    }
}

static bool apply_led_params(void)
{
    led_pwm_set_wrap(g_params.led_pwm_wrap);
    led_pwm_set_levels(g_params.led_level, LED_CHANNEL_COUNT);
    exposure_sync_refresh_outputs();
    return true;
}

static void apply_tmc_init_current(void)
{
    for (uint8_t i = 0; i < MOTOR_COUNT; i++) {
        g_params.motor_irun[i] = TMC2209_INIT_IRUN;
        g_params.motor_ihold[i] = TMC2209_INIT_IHOLD;
    }
}

static bool apply_motor_params(uint8_t motor_index)
{
    uint32_t probe_value = 0;
    if (!tmc2209_read_register(motor_index, TMC2209_PROBE_REG_ADDR, &probe_value)) {
        g_tmc_presence_status[motor_index] = TMC2209_PRESENCE_ABSENT;
        return true;
    }

    g_tmc_presence_status[motor_index] = 0u;
    return tmc2209_apply_basic_config(motor_index,
                                      g_params.motor_irun[motor_index],
                                      g_params.motor_ihold[motor_index],
                                      g_params.motor_microsteps[motor_index],
                                      g_params.motor_stealthchop_enable[motor_index] != 0u);
}

static bool apply_all_motor_params(void)
{
    for (uint8_t i = 0; i < MOTOR_COUNT; i++) {
        if (!apply_motor_params(i)) {
            return false;
        }
    }
    return true;
}

static void push_status_payload(control_response_t *rsp)
{
    exposure_sync_status_t sync_status = {0};
    exposure_sync_get_status(&sync_status);

    rsp->payload_len = STATUS_PAYLOAD_LEN;
    uint8_t *out = rsp->payload;
    encode_u32_le(out, g_params.sys_clock_khz);
    out += 4;

    for (uint32_t i = 0; i < LED_CHANNEL_COUNT; i++) {
        encode_u16_le(out, g_params.led_level[i]);
        out += 2;
    }

    for (uint8_t i = 0; i < MOTOR_COUNT; i++) {
        stepper_status_t status = {0};
        stepper_task_get_status(i, &status);
        *out++ = i;
        *out++ = status.enabled ? 1u : 0u;
        *out++ = status.running ? 1u : 0u;
        *out++ = status.direction ? 1u : 0u;
        *out++ = (uint8_t)status.diag_state;
        *out++ = g_tmc_presence_status[i];
        encode_u32_le(out, status.configured_interval_ns);
        out += 4;
        encode_u32_le(out, status.remaining_steps);
        out += 4;
    }

    *out++ = sync_status.steady_mask;
    *out++ = sync_status.sync_mask;
    *out++ = sync_status.active;
    *out++ = 0u;
    encode_u32_le(out, 0u);
}

static void push_illumination_status_payload(control_response_t *rsp)
{
    exposure_sync_status_t sync_status = {0};
    exposure_sync_get_status(&sync_status);

    rsp->payload_len = ILLUMINATION_STATUS_PAYLOAD_LEN;
    uint8_t *out = rsp->payload;
    for (uint32_t i = 0; i < LED_CHANNEL_COUNT; i++) {
        encode_u16_le(out, g_params.led_level[i]);
        out += 2;
    }

    *out++ = sync_status.steady_mask;
    *out++ = sync_status.sync_mask;
    *out++ = sync_status.active;
    *out++ = 0u;

    for (uint32_t i = 0; i < LED_CHANNEL_COUNT; i++) {
        encode_u32_le(out, sync_status.pulse_clk[i]);
        out += 4;
    }
}

static void push_motion_status_payload(control_response_t *rsp)
{
    rsp->payload_len = MOTION_STATUS_PAYLOAD_LEN;
    uint8_t *out = rsp->payload;

    for (uint8_t i = 0; i < MOTOR_COUNT; i++) {
        stepper_status_t status = {0};
        stepper_task_get_status(i, &status);
        *out++ = i;
        *out++ = status.enabled ? 1u : 0u;
        *out++ = status.running ? 1u : 0u;
        *out++ = status.direction ? 1u : 0u;
        *out++ = (uint8_t)status.diag_state;
        *out++ = g_tmc_presence_status[i];
        encode_u32_le(out, status.configured_interval_ns);
        out += 4;
        encode_u32_le(out, status.remaining_steps);
        out += 4;
    }
}

static void push_single_motion_status_payload(control_response_t *rsp,
                                             uint8_t motor_index,
                                             const stepper_status_t *status)
{
    rsp->payload_len = MOTION_STATUS_PAYLOAD_LEN / MOTOR_COUNT;
    uint8_t *out = rsp->payload;
    *out++ = motor_index;
    *out++ = status->enabled ? 1u : 0u;
    *out++ = status->running ? 1u : 0u;
    *out++ = status->direction ? 1u : 0u;
    *out++ = (uint8_t)status->diag_state;
    *out++ = g_tmc_presence_status[motor_index];
    encode_u32_le(out, status->configured_interval_ns);
    out += 4;
    encode_u32_le(out, status->remaining_steps);
}

static void process_get_param_by_hash(const control_command_t *cmd, control_response_t *rsp)
{
    if (cmd->payload_len != 4u) {
        rsp->status = CONTROL_STATUS_PAYLOAD_INVALID;
        return;
    }

    uint32_t key_hash = decode_u32_le(cmd->payload);
    uint8_t param_type = 0;
    uint8_t param_len = 0;
    uint8_t value[32] = {0};

    if (!prism_param_get_by_hash(&g_params, key_hash, &param_type, &param_len, value)) {
        rsp->status = CONTROL_STATUS_PARAM_NOT_FOUND;
        return;
    }

    encode_u32_le(rsp->payload, key_hash);
    rsp->payload[4] = param_type;
    rsp->payload[5] = param_len;
    memcpy(&rsp->payload[6], value, param_len);
    rsp->payload_len = (uint16_t)(6u + param_len);
}

static void process_set_param_by_hash(const control_command_t *cmd, control_response_t *rsp)
{
    if (cmd->payload_len < 6u) {
        rsp->status = CONTROL_STATUS_PAYLOAD_INVALID;
        return;
    }

    uint32_t key_hash = decode_u32_le(cmd->payload);
    uint8_t param_type = cmd->payload[4];
    uint8_t param_len = cmd->payload[5];
    if ((uint16_t)(6u + param_len) != cmd->payload_len) {
        rsp->status = CONTROL_STATUS_PAYLOAD_INVALID;
        return;
    }

    uint8_t expected_type = 0;
    uint8_t expected_len = 0;
    if (!prism_param_meta_by_hash(key_hash, &expected_type, &expected_len)) {
        rsp->status = CONTROL_STATUS_PARAM_NOT_FOUND;
        return;
    }
    if (param_type != expected_type) {
        rsp->status = CONTROL_STATUS_PARAM_TYPE_MISMATCH;
        return;
    }
    if (param_len != expected_len) {
        rsp->status = CONTROL_STATUS_PARAM_LEN_INVALID;
        return;
    }

    prism_params_t previous = g_params;
    if (!prism_param_set_by_hash(&g_params, key_hash, param_type, param_len, &cmd->payload[6])) {
        rsp->status = CONTROL_STATUS_PAYLOAD_INVALID;
        return;
    }

    bool apply_ok = true;
    bool clock_changed = (key_hash == prism_param_hash_key("prism.sys_clock_khz"));
    if (clock_changed) {
        if (stepper_task_has_active_motion()) {
            g_params = previous;
            rsp->status = CONTROL_STATUS_BUSY;
            return;
        }
        apply_ok = apply_system_clock_khz(g_params.sys_clock_khz);
        if (apply_ok) {
            tmc2209_bus_init();
            apply_ok = apply_led_params() && apply_all_motor_params();
        }
    } else if ((key_hash == prism_param_hash_key("prism.led_pwm_wrap")) ||
               (key_hash == prism_param_hash_key("prism.led1.level")) ||
               (key_hash == prism_param_hash_key("prism.led2.level")) ||
               (key_hash == prism_param_hash_key("prism.led3.level")) ||
               (key_hash == prism_param_hash_key("prism.led4.level"))) {
        apply_ok = apply_led_params();
    } else {
        for (uint8_t i = 0; i < MOTOR_COUNT; i++) {
            if ((key_hash == prism_param_hash_key(i == 0 ? "prism.motor1.irun" : (i == 1 ? "prism.motor2.irun" : "prism.motor3.irun"))) ||
                (key_hash == prism_param_hash_key(i == 0 ? "prism.motor1.ihold" : (i == 1 ? "prism.motor2.ihold" : "prism.motor3.ihold"))) ||
                (key_hash == prism_param_hash_key(i == 0 ? "prism.motor1.microsteps" : (i == 1 ? "prism.motor2.microsteps" : "prism.motor3.microsteps"))) ||
                (key_hash == prism_param_hash_key(i == 0 ? "prism.motor1.stealthchop_enable" : (i == 1 ? "prism.motor2.stealthchop_enable" : "prism.motor3.stealthchop_enable")))) {
                apply_ok = apply_motor_params(i);
                break;
            }
        }
    }

    if (!apply_ok) {
        g_params = previous;
        if (clock_changed) {
            (void)apply_system_clock_khz(g_params.sys_clock_khz);
            tmc2209_bus_init();
        }
        (void)apply_led_params();
        apply_tmc_init_current();
        (void)apply_all_motor_params();
        rsp->status = CONTROL_STATUS_HW_ERROR;
        return;
    }

    schedule_params_save();

    encode_u32_le(rsp->payload, key_hash);
    rsp->payload[4] = param_type;
    rsp->payload[5] = param_len;
    memcpy(&rsp->payload[6], &cmd->payload[6], param_len);
    rsp->payload_len = (uint16_t)(6u + param_len);
}

static void process_set_motor_enable(const control_command_t *cmd, control_response_t *rsp)
{
    if (cmd->payload_len != 2u) {
        rsp->status = CONTROL_STATUS_PAYLOAD_INVALID;
        return;
    }

    uint8_t motor_index = cmd->payload[0];
    bool enabled = cmd->payload[1] != 0u;
    if (!stepper_task_set_enable(motor_index, enabled)) {
        rsp->status = CONTROL_STATUS_RANGE_INVALID;
        return;
    }

    rsp->payload[0] = motor_index;
    rsp->payload[1] = enabled ? 1u : 0u;
    rsp->payload_len = 2u;
}

static void process_move_motor_steps(const control_command_t *cmd, control_response_t *rsp)
{
    if (cmd->payload_len != 10u) {
        rsp->status = CONTROL_STATUS_PAYLOAD_INVALID;
        return;
    }

    uint8_t motor_index = cmd->payload[0];
    bool direction = cmd->payload[1] != 0u;
    uint32_t steps = decode_u32_le(&cmd->payload[2]);
    uint32_t interval_ns = decode_u32_le(&cmd->payload[6]);
    if (!stepper_task_start_move(motor_index, direction, steps, interval_ns)) {
        rsp->status = CONTROL_STATUS_RANGE_INVALID;
        return;
    }

    rsp->payload[0] = motor_index;
    rsp->payload[1] = direction ? 1u : 0u;
    memcpy(&rsp->payload[2], &cmd->payload[2], 8u);
    rsp->payload_len = 10u;
}

static void process_prepare_motor_on_sync(const control_command_t *cmd, control_response_t *rsp)
{
    if (cmd->payload_len != 10u) {
        rsp->status = CONTROL_STATUS_PAYLOAD_INVALID;
        return;
    }

    uint8_t motor_index = cmd->payload[0];
    bool direction = cmd->payload[1] != 0u;
    uint32_t steps = decode_u32_le(&cmd->payload[2]);
    uint32_t interval_ns = decode_u32_le(&cmd->payload[6]);
    if (!stepper_task_prepare_move_on_sync(motor_index, direction, steps, interval_ns)) {
        rsp->status = CONTROL_STATUS_RANGE_INVALID;
        return;
    }

    rsp->payload[0] = motor_index;
    rsp->payload[1] = direction ? 1u : 0u;
    memcpy(&rsp->payload[2], &cmd->payload[2], 8u);
    rsp->payload_len = 10u;
}

static void process_stop_motor(const control_command_t *cmd, control_response_t *rsp)
{
    if (cmd->payload_len != 1u) {
        rsp->status = CONTROL_STATUS_PAYLOAD_INVALID;
        return;
    }

    uint8_t motor_index = cmd->payload[0];
    if (!stepper_task_stop(motor_index)) {
        rsp->status = CONTROL_STATUS_RANGE_INVALID;
        return;
    }

    rsp->payload[0] = motor_index;
    rsp->payload_len = 1u;
}

static void process_read_tmc_reg(const control_command_t *cmd, control_response_t *rsp)
{
    if (cmd->payload_len != 2u) {
        rsp->status = CONTROL_STATUS_PAYLOAD_INVALID;
        return;
    }

    uint8_t motor_index = cmd->payload[0];
    uint8_t reg_addr = cmd->payload[1];
    uint32_t value = 0;
    if (!tmc2209_read_register(motor_index, reg_addr, &value)) {
        rsp->status = CONTROL_STATUS_HW_ERROR;
        return;
    }

    rsp->payload[0] = motor_index;
    rsp->payload[1] = reg_addr;
    encode_u32_le(&rsp->payload[2], value);
    rsp->payload_len = 6u;
}

static void process_write_tmc_reg(const control_command_t *cmd, control_response_t *rsp)
{
    if (cmd->payload_len != 6u) {
        rsp->status = CONTROL_STATUS_PAYLOAD_INVALID;
        return;
    }

    uint8_t motor_index = cmd->payload[0];
    uint8_t reg_addr = cmd->payload[1];
    uint32_t value = decode_u32_le(&cmd->payload[2]);
    if (!tmc2209_write_register(motor_index, reg_addr, value)) {
        rsp->status = CONTROL_STATUS_HW_ERROR;
        return;
    }

    memcpy(rsp->payload, cmd->payload, cmd->payload_len);
    rsp->payload_len = cmd->payload_len;
}

static void process_apply_motor_config(const control_command_t *cmd, control_response_t *rsp)
{
    if (cmd->payload_len != 1u) {
        rsp->status = CONTROL_STATUS_PAYLOAD_INVALID;
        return;
    }

    uint8_t motor_index = cmd->payload[0];
    if (motor_index >= MOTOR_COUNT) {
        rsp->status = CONTROL_STATUS_RANGE_INVALID;
        return;
    }
    if (!apply_motor_params(motor_index)) {
        rsp->status = CONTROL_STATUS_HW_ERROR;
        return;
    }

    rsp->payload[0] = motor_index;
    rsp->payload_len = 1u;
}

static void process_set_led_levels(const control_command_t *cmd, control_response_t *rsp)
{
    if (cmd->payload_len != 8u) {
        rsp->status = CONTROL_STATUS_PAYLOAD_INVALID;
        return;
    }

    for (uint32_t i = 0; i < LED_CHANNEL_COUNT; i++) {
        g_params.led_level[i] = decode_u16_le(&cmd->payload[i * 2u]);
        if (g_params.led_level[i] > g_params.led_pwm_wrap) {
            g_params.led_level[i] = g_params.led_pwm_wrap;
        }
    }

    if (!apply_led_params()) {
        rsp->status = CONTROL_STATUS_HW_ERROR;
        return;
    }

    schedule_params_save();
    memcpy(rsp->payload, cmd->payload, 8u);
    rsp->payload_len = 8u;
}

static void process_set_steady_illumination(const control_command_t *cmd, control_response_t *rsp)
{
    if (cmd->payload_len != 4u) {
        rsp->status = CONTROL_STATUS_PAYLOAD_INVALID;
        return;
    }

    uint8_t steady_mask = cmd->payload[0];
    if (!exposure_sync_set_steady_mask(steady_mask)) {
        rsp->status = CONTROL_STATUS_RANGE_INVALID;
        return;
    }

    rsp->payload[0] = steady_mask;
    rsp->payload[1] = 0u;
    rsp->payload[2] = 0u;
    rsp->payload[3] = 0u;
    rsp->payload_len = 4u;
}

static void process_config_led_sync(const control_command_t *cmd, control_response_t *rsp)
{
    if (cmd->payload_len != 4u) {
        rsp->status = CONTROL_STATUS_PAYLOAD_INVALID;
        return;
    }

    uint8_t sync_mask = cmd->payload[0];
    if (!exposure_sync_set_sync_mask(sync_mask)) {
        rsp->status = CONTROL_STATUS_RANGE_INVALID;
        return;
    }

    memcpy(rsp->payload, cmd->payload, 4u);
    rsp->payload_len = 4u;
}

static void process_set_sync_pulse_clk(const control_command_t *cmd, control_response_t *rsp)
{
    if (cmd->payload_len != 16u) {
        rsp->status = CONTROL_STATUS_PAYLOAD_INVALID;
        return;
    }

    uint32_t pulse_clk[LED_CHANNEL_COUNT] = {0};
    for (uint32_t i = 0; i < LED_CHANNEL_COUNT; i++) {
        pulse_clk[i] = decode_u32_le(&cmd->payload[i * 4u]);
    }
    if (!exposure_sync_set_pulse_clk(pulse_clk)) {
        rsp->status = CONTROL_STATUS_RANGE_INVALID;
        return;
    }

    memcpy(rsp->payload, cmd->payload, 16u);
    rsp->payload_len = 16u;
}

bool command_dispatcher_init(void)
{
    flash_safe_execute_core_init();

    prism_params_set_defaults(&g_params);
    (void)prism_params_load(&g_params);
    if (!apply_system_clock_khz(g_params.sys_clock_khz)) {
        prism_params_set_defaults(&g_params);
        (void)apply_system_clock_khz(g_params.sys_clock_khz);
    }

    apply_tmc_init_current();

    for (uint8_t i = 0; i < MOTOR_COUNT; i++) {
        g_params.motor_microsteps[i] = 256u;
    }

    led_pwm_init(g_params.led_pwm_wrap);
    exposure_sync_init();
    apply_led_params();

    stepper_task_init();
    tmc2209_bus_init();
    for (uint8_t i = 0; i < MOTOR_COUNT; i++) {
        (void)stepper_task_set_enable(i, false);
    }

    return apply_all_motor_params();
}

void command_dispatcher_process(const control_command_t *cmd, control_response_t *rsp)
{
    rsp->status = CONTROL_STATUS_OK;
    rsp->opcode = cmd->type;
    rsp->payload_len = 0u;

    switch (cmd->type) {
        case CONTROL_CMD_GET_PARAM_BY_HASH:
            process_get_param_by_hash(cmd, rsp);
            break;
        case CONTROL_CMD_SET_PARAM_BY_HASH:
            process_set_param_by_hash(cmd, rsp);
            break;
        case CONTROL_CMD_GET_STATUS:
            push_status_payload(rsp);
            break;
        case CONTROL_CMD_GET_ILLUMINATION_STATUS:
            push_illumination_status_payload(rsp);
            break;
        case CONTROL_CMD_GET_MOTION_STATUS:
            push_motion_status_payload(rsp);
            break;
        case CONTROL_CMD_SET_LED_LEVELS:
            process_set_led_levels(cmd, rsp);
            break;
        case CONTROL_CMD_SET_MOTOR_ENABLE:
            process_set_motor_enable(cmd, rsp);
            break;
        case CONTROL_CMD_MOVE_MOTOR_STEPS:
            process_move_motor_steps(cmd, rsp);
            break;
        case CONTROL_CMD_STOP_MOTOR:
            process_stop_motor(cmd, rsp);
            break;
        case CONTROL_CMD_READ_TMC_REG:
            process_read_tmc_reg(cmd, rsp);
            break;
        case CONTROL_CMD_WRITE_TMC_REG:
            process_write_tmc_reg(cmd, rsp);
            break;
        case CONTROL_CMD_PREPARE_MOTOR_ON_SYNC:
            process_prepare_motor_on_sync(cmd, rsp);
            break;
        case CONTROL_CMD_APPLY_MOTOR_CONFIG:
            process_apply_motor_config(cmd, rsp);
            break;
        case CONTROL_CMD_CONFIG_LED_SYNC:
            process_config_led_sync(cmd, rsp);
            break;
        case CONTROL_CMD_SET_STEADY_ILLUMINATION:
            process_set_steady_illumination(cmd, rsp);
            break;
        case CONTROL_CMD_SET_SYNC_PULSE_CLK:
            process_set_sync_pulse_clk(cmd, rsp);
            break;
        default:
            rsp->status = CONTROL_STATUS_BAD_FRAME;
            break;
    }
}

bool command_dispatcher_try_pop_async_response(control_response_t *rsp)
{
    if (rsp == NULL) {
        return false;
    }

    stepper_motion_event_t event = {0};
    if (!stepper_task_try_pop_motion_complete_event(&event)) {
        return false;
    }

    rsp->status = CONTROL_STATUS_OK;
    rsp->opcode = CONTROL_EVT_MOTION_COMPLETE;
    rsp->payload_len = 0u;
    push_single_motion_status_payload(rsp, event.motor_index, &event.status);
    return true;
}
