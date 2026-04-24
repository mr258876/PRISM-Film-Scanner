/*
 * Copyright (c) 2026 mr258876
 * SPDX-License-Identifier: MIT
 */

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/flash.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/pwm.h"
#include "hardware/gpio.h"

#include <string.h>

#include "prism.pio.h"
#include "pericontrol_link.h"
#include "Pinouts.h"
#include "AD9826_SPI/AD9826_SPI.h"
#include "persistent_params.h"
#include "prism_defaults.h"
#include "usb_task.h"

#define PRISM_SH_TICKS_PER_LINE 11UL
#define PRISM_PIXEL_CYCLES_PER_LINE 3800UL
#define PRISM_CDS_CYCLES_PER_LINE 3800UL
#define PRISM_FIFO_CYCLES_PER_LINE 7601UL
#define PRISM_BYTES_PER_LINE (15204UL + 2UL)  // Extra 2 bytes when judging manual flush
#define PRISM_MIN_SYS_CLOCK_KHZ 30000u
#define PRISM_MAX_SYS_CLOCK_KHZ 200000u
#define PRISM_PARAMS_SAVE_DEBOUNCE_MS 1000u
#define BOARD102_LED_CHANNEL_COUNT 4u
#define BOARD102_VALID_LED_MASK ((1u << BOARD102_LED_CHANNEL_COUNT) - 1u)
#define BOARD102_MOTOR_COUNT 3u
#define BOARD102_MIN_STEP_INTERVAL_US 10u
#define BOARD102_MIN_SYNC_PULSE_CLK 2u
#define BOARD102_STATUS_MOTION_OFFSET 12u
#define BOARD102_STATUS_MOTOR_ENTRY_LEN 12u
#define BOARD102_STATUS_MOTOR_COUNT BOARD102_MOTOR_COUNT
#define BOARD102_STATUS_MOTION_LEN (BOARD102_STATUS_MOTOR_ENTRY_LEN * BOARD102_STATUS_MOTOR_COUNT)
#define BOARD102_ILLUMINATION_STATUS_LEN 28u
#define BOARD102_MOTION_EVENT_POLL_INTERVAL_US 20000u

static prism_params_t g_params;
static bool g_params_dirty = false;
static uint64_t g_params_save_deadline_us = 0;
static uint32_t g_scan_lines = 1;
static volatile bool g_scan_active = false;
static volatile bool g_scan_done_pending = false;
static volatile uint32_t g_scan_target_lines = 0;
static volatile uint32_t g_scan_completed_lines = 0;

#define SCAN_DMA_SM_COUNT 3u
#define SCAN_DMA_BANKS 2u
#define SCAN_DMA_CHUNK_LINES 64u
#define WARMUP_DMA_CHUNK_LINES 64u

static int g_scan_dma_chan[SCAN_DMA_SM_COUNT] = {-1, -1, -1};
static uint32_t g_scan_dma_irq_mask = 0;
static uint32_t g_scan_dma_active_mask = 0;
static volatile uint8_t g_scan_dma_running_bank = 0;
static volatile uint8_t g_scan_dma_pending_bank = 1;
static volatile uint32_t g_scan_prepared_lines = 0;
static volatile uint32_t g_scan_bank_lines[SCAN_DMA_BANKS] = {0, 0};
static volatile bool g_warmup_active = false;
static bool g_board102_motion_seen = false;
static bool g_board102_motion_running[BOARD102_MOTOR_COUNT] = {false};
static uint64_t g_board102_motion_poll_deadline_us = 0u;
static uint32_t g_warmup_dma_buf[WARMUP_DMA_CHUNK_LINES];
static uint g_line_sig_offset = 0;
static uint g_cds_line_offset = 0;
static uint g_fifo_line_offset = 0;
static uint g_ifclk_offset = 0;
static uint32_t g_scan_dma_buf_sm0[SCAN_DMA_BANKS][SCAN_DMA_CHUNK_LINES];
static uint32_t g_scan_dma_buf_sm1[SCAN_DMA_BANKS][SCAN_DMA_CHUNK_LINES];
static uint32_t g_scan_dma_buf_sm2[SCAN_DMA_BANKS][SCAN_DMA_CHUNK_LINES];

static void timing_gen_reinit(void);
static void warmup_dma_fill_buffer(void);

static void schedule_params_save(void)
{
    g_params_dirty = true;
    g_params_save_deadline_us = time_us_64() + ((uint64_t)PRISM_PARAMS_SAVE_DEBOUNCE_MS * 1000u);
}

static void flush_pending_params_save(void)
{
    if (!g_params_dirty || g_scan_active || g_warmup_active) {
        return;
    }

    uint64_t now = time_us_64();
    if ((int64_t)(now - g_params_save_deadline_us) < 0) {
        return;
    }

    if (prism_params_save(&g_params)) {
        g_params_dirty = false;
        g_params_save_deadline_us = 0;
        return;
    }

    g_params_save_deadline_us = now + ((uint64_t)PRISM_PARAMS_SAVE_DEBOUNCE_MS * 1000u);
}


static uint8_t map_pericontrol_link_result_to_usb_status(pericontrol_link_result_t result)
{
    switch (result)
    {
    case PERICONTROL_LINK_TIMEOUT:
        return USB_STATUS_SUBORDINATE_TIMEOUT;
    case PERICONTROL_LINK_BAD_RESPONSE:
    case PERICONTROL_LINK_CRC_MISMATCH:
        return USB_STATUS_SUBORDINATE_LINK_ERROR;
    case PERICONTROL_LINK_INVALID_ARGUMENT:
        return USB_STATUS_PAYLOAD_INVALID;
    default:
        return USB_STATUS_BAD_FRAME;
    }
}

static uint8_t map_pericontrol_status_to_usb_status(uint8_t pericontrol_status)
{
    switch (pericontrol_status)
    {
    case 0x00:
        return USB_STATUS_OK;
    case 0xE6:
    case 0xEA:
        return USB_STATUS_BUSY;
    case 0xE8:
    case 0xE9:
        return USB_STATUS_PAYLOAD_INVALID;
    case 0xEB:
        return USB_STATUS_RANGE_INVALID;
    case 0xEC:
        return USB_STATUS_HW_ERROR;
    default:
        return USB_STATUS_BAD_FRAME;
    }
}

static bool pericontrol_command_roundtrip(uint8_t pericontrol_opcode,
                                          const uint8_t *tx_payload,
                                          uint16_t tx_payload_len,
                                          usb_response_t *rsp,
                                          uint8_t *rx_payload,
                                          uint16_t *rx_payload_len)
{
    uint8_t pericontrol_status = 0u;
    pericontrol_link_result_t link_result = pericontrol_link_transceive(pericontrol_opcode,
                                                                        tx_payload,
                                                                        tx_payload_len,
                                                                        &pericontrol_status,
                                                                        rx_payload_len,
                                                                        rx_payload,
                                                                        PERICONTROL_MAX_PAYLOAD);
    if (link_result != PERICONTROL_LINK_OK)
    {
        rsp->status = map_pericontrol_link_result_to_usb_status(link_result);
        return false;
    }

    rsp->status = map_pericontrol_status_to_usb_status(pericontrol_status);
    return rsp->status == USB_STATUS_OK;
}

static bool fetch_board102_illumination_state(usb_response_t *rsp,
                                              uint8_t *status_out,
                                              uint16_t *status_len_out)
{
    return pericontrol_command_roundtrip(PERICONTROL_CMD_GET_ILLUMINATION_STATUS,
                                         NULL,
                                         0u,
                                         rsp,
                                         status_out,
                                         status_len_out);
}

static bool fetch_board102_motion_state(usb_response_t *rsp,
                                        uint8_t *status_out,
                                        uint16_t *status_len_out)
{
    return pericontrol_command_roundtrip(PERICONTROL_CMD_GET_MOTION_STATUS,
                                         NULL,
                                         0u,
                                         rsp,
                                         status_out,
                                         status_len_out);
}

static bool validate_led_mask_payload(const uint8_t *payload,
                                      uint8_t *mask_out,
                                      usb_response_t *rsp)
{
    if (payload[1] != 0u || payload[2] != 0u || payload[3] != 0u)
    {
        rsp->status = USB_STATUS_PAYLOAD_INVALID;
        return false;
    }

    uint8_t mask = payload[0];
    if ((mask & (uint8_t)(~BOARD102_VALID_LED_MASK)) != 0u)
    {
        rsp->status = USB_STATUS_RANGE_INVALID;
        return false;
    }

    *mask_out = mask;
    return true;
}

static uint32_t decode_u32_le_local(const uint8_t *in)
{
    return ((uint32_t)in[0]) |
           ((uint32_t)in[1] << 8) |
           ((uint32_t)in[2] << 16) |
           ((uint32_t)in[3] << 24);
}

static bool fetch_motion_entry(usb_response_t *rsp,
                               uint8_t motor_id,
                               uint8_t *entry_out)
{
    uint8_t motion_state[PERICONTROL_MAX_PAYLOAD] = {0};
    uint16_t motion_state_len = 0u;
    if (!fetch_board102_motion_state(rsp, motion_state, &motion_state_len))
    {
        return false;
    }

    if (motion_state_len != BOARD102_STATUS_MOTION_LEN)
    {
        rsp->status = USB_STATUS_SUBORDINATE_LINK_ERROR;
        return false;
    }

    memcpy(entry_out,
           &motion_state[motor_id * BOARD102_STATUS_MOTOR_ENTRY_LEN],
           BOARD102_STATUS_MOTOR_ENTRY_LEN);
    return true;
}

static void pericontrol_event_step(void)
{
    uint64_t now = time_us_64();
    if ((int64_t)(now - g_board102_motion_poll_deadline_us) < 0) {
        return;
    }
    g_board102_motion_poll_deadline_us = now + BOARD102_MOTION_EVENT_POLL_INTERVAL_US;

    uint8_t status = 0u;
    uint16_t payload_len = 0u;
    uint8_t payload[PERICONTROL_MAX_PAYLOAD] = {0};
    if (pericontrol_link_transceive(PERICONTROL_CMD_GET_MOTION_STATUS,
                                    NULL,
                                    0u,
                                    &status,
                                    &payload_len,
                                    payload,
                                    sizeof(payload)) != PERICONTROL_LINK_OK ||
        status != 0u ||
        payload_len != BOARD102_STATUS_MOTION_LEN)
    {
        return;
    }

    for (uint8_t i = 0; i < BOARD102_MOTOR_COUNT; i++) {
        uint8_t *entry = &payload[i * BOARD102_STATUS_MOTOR_ENTRY_LEN];
        uint8_t motor_id = entry[0];
        if (motor_id >= BOARD102_MOTOR_COUNT) {
            continue;
        }

        bool running = entry[2] != 0u;
        uint32_t remaining = decode_u32_le_local(&entry[8]);
        if (g_board102_motion_seen && g_board102_motion_running[motor_id] && !running && remaining == 0u) {
            usb_response_t event = {
                .status = USB_STATUS_OK,
                .opcode = USB_CMD_MOTION_GET_STATE,
                .debug_payload_len = BOARD102_STATUS_MOTOR_ENTRY_LEN,
            };
            memcpy(event.debug_payload, entry, BOARD102_STATUS_MOTOR_ENTRY_LEN);
            (void)usb_task_try_send(&event);
        }

        g_board102_motion_running[motor_id] = running;
    }
    g_board102_motion_seen = true;
}

static bool process_illumination_get_state(usb_response_t *rsp)
{
    uint8_t board102_status[PERICONTROL_MAX_PAYLOAD] = {0};
    uint16_t board102_status_len = 0u;
    if (!fetch_board102_illumination_state(rsp,
                                           board102_status,
                                           &board102_status_len))
    {
        return true;
    }

    if (board102_status_len != BOARD102_ILLUMINATION_STATUS_LEN)
    {
        rsp->status = USB_STATUS_SUBORDINATE_LINK_ERROR;
        return true;
    }

    rsp->debug_payload_len = BOARD102_ILLUMINATION_STATUS_LEN;
    memcpy(rsp->debug_payload, board102_status, BOARD102_ILLUMINATION_STATUS_LEN);
    return true;
}

static bool process_illumination_set_levels_payload(const usb_command_t *cmd, usb_response_t *rsp, const uint8_t *payload)
{
    uint8_t rx_payload[PERICONTROL_MAX_PAYLOAD] = {0};
    uint16_t rx_payload_len = 0u;
    if (!pericontrol_command_roundtrip(PERICONTROL_CMD_SET_LED_LEVELS,
                                       payload,
                                       8u,
                                       rsp,
                                       rx_payload,
                                       &rx_payload_len))
    {
        return true;
    }
    if (rx_payload_len != 8u) {
        rsp->status = USB_STATUS_SUBORDINATE_LINK_ERROR;
        return true;
    }

    uint8_t illumination_state[PERICONTROL_MAX_PAYLOAD] = {0};
    uint16_t illumination_state_len = 0u;
    if (!fetch_board102_illumination_state(rsp, illumination_state, &illumination_state_len))
    {
        return true;
    }
    if (illumination_state_len != BOARD102_ILLUMINATION_STATUS_LEN)
    {
        rsp->status = USB_STATUS_SUBORDINATE_LINK_ERROR;
        return true;
    }

    memcpy(rsp->debug_payload, illumination_state, 8u);
    rsp->debug_payload_len = 8u;
    return true;
}

static bool process_illumination_set_steady(const uint8_t *payload, usb_response_t *rsp)
{
    uint8_t steady_mask = 0u;
    if (!validate_led_mask_payload(payload, &steady_mask, rsp))
    {
        return true;
    }

    uint8_t illumination_state[PERICONTROL_MAX_PAYLOAD] = {0};
    uint16_t illumination_state_len = 0u;
    if (!fetch_board102_illumination_state(rsp, illumination_state, &illumination_state_len))
    {
        return true;
    }
    if (illumination_state_len != BOARD102_ILLUMINATION_STATUS_LEN)
    {
        rsp->status = USB_STATUS_SUBORDINATE_LINK_ERROR;
        return true;
    }

    if ((steady_mask & illumination_state[9]) != 0u)
    {
        rsp->status = USB_STATUS_RANGE_INVALID;
        return true;
    }

    uint8_t normalized_payload[4] = {steady_mask, 0u, 0u, 0u};
    uint8_t rx_payload[PERICONTROL_MAX_PAYLOAD] = {0};
    uint16_t rx_payload_len = 0u;
    if (!pericontrol_command_roundtrip(PERICONTROL_CMD_SET_STEADY_ILLUMINATION,
                                       normalized_payload,
                                       4u,
                                       rsp,
                                       rx_payload,
                                       &rx_payload_len))
    {
        return true;
    }
    if (rx_payload_len != 4u) {
        rsp->status = USB_STATUS_SUBORDINATE_LINK_ERROR;
        return true;
    }
    memcpy(rsp->debug_payload, normalized_payload, 4u);
    rsp->debug_payload_len = 4u;
    return true;
}

static bool process_illumination_config_sync(const uint8_t *payload, usb_response_t *rsp)
{
    uint8_t sync_mask = 0u;
    if (!validate_led_mask_payload(payload, &sync_mask, rsp))
    {
        return true;
    }

    uint8_t illumination_state[PERICONTROL_MAX_PAYLOAD] = {0};
    uint16_t illumination_state_len = 0u;
    if (!fetch_board102_illumination_state(rsp, illumination_state, &illumination_state_len))
    {
        return true;
    }
    if (illumination_state_len != BOARD102_ILLUMINATION_STATUS_LEN)
    {
        rsp->status = USB_STATUS_SUBORDINATE_LINK_ERROR;
        return true;
    }

    if ((sync_mask & illumination_state[8]) != 0u)
    {
        rsp->status = USB_STATUS_RANGE_INVALID;
        return true;
    }

    for (uint32_t i = 0; i < BOARD102_LED_CHANNEL_COUNT; i++)
    {
        uint32_t pulse_clk = decode_u32_le_local(&illumination_state[12u + (i * 4u)]);
        if (((sync_mask >> i) & 0x01u) != 0u && pulse_clk < BOARD102_MIN_SYNC_PULSE_CLK)
        {
            rsp->status = USB_STATUS_RANGE_INVALID;
            return true;
        }
    }

    uint8_t normalized_payload[4] = {sync_mask, 0u, 0u, 0u};
    uint8_t rx_payload[PERICONTROL_MAX_PAYLOAD] = {0};
    uint16_t rx_payload_len = 0u;
    if (!pericontrol_command_roundtrip(PERICONTROL_CMD_CONFIG_LED_SYNC,
                                       normalized_payload,
                                       4u,
                                       rsp,
                                       rx_payload,
                                       &rx_payload_len))
    {
        return true;
    }
    if (rx_payload_len != 4u) {
        rsp->status = USB_STATUS_SUBORDINATE_LINK_ERROR;
        return true;
    }
    memcpy(rsp->debug_payload, normalized_payload, 4u);
    rsp->debug_payload_len = 4u;
    return true;
}

static bool process_illumination_set_sync_pulse(const uint8_t *payload, usb_response_t *rsp)
{
    uint8_t illumination_state[PERICONTROL_MAX_PAYLOAD] = {0};
    uint16_t illumination_state_len = 0u;
    if (!fetch_board102_illumination_state(rsp, illumination_state, &illumination_state_len))
    {
        return true;
    }
    if (illumination_state_len != BOARD102_ILLUMINATION_STATUS_LEN)
    {
        rsp->status = USB_STATUS_SUBORDINATE_LINK_ERROR;
        return true;
    }

    uint8_t sync_mask = illumination_state[9];
    for (uint32_t i = 0; i < BOARD102_LED_CHANNEL_COUNT; i++)
    {
        uint32_t pulse_clk = decode_u32_le_local(&payload[i * 4u]);
        if (((sync_mask >> i) & 0x01u) != 0u && pulse_clk < BOARD102_MIN_SYNC_PULSE_CLK)
        {
            rsp->status = USB_STATUS_RANGE_INVALID;
            return true;
        }
    }

    uint8_t rx_payload[PERICONTROL_MAX_PAYLOAD] = {0};
    uint16_t rx_payload_len = 0u;
    if (!pericontrol_command_roundtrip(PERICONTROL_CMD_SET_SYNC_PULSE_CLK,
                                       payload,
                                       16u,
                                       rsp,
                                       rx_payload,
                                       &rx_payload_len))
    {
        return true;
    }
    if (rx_payload_len != 16u) {
        rsp->status = USB_STATUS_SUBORDINATE_LINK_ERROR;
        return true;
    }
    memcpy(rsp->debug_payload, rx_payload, 16u);
    rsp->debug_payload_len = 16u;
    return true;
}

static bool process_motion_get_state(usb_response_t *rsp)
{
    uint8_t board102_status[PERICONTROL_MAX_PAYLOAD] = {0};
    uint16_t board102_status_len = 0u;
    if (!fetch_board102_motion_state(rsp,
                                     board102_status,
                                     &board102_status_len))
    {
        return true;
    }
    if (board102_status_len != BOARD102_STATUS_MOTION_LEN)
    {
        rsp->status = USB_STATUS_SUBORDINATE_LINK_ERROR;
        return true;
    }

    memcpy(rsp->debug_payload, board102_status, BOARD102_STATUS_MOTION_LEN);
    rsp->debug_payload_len = BOARD102_STATUS_MOTION_LEN;
    return true;
}

static bool process_motion_passthrough(uint8_t pericontrol_opcode,
                                       const uint8_t *payload,
                                       uint16_t payload_len,
                                       uint16_t expected_rx_payload_len,
                                       usb_response_t *rsp)
{
    uint8_t rx_payload[PERICONTROL_MAX_PAYLOAD] = {0};
    uint16_t rx_payload_len = 0u;
    if (!pericontrol_command_roundtrip(pericontrol_opcode,
                                       payload,
                                       payload_len,
                                       rsp,
                                       rx_payload,
                                       &rx_payload_len))
    {
        return true;
    }

    if (rx_payload_len != expected_rx_payload_len ||
        rx_payload_len > USB_DEBUG_PASSTHROUGH_MAX_FRAME_PAYLOAD) {
        rsp->status = USB_STATUS_SUBORDINATE_LINK_ERROR;
        return true;
    }
    memcpy(rsp->debug_payload, rx_payload, rx_payload_len);
    rsp->debug_payload_len = rx_payload_len;
    return true;
}

static bool validate_motion_enable_payload(const uint8_t *payload, usb_response_t *rsp)
{
    if (payload[0] >= BOARD102_MOTOR_COUNT)
    {
        rsp->status = USB_STATUS_RANGE_INVALID;
        return false;
    }
    if (payload[1] > 1u)
    {
        rsp->status = USB_STATUS_PAYLOAD_INVALID;
        return false;
    }
    return true;
}

static bool validate_motion_move_payload(const uint8_t *payload, usb_response_t *rsp)
{
    uint8_t motor_id = payload[0];
    uint8_t direction = payload[1];
    uint32_t steps = decode_u32_le_local(&payload[2]);
    uint32_t interval_us = decode_u32_le_local(&payload[6]);

    if (motor_id >= BOARD102_MOTOR_COUNT)
    {
        rsp->status = USB_STATUS_RANGE_INVALID;
        return false;
    }
    if (direction > 1u)
    {
        rsp->status = USB_STATUS_PAYLOAD_INVALID;
        return false;
    }
    if (steps == 0u || interval_us < BOARD102_MIN_STEP_INTERVAL_US)
    {
        rsp->status = USB_STATUS_RANGE_INVALID;
        return false;
    }

    uint8_t motion_entry[BOARD102_STATUS_MOTOR_ENTRY_LEN] = {0};
    if (!fetch_motion_entry(rsp, motor_id, motion_entry))
    {
        return false;
    }
    if (motion_entry[1] == 0u)
    {
        rsp->status = USB_STATUS_RANGE_INVALID;
        return false;
    }

    return true;
}

static bool validate_motion_single_motor_payload(const uint8_t *payload, usb_response_t *rsp)
{
    if (payload[0] >= BOARD102_MOTOR_COUNT)
    {
        rsp->status = USB_STATUS_RANGE_INVALID;
        return false;
    }
    return true;
}

static bool process_debug_passthrough_command(const usb_command_t *cmd, usb_response_t *rsp)
{
    if (cmd->debug_payload_len < 2u)
    {
        return false;
    }

    uint8_t target = cmd->debug_payload[0];
    uint8_t pericontrol_opcode = cmd->debug_payload[1];
    if (target != USB_DEBUG_TARGET_BOARD102)
    {
        rsp->status = USB_STATUS_DEBUG_TARGET_UNSUPPORTED;
        rsp->debug_payload_len = 0u;
        return true;
    }

    uint16_t tx_payload_len = (uint16_t)(cmd->debug_payload_len - 2u);
    if (tx_payload_len > USB_DEBUG_PASSTHROUGH_MAX_SUBPAYLOAD)
    {
        rsp->status = USB_STATUS_PAYLOAD_INVALID;
        rsp->debug_payload_len = 0u;
        return true;
    }

    uint8_t pericontrol_status = 0u;
    uint16_t pericontrol_payload_len = 0u;
    uint8_t pericontrol_payload[PERICONTROL_MAX_PAYLOAD] = {0};
    pericontrol_link_result_t link_result = pericontrol_link_transceive(pericontrol_opcode,
                                                                        &cmd->debug_payload[2],
                                                                        tx_payload_len,
                                                                        &pericontrol_status,
                                                                        &pericontrol_payload_len,
                                                                        pericontrol_payload,
                                                                        sizeof(pericontrol_payload));
    if (link_result != PERICONTROL_LINK_OK)
    {
        rsp->status = map_pericontrol_link_result_to_usb_status(link_result);
        rsp->debug_payload_len = 0u;
        return true;
    }

    if ((uint16_t)(pericontrol_payload_len + 3u) > USB_DEBUG_PASSTHROUGH_MAX_FRAME_PAYLOAD)
    {
        rsp->status = USB_STATUS_SUBORDINATE_LINK_ERROR;
        rsp->debug_payload_len = 0u;
        return true;
    }

    rsp->status = USB_STATUS_OK;
    rsp->debug_payload[0] = target;
    rsp->debug_payload[1] = pericontrol_opcode;
    rsp->debug_payload[2] = pericontrol_status;
    memcpy(&rsp->debug_payload[3], pericontrol_payload, pericontrol_payload_len);
    rsp->debug_payload_len = (uint16_t)(pericontrol_payload_len + 3u);
    return true;
}

static inline uint32_t sm0_dma_irq_mask(void)
{
    return (1u << g_scan_dma_chan[0]);
}

static void normalize_params(prism_params_t *params)
{
    if (params->sys_clock_khz < PRISM_MIN_SYS_CLOCK_KHZ ||
        params->sys_clock_khz > PRISM_MAX_SYS_CLOCK_KHZ)
    {
        params->sys_clock_khz = PRISM_DEFAULT_SYS_CLOCK_KHZ;
    }
}

static bool system_clock_supported_khz(uint32_t sys_clock_khz)
{
    if (sys_clock_khz < PRISM_MIN_SYS_CLOCK_KHZ ||
        sys_clock_khz > PRISM_MAX_SYS_CLOCK_KHZ)
    {
        return false;
    }

    uint vco = 0;
    uint post_div1 = 0;
    uint post_div2 = 0;
    return check_sys_clock_khz(sys_clock_khz, &vco, &post_div1, &post_div2);
}

static bool apply_system_clock_khz(uint32_t sys_clock_khz)
{
    if (!system_clock_supported_khz(sys_clock_khz))
    {
        return false;
    }

    return set_sys_clock_khz(sys_clock_khz, false);
}

static void reinit_runtime_after_clock_change(void)
{
    timing_gen_reinit();
    if (g_warmup_active)
    {
        warmup_dma_fill_buffer();
    }
}

static void apply_ad9826_params(const prism_params_t *params)
{
    AD9826_SPI_Handle adc1 = {ADC1_ADCCLK_PIN, ADC1_SPI_SCLK_PIN, ADC1_SPI_DATA_PIN, ADC1_SPI_LOAD_PIN};
    ad9826_spi_init_handle(&adc1);
    ad9826_write_data_handle(&adc1, AD9826_REG_CONFIG, 0b001011000); // Set 2V Input, 3CH Mode Off
    ad9826_write_data_handle(&adc1, AD9826_REG_MUX, 0b010010000); // Set RG Channel Off, B Channel On
    ad9826_write_data_handle(&adc1, AD9826_REG_GAIN_B, params->adc1_gain & AD9826_REG_DATA_MASK);
    ad9826_write_data_handle(&adc1, AD9826_REG_OFFSET_B, params->adc1_offset & AD9826_REG_DATA_MASK);

    AD9826_SPI_Handle adc2 = {ADC2_ADCCLK_PIN, ADC2_SPI_SCLK_PIN, ADC2_SPI_DATA_PIN, ADC2_SPI_LOAD_PIN};
    ad9826_spi_init_handle(&adc2);
    ad9826_write_data_handle(&adc2, AD9826_REG_CONFIG, 0b001011000); // Set 2V Input, 3CH Mode Off
    ad9826_write_data_handle(&adc2, AD9826_REG_MUX, 0b010100000); // Set RB Channel Off, G Channel On
    ad9826_write_data_handle(&adc2, AD9826_REG_GAIN_G, params->adc2_gain & AD9826_REG_DATA_MASK);
    ad9826_write_data_handle(&adc2, AD9826_REG_OFFSET_G, params->adc2_offset & AD9826_REG_DATA_MASK);

    ad9826_spi_deinit_handle(&adc1);
    ad9826_spi_deinit_handle(&adc2);
}

static void encode_u16_le_local(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)(value & 0xFFu);
    out[1] = (uint8_t)((value >> 8) & 0xFFu);
}

static bool try_read_ad9826_param_by_hash(uint32_t key_hash, uint8_t *type, uint8_t *len, uint8_t *value)
{
    static uint32_t adc1_gain_hash = 0;
    static uint32_t adc1_offset_hash = 0;
    static uint32_t adc2_gain_hash = 0;
    static uint32_t adc2_offset_hash = 0;
    static bool hashes_initialized = false;

    if (!hashes_initialized)
    {
        adc1_gain_hash = prism_param_hash_key("prism.adc1.gain");
        adc1_offset_hash = prism_param_hash_key("prism.adc1.offset");
        adc2_gain_hash = prism_param_hash_key("prism.adc2.gain");
        adc2_offset_hash = prism_param_hash_key("prism.adc2.offset");
        hashes_initialized = true;
    }

    AD9826_SPI_Handle handle = {0};
    uint16_t reg_value = 0;

    if (key_hash == adc1_gain_hash)
    {
        handle = (AD9826_SPI_Handle){ADC1_ADCCLK_PIN, ADC1_SPI_SCLK_PIN, ADC1_SPI_DATA_PIN, ADC1_SPI_LOAD_PIN};
        ad9826_spi_init_handle(&handle);
        reg_value = ad9826_read_data_handle(&handle, AD9826_REG_GAIN_B) & AD9826_REG_DATA_MASK;
        ad9826_spi_deinit_handle(&handle);
    }
    else if (key_hash == adc1_offset_hash)
    {
        handle = (AD9826_SPI_Handle){ADC1_ADCCLK_PIN, ADC1_SPI_SCLK_PIN, ADC1_SPI_DATA_PIN, ADC1_SPI_LOAD_PIN};
        ad9826_spi_init_handle(&handle);
        reg_value = ad9826_read_data_handle(&handle, AD9826_REG_OFFSET_B) & AD9826_REG_DATA_MASK;
        ad9826_spi_deinit_handle(&handle);
    }
    else if (key_hash == adc2_gain_hash)
    {
        handle = (AD9826_SPI_Handle){ADC2_ADCCLK_PIN, ADC2_SPI_SCLK_PIN, ADC2_SPI_DATA_PIN, ADC2_SPI_LOAD_PIN};
        ad9826_spi_init_handle(&handle);
        reg_value = ad9826_read_data_handle(&handle, AD9826_REG_GAIN_G) & AD9826_REG_DATA_MASK;
        ad9826_spi_deinit_handle(&handle);
    }
    else if (key_hash == adc2_offset_hash)
    {
        handle = (AD9826_SPI_Handle){ADC2_ADCCLK_PIN, ADC2_SPI_SCLK_PIN, ADC2_SPI_DATA_PIN, ADC2_SPI_LOAD_PIN};
        ad9826_spi_init_handle(&handle);
        reg_value = ad9826_read_data_handle(&handle, AD9826_REG_OFFSET_G) & AD9826_REG_DATA_MASK;
        ad9826_spi_deinit_handle(&handle);
    }
    else
    {
        return false;
    }

    *type = 2u;
    *len = 2u;
    encode_u16_le_local(value, reg_value);
    return true;
}

static void timing_gen_configure_sms(void)
{
    PIO pio = pio0;

    line_sig_generate_program_init(pio, 0, g_line_sig_offset, CCD_CLK_SH_PIN);
    pio_sm_clear_fifos(pio, 0);

    cds_line_generate_program_init(pio, 1, g_cds_line_offset, ADC2_ADCCLK_PIN);
    pio_sm_clear_fifos(pio, 1);

    fifo_line_generate_sync_program_init(pio, 2, g_fifo_line_offset, FIFO_SLWR_PIN);
    pio_sm_clear_fifos(pio, 2);

    ifclk_generate_program_init(pio, 3, g_ifclk_offset, FIFO_IFCLK_PIN);
}

static void timing_gen_init(void)
{
    PIO pio = pio0;

    g_line_sig_offset = pio_add_program(pio, &line_sig_generate_program);
    g_cds_line_offset = pio_add_program(pio, &cds_line_generate_program);
    g_fifo_line_offset = pio_add_program(pio, &fifo_line_generate_sync_program);
    g_ifclk_offset = pio_add_program(pio, &ifclk_generate_program);

    pio_sm_claim(pio, 0);
    pio_sm_claim(pio, 1);
    pio_sm_claim(pio, 2);
    pio_sm_claim(pio, 3);

    timing_gen_configure_sms();

    pio_enable_sm_mask_in_sync(pio, 0b1111);
}

static void timing_gen_reinit(void)
{
    PIO pio = pio0;

    pio_set_sm_mask_enabled(pio, 0b1111, false);
    pio_interrupt_clear(pio, 4);
    pio_interrupt_clear(pio, 5);

    timing_gen_configure_sms();

    pio_enable_sm_mask_in_sync(pio, 0b1111);
}

static void warmup_dma_fill_buffer(void)
{
    uint32_t word = g_params.exposure_ticks | (PRISM_SH_TICKS_PER_LINE << 16) | (PRISM_PIXEL_CYCLES_PER_LINE << 20);
    for (uint32_t i = 0; i < WARMUP_DMA_CHUNK_LINES; i++)
    {
        g_warmup_dma_buf[i] = word;
    }
}

static inline uint32_t scan_last_flag_for_line(uint32_t line_1_based)
{
    uint32_t rem = (uint32_t)(((uint64_t)g_scan_target_lines * PRISM_BYTES_PER_LINE) % 512u);
    return (line_1_based < g_scan_target_lines) ? 0x00u : (rem ? 0xFFu : 0x00u);
}

static void scan_dma_prepare_bank(uint8_t bank)
{
    uint32_t remaining = g_scan_target_lines - g_scan_prepared_lines;
    uint32_t count = (remaining > SCAN_DMA_CHUNK_LINES) ? SCAN_DMA_CHUNK_LINES : remaining;
    g_scan_bank_lines[bank] = count;

    for (uint32_t i = 0; i < count; i++)
    {
        uint32_t line_1_based = g_scan_prepared_lines + i + 1u;
        uint32_t last_flag = scan_last_flag_for_line(line_1_based);
        g_scan_dma_buf_sm0[bank][i] = g_params.exposure_ticks | (PRISM_SH_TICKS_PER_LINE << 16) | (PRISM_PIXEL_CYCLES_PER_LINE << 20);
        g_scan_dma_buf_sm1[bank][i] = PRISM_CDS_CYCLES_PER_LINE;
        g_scan_dma_buf_sm2[bank][i] = last_flag | (PRISM_FIFO_CYCLES_PER_LINE << 16);
    }

    g_scan_prepared_lines += count;
}

static void scan_dma_update_active_mask(void)
{
    g_scan_dma_active_mask = sm0_dma_irq_mask() | (1u << g_scan_dma_chan[1]) | (1u << g_scan_dma_chan[2]);
}

static void scan_dma_start_bank(uint8_t bank)
{
    uint32_t count = g_scan_bank_lines[bank];
    if (count == 0)
    {
        return;
    }

    dma_channel_set_read_addr(g_scan_dma_chan[0], g_scan_dma_buf_sm0[bank], false);
    dma_channel_set_trans_count(g_scan_dma_chan[0], count, false);

    dma_channel_set_read_addr(g_scan_dma_chan[1], g_scan_dma_buf_sm1[bank], false);
    dma_channel_set_trans_count(g_scan_dma_chan[1], count, false);

    dma_channel_set_read_addr(g_scan_dma_chan[2], g_scan_dma_buf_sm2[bank], false);
    dma_channel_set_trans_count(g_scan_dma_chan[2], count, false);

    dma_start_channel_mask(g_scan_dma_active_mask);
}

static bool scan_dma_all_channels_idle(void)
{
    if (dma_channel_is_busy(g_scan_dma_chan[0]))
    {
        return false;
    }

    return !dma_channel_is_busy(g_scan_dma_chan[1]) &&
           !dma_channel_is_busy(g_scan_dma_chan[2]);
}

static void warmup_dma_start_chunk(void)
{
    if (!g_warmup_active)
    {
        return;
    }

    dma_channel_set_read_addr(g_scan_dma_chan[0], g_warmup_dma_buf, false);
    dma_channel_set_trans_count(g_scan_dma_chan[0], WARMUP_DMA_CHUNK_LINES, true);
}

static void dma_clear_runtime_buffers(void)
{
    memset((void *)g_scan_bank_lines, 0, sizeof(g_scan_bank_lines));
    memset(g_scan_dma_buf_sm0, 0, sizeof(g_scan_dma_buf_sm0));
    memset(g_scan_dma_buf_sm1, 0, sizeof(g_scan_dma_buf_sm1));
    memset(g_scan_dma_buf_sm2, 0, sizeof(g_scan_dma_buf_sm2));
    memset(g_warmup_dma_buf, 0, sizeof(g_warmup_dma_buf));
}

static void scan_dma_stop_internal(void)
{
    g_scan_active = false;
    g_scan_done_pending = false;
    g_warmup_active = false;
    dma_channel_abort(g_scan_dma_chan[0]);
    dma_channel_abort(g_scan_dma_chan[1]);
    dma_channel_abort(g_scan_dma_chan[2]);
    irq_set_enabled(DMA_IRQ_1, false);
    dma_hw->ints1 = g_scan_dma_irq_mask;

    g_scan_target_lines = 0;
    g_scan_completed_lines = 0;
    g_scan_prepared_lines = 0;
    g_scan_dma_active_mask = 0;
    g_scan_dma_running_bank = 0;
    g_scan_dma_pending_bank = 1;
    dma_clear_runtime_buffers();
    timing_gen_reinit();
}

static void scan_dma_irq_handler(void)
{
    uint32_t ints = dma_hw->ints1 & g_scan_dma_irq_mask;
    if (!ints)
    {
        return;
    }

    dma_hw->ints1 = ints;

    if (!g_scan_active)
    {
        if ((ints & sm0_dma_irq_mask()) && g_warmup_active)
        {
            warmup_dma_start_chunk();
        }
        return;
    }

    if (!(ints & g_scan_dma_active_mask) || !scan_dma_all_channels_idle())
    {
        return;
    }

    uint8_t finished_bank = g_scan_dma_running_bank;
    g_scan_completed_lines += g_scan_bank_lines[finished_bank];

    if (g_scan_completed_lines >= g_scan_target_lines)
    {
        g_scan_active = false;
        g_scan_done_pending = true;
        if (g_warmup_active)
        {
            warmup_dma_start_chunk();
        }
        return;
    }

    uint8_t next_bank = g_scan_dma_pending_bank;
    scan_dma_start_bank(next_bank);
    g_scan_dma_running_bank = next_bank;
    g_scan_dma_pending_bank = finished_bank;

    if (g_scan_prepared_lines < g_scan_target_lines)
    {
        scan_dma_prepare_bank(finished_bank);
    }
    else
    {
        g_scan_bank_lines[finished_bank] = 0;
    }
}

static void scan_dma_init(void)
{
    g_scan_dma_irq_mask = 0;
    g_scan_dma_chan[0] = dma_claim_unused_channel(true);
    g_scan_dma_chan[1] = dma_claim_unused_channel(true);
    g_scan_dma_chan[2] = dma_claim_unused_channel(true);

    for (int sm = (int)SCAN_DMA_SM_COUNT - 1; sm >= 0; sm--)    // Load SM2 and SM1 first
    {
        dma_channel_config cfg = dma_channel_get_default_config(g_scan_dma_chan[sm]);
        channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);
        channel_config_set_read_increment(&cfg, true);
        channel_config_set_write_increment(&cfg, false);
        channel_config_set_dreq(&cfg, pio_get_dreq(pio0, sm, true));
        dma_channel_configure(g_scan_dma_chan[sm], &cfg, &pio0->txf[sm], NULL, 0, false);
        dma_channel_set_irq1_enabled(g_scan_dma_chan[sm], true);
        g_scan_dma_irq_mask |= (1u << g_scan_dma_chan[sm]);
    }

    irq_set_exclusive_handler(DMA_IRQ_1, scan_dma_irq_handler);
    irq_set_enabled(DMA_IRQ_1, false);
}

static void warmup_start(void)
{
    uint32_t irq_state = save_and_disable_interrupts();

    if (g_warmup_active)
    {
        restore_interrupts(irq_state);
        return;
    }

    timing_gen_reinit();
    warmup_dma_fill_buffer();
    dma_hw->ints1 = g_scan_dma_irq_mask;
    g_warmup_active = true;
    irq_set_enabled(DMA_IRQ_1, true);
    warmup_dma_start_chunk();

    restore_interrupts(irq_state);
}

static void scan_engine_start(void)
{
    uint32_t irq_state = save_and_disable_interrupts();

    if (g_warmup_active)
    {
        dma_channel_abort(g_scan_dma_chan[0]);
    }

    g_scan_target_lines = g_scan_lines;
    g_scan_completed_lines = 0;
    g_scan_prepared_lines = 0;
    g_scan_done_pending = false;

    g_scan_dma_running_bank = 0;
    g_scan_dma_pending_bank = 1;
    g_scan_bank_lines[0] = 0;
    g_scan_bank_lines[1] = 0;

    scan_dma_update_active_mask();
    scan_dma_prepare_bank(g_scan_dma_running_bank);
    scan_dma_prepare_bank(g_scan_dma_pending_bank);

    dma_hw->ints1 = g_scan_dma_irq_mask;
    irq_set_enabled(DMA_IRQ_1, true);

    g_scan_active = true;
    scan_dma_start_bank(g_scan_dma_running_bank);

    restore_interrupts(irq_state);
}

static void scan_engine_stop(void)
{
    uint32_t irq_state = save_and_disable_interrupts();
    scan_dma_stop_internal();
    restore_interrupts(irq_state);
}

static void scan_engine_step(void)
{
    if (!g_scan_done_pending)
    {
        return;
    }

    uint32_t irq_state = save_and_disable_interrupts();
    g_scan_done_pending = false;
    uint32_t target = g_scan_target_lines;
    uint32_t completed = g_scan_completed_lines;
    restore_interrupts(irq_state);

    usb_response_t done = {
        .status = USB_STATUS_OK,
        .opcode = USB_CMD_START_SCAN,
        .target_scan_lines = target,
        .completed_scan_lines = completed,
    };
    usb_task_send_blocking(&done);
}

static void process_usb_command(const usb_command_t *cmd)
{
    usb_response_t rsp = {
        .status = USB_STATUS_OK,
        .opcode = cmd->type,
        .params = g_params,
    };

    if (g_scan_active && cmd->type != USB_CMD_STOP_SCAN)
    {
        rsp.status = USB_STATUS_BUSY;
        rsp.target_scan_lines = g_scan_target_lines;
        rsp.completed_scan_lines = g_scan_completed_lines;
        usb_task_send_blocking(&rsp);
        return;
    }

    if (g_warmup_active && (cmd->type < 0x30u || cmd->type > 0x3Fu))
    {
        rsp.status = USB_STATUS_BUSY;
        rsp.target_scan_lines = g_scan_target_lines;
        rsp.completed_scan_lines = g_scan_completed_lines;
        usb_task_send_blocking(&rsp);
        return;
    }

    switch (cmd->type)
    {
    case USB_CMD_GET_PARAM_BY_HASH:
        rsp.key_hash = cmd->key_hash;
        if (!try_read_ad9826_param_by_hash(cmd->key_hash, &rsp.param_type, &rsp.param_len, rsp.param_data) &&
            !prism_param_get_by_hash(&g_params, cmd->key_hash, &rsp.param_type, &rsp.param_len, rsp.param_data))
        {
            rsp.status = USB_STATUS_PARAM_NOT_FOUND;
        }
        break;

    case USB_CMD_SET_PARAM_BY_HASH: {
        rsp.key_hash = cmd->key_hash;
        uint8_t expected_type = 0;
        uint8_t expected_len = 0;
        uint8_t current_type = 0;
        uint8_t current_len = 0;
        uint8_t current_data[USB_PARAM_MAX_DATA_LEN] = {0};
        if (!prism_param_meta_by_hash(cmd->key_hash, &expected_type, &expected_len))
        {
            rsp.status = USB_STATUS_PARAM_NOT_FOUND;
            break;
        }

        if (cmd->param_type != expected_type)
        {
            rsp.status = USB_STATUS_PARAM_TYPE_MISMATCH;
            break;
        }

        if (cmd->param_len != expected_len)
        {
            rsp.status = USB_STATUS_PARAM_LEN_INVALID;
            break;
        }

        if (!prism_param_get_by_hash(&g_params, cmd->key_hash, &current_type, &current_len, current_data))
        {
            rsp.status = USB_STATUS_PARAM_NOT_FOUND;
            break;
        }

        bool value_changed = (current_type != cmd->param_type) ||
                             (current_len != cmd->param_len) ||
                             (memcmp(current_data, cmd->param_data, cmd->param_len) != 0);

        if (value_changed)
        {
            if (!prism_param_set_by_hash(&g_params, cmd->key_hash, cmd->param_type, cmd->param_len, cmd->param_data))
            {
                rsp.status = USB_STATUS_PAYLOAD_INVALID;
                break;
            }

            normalize_params(&g_params);
            if (!apply_system_clock_khz(g_params.sys_clock_khz))
            {
                (void)prism_param_set_by_hash(&g_params, cmd->key_hash, current_type, current_len, current_data);
                rsp.status = USB_STATUS_PAYLOAD_INVALID;
                break;
            }
            apply_ad9826_params(&g_params);
            reinit_runtime_after_clock_change();
            schedule_params_save();
        }

        if (!try_read_ad9826_param_by_hash(cmd->key_hash, &rsp.param_type, &rsp.param_len, rsp.param_data))
        {
            (void)prism_param_get_by_hash(&g_params, cmd->key_hash, &rsp.param_type, &rsp.param_len, rsp.param_data);
        }
        break;
    }

    case USB_CMD_SET_SCAN_LINES:
        rsp.target_scan_lines = cmd->scan_lines;
        rsp.completed_scan_lines = 0;
        if (cmd->scan_lines == 0)
        {
            rsp.status = USB_STATUS_SCAN_LINES_INVALID;
        }
        else
        {
            g_scan_lines = cmd->scan_lines;
        }
        break;

    case USB_CMD_START_SCAN:
        if (g_scan_lines == 0)
        {
            rsp.status = USB_STATUS_SCAN_LINES_INVALID;
            break;
        }

        scan_engine_start();

        rsp.target_scan_lines = g_scan_target_lines;
        rsp.completed_scan_lines = g_scan_completed_lines;
        usb_task_send_blocking(&rsp);
        return;

    case USB_CMD_START_WARMUP:
        warmup_start();
        rsp.target_scan_lines = g_scan_target_lines;
        rsp.completed_scan_lines = g_scan_completed_lines;
        break;

    case USB_CMD_ILLUMINATION_GET_STATE:
        (void)process_illumination_get_state(&rsp);
        break;

    case USB_CMD_ILLUMINATION_SET_LEVELS:
        (void)process_illumination_set_levels_payload(cmd, &rsp, cmd->debug_payload);
        break;

    case USB_CMD_ILLUMINATION_SET_STEADY:
        (void)process_illumination_set_steady(cmd->debug_payload, &rsp);
        break;

    case USB_CMD_ILLUMINATION_CONFIG_SYNC:
        (void)process_illumination_config_sync(cmd->debug_payload, &rsp);
        break;

    case USB_CMD_ILLUMINATION_SET_SYNC_PULSE:
        (void)process_illumination_set_sync_pulse(cmd->debug_payload, &rsp);
        break;

    case USB_CMD_MOTION_GET_STATE:
        (void)process_motion_get_state(&rsp);
        break;

    case USB_CMD_MOTION_SET_ENABLE:
        if (!validate_motion_enable_payload(cmd->debug_payload, &rsp))
        {
            break;
        }
        (void)process_motion_passthrough(PERICONTROL_CMD_SET_MOTOR_ENABLE, cmd->debug_payload, cmd->debug_payload_len, 2u, &rsp);
        break;

    case USB_CMD_MOTION_MOVE_STEPS:
        if (!validate_motion_move_payload(cmd->debug_payload, &rsp))
        {
            break;
        }
        (void)process_motion_passthrough(PERICONTROL_CMD_MOVE_MOTOR_STEPS, cmd->debug_payload, cmd->debug_payload_len, 10u, &rsp);
        if (rsp.status == USB_STATUS_OK && cmd->debug_payload[0] < BOARD102_MOTOR_COUNT)
        {
            g_board102_motion_running[cmd->debug_payload[0]] = true;
            g_board102_motion_seen = true;
            g_board102_motion_poll_deadline_us = time_us_64() + BOARD102_MOTION_EVENT_POLL_INTERVAL_US;
        }
        break;

    case USB_CMD_MOTION_PREPARE_ON_SYNC:
        if (!validate_motion_move_payload(cmd->debug_payload, &rsp))
        {
            break;
        }
        (void)process_motion_passthrough(PERICONTROL_CMD_PREPARE_MOTOR_ON_SYNC, cmd->debug_payload, cmd->debug_payload_len, 10u, &rsp);
        if (rsp.status == USB_STATUS_OK && cmd->debug_payload[0] < BOARD102_MOTOR_COUNT)
        {
            g_board102_motion_running[cmd->debug_payload[0]] = true;
            g_board102_motion_seen = true;
            g_board102_motion_poll_deadline_us = time_us_64() + BOARD102_MOTION_EVENT_POLL_INTERVAL_US;
        }
        break;

    case USB_CMD_MOTION_STOP:
        if (!validate_motion_single_motor_payload(cmd->debug_payload, &rsp))
        {
            break;
        }
        (void)process_motion_passthrough(PERICONTROL_CMD_STOP_MOTOR, cmd->debug_payload, cmd->debug_payload_len, 1u, &rsp);
        break;

    case USB_CMD_MOTION_APPLY_CONFIG:
        if (!validate_motion_single_motor_payload(cmd->debug_payload, &rsp))
        {
            break;
        }
        (void)process_motion_passthrough(PERICONTROL_CMD_APPLY_MOTOR_CONFIG, cmd->debug_payload, cmd->debug_payload_len, 1u, &rsp);
        break;

    case USB_CMD_STOP_SCAN:
        scan_engine_stop();
        rsp.target_scan_lines = g_scan_target_lines;
        rsp.completed_scan_lines = g_scan_completed_lines;
        break;

    case USB_CMD_DEBUG_PASSTHROUGH:
        if (!process_debug_passthrough_command(cmd, &rsp))
        {
            rsp.status = USB_STATUS_BAD_FRAME;
        }
        break;

    default:
        rsp.status = USB_STATUS_BAD_FRAME;
        break;
    }

    usb_task_send_blocking(&rsp);
}

int main()
{
    flash_safe_execute_core_init();
    usb_task_init();

    prism_params_set_defaults(&g_params);
    (void)prism_params_load(&g_params);
    normalize_params(&g_params);
    (void)apply_system_clock_khz(g_params.sys_clock_khz);

    apply_ad9826_params(&g_params);
    timing_gen_init();
    scan_dma_init();
    pericontrol_link_init();

    multicore_launch_core1(usb_task_core1_main);

    while (true)
    {
        pericontrol_event_step();

        usb_command_t cmd;
        if (usb_task_try_recv(&cmd))
        {
            process_usb_command(&cmd);
        }

        scan_engine_step();
        flush_pending_params_save();
    }
}
