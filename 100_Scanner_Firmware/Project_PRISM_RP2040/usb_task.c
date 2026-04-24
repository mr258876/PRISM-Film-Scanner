/*
 * Copyright (c) 2026 mr258876
 * SPDX-License-Identifier: MIT
 */

#include "usb_task.h"

#include <string.h>

#include "pico/flash.h"
#include "pico/stdlib.h"
#include "pico/util/queue.h"
#include "tusb.h"

#define USB_TASK_QUEUE_DEPTH 64u
#define USB_FRAME_MAX_PAYLOAD CFG_TUD_VENDOR_EPSIZE
#define USB_FRAME_LEN_FIELD_SIZE 2u
#define USB_GET_PARAM_PAYLOAD_LEN 4u
#define USB_SET_PARAM_FIXED_PAYLOAD_LEN 6u
#define USB_SET_PARAM_TYPE_OFFSET 4u
#define USB_SET_PARAM_LEN_OFFSET 5u
#define USB_SET_PARAM_DATA_OFFSET 6u
#define USB_SCAN_STATUS_PAYLOAD_LEN 8u
#define USB_RESPONSE_HEADER_SIZE 5u

static bool is_debug_passthrough_opcode(uint8_t opcode)
{
    switch (opcode)
    {
    case USB_CMD_DEBUG_PASSTHROUGH:
        return true;
    default:
        return false;
    }
}

static bool is_domain_payload_opcode(uint8_t opcode)
{
    switch (opcode)
    {
    case USB_CMD_ILLUMINATION_GET_STATE:
    case USB_CMD_ILLUMINATION_SET_LEVELS:
    case USB_CMD_ILLUMINATION_SET_STEADY:
    case USB_CMD_ILLUMINATION_CONFIG_SYNC:
    case USB_CMD_ILLUMINATION_SET_SYNC_PULSE:
    case USB_CMD_MOTION_GET_STATE:
    case USB_CMD_MOTION_SET_ENABLE:
    case USB_CMD_MOTION_MOVE_STEPS:
    case USB_CMD_MOTION_STOP:
    case USB_CMD_MOTION_APPLY_CONFIG:
    case USB_CMD_MOTION_PREPARE_ON_SYNC:
        return true;
    default:
        return false;
    }
}

typedef enum {
    USB_RX_STATE_WAIT_MARKER = 0,
    USB_RX_STATE_WAIT_OPCODE,
    USB_RX_STATE_WAIT_LEN,
    USB_RX_STATE_WAIT_PAYLOAD
} usb_rx_state_t;

static queue_t usb_rx_queue;
static queue_t usb_tx_queue;

static uint32_t decode_u32_le(const uint8_t *in)
{
    return ((uint32_t)in[0]) |
           ((uint32_t)in[1] << 8) |
           ((uint32_t)in[2] << 16) |
           ((uint32_t)in[3] << 24);
}

static uint16_t decode_u16_le(const uint8_t *in)
{
    return (uint16_t)(((uint16_t)in[0]) | ((uint16_t)in[1] << 8));
}

static void encode_u32_le(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)(value & 0xFFu);
    out[1] = (uint8_t)((value >> 8) & 0xFFu);
    out[2] = (uint8_t)((value >> 16) & 0xFFu);
    out[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static void encode_u16_le(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)(value & 0xFFu);
    out[1] = (uint8_t)((value >> 8) & 0xFFu);
}

static void push_status_response(uint8_t opcode, uint8_t status)
{
    usb_response_t rsp = {
        .status = status,
        .opcode = opcode,
    };
    (void)queue_try_add(&usb_tx_queue, &rsp);
}

static bool is_valid_frame_opcode(uint8_t opcode)
{
    switch (opcode)
    {
    case USB_CMD_GET_PARAM_BY_HASH:
    case USB_CMD_SET_PARAM_BY_HASH:
    case USB_CMD_START_SCAN:
    case USB_CMD_SET_SCAN_LINES:
    case USB_CMD_STOP_SCAN:
    case USB_CMD_START_WARMUP:
    case USB_CMD_ILLUMINATION_GET_STATE:
    case USB_CMD_ILLUMINATION_SET_LEVELS:
    case USB_CMD_ILLUMINATION_SET_STEADY:
    case USB_CMD_ILLUMINATION_CONFIG_SYNC:
    case USB_CMD_ILLUMINATION_SET_SYNC_PULSE:
    case USB_CMD_MOTION_GET_STATE:
    case USB_CMD_MOTION_SET_ENABLE:
    case USB_CMD_MOTION_MOVE_STEPS:
    case USB_CMD_MOTION_STOP:
    case USB_CMD_MOTION_APPLY_CONFIG:
    case USB_CMD_MOTION_PREPARE_ON_SYNC:
    case USB_CMD_DEBUG_PASSTHROUGH:
        return true;
    default:
        return false;
    }
}

static bool is_valid_frame_len(uint8_t opcode, uint16_t frame_len)
{
    switch (opcode)
    {
    case USB_CMD_START_SCAN:
    case USB_CMD_STOP_SCAN:
    case USB_CMD_START_WARMUP:
    case USB_CMD_ILLUMINATION_GET_STATE:
    case USB_CMD_MOTION_GET_STATE:
        return frame_len == 0;
    case USB_CMD_GET_PARAM_BY_HASH:
    case USB_CMD_SET_SCAN_LINES:
        return frame_len == USB_GET_PARAM_PAYLOAD_LEN;
    case USB_CMD_ILLUMINATION_SET_LEVELS:
        return frame_len == 8u;
    case USB_CMD_ILLUMINATION_SET_STEADY:
        return frame_len == 4u;
    case USB_CMD_ILLUMINATION_CONFIG_SYNC:
        return frame_len == 4u;
    case USB_CMD_ILLUMINATION_SET_SYNC_PULSE:
        return frame_len == 16u;
    case USB_CMD_MOTION_SET_ENABLE:
        return frame_len == 2u;
    case USB_CMD_MOTION_MOVE_STEPS:
    case USB_CMD_MOTION_PREPARE_ON_SYNC:
        return frame_len == 10u;
    case USB_CMD_MOTION_STOP:
    case USB_CMD_MOTION_APPLY_CONFIG:
        return frame_len == 1u;
    case USB_CMD_DEBUG_PASSTHROUGH:
        return frame_len >= 2u && frame_len <= (USB_DEBUG_PASSTHROUGH_MAX_SUBPAYLOAD + 2u);
    case USB_CMD_SET_PARAM_BY_HASH:
        return frame_len >= USB_SET_PARAM_FIXED_PAYLOAD_LEN &&
               frame_len <= (uint16_t)(USB_SET_PARAM_FIXED_PAYLOAD_LEN + USB_PARAM_MAX_DATA_LEN);
    default:
        return false;
    }
}

static void reset_rx_frame(usb_rx_state_t *state,
                           uint8_t *frame_opcode,
                           uint16_t *frame_len,
                           uint8_t *frame_pos,
                           uint8_t *frame_len_pos)
{
    *state = USB_RX_STATE_WAIT_MARKER;
    *frame_opcode = 0;
    *frame_len = 0;
    *frame_pos = 0;
    *frame_len_pos = 0;
}

static void try_queue_zero_payload_command(uint8_t *frame_opcode,
                                           usb_rx_state_t *state,
                                           uint8_t *frame_pos,
                                           uint8_t *frame_len_pos,
                                           uint16_t *frame_len)
{
    usb_command_t cmd = {
        .type = *frame_opcode,
    };
    if (!queue_try_add(&usb_rx_queue, &cmd))
    {
        push_status_response(*frame_opcode, USB_STATUS_QUEUE_FULL);
    }

    reset_rx_frame(state, frame_opcode, frame_len, frame_pos, frame_len_pos);
}

static void try_queue_payload_command(uint8_t *frame_opcode,
                                      uint16_t *frame_len,
                                      const uint8_t *frame_payload,
                                      usb_rx_state_t *state,
                                      uint8_t *frame_pos,
                                      uint8_t *frame_len_pos)
{
    usb_command_t cmd = {
        .type = *frame_opcode,
    };

    switch (*frame_opcode)
    {
    case USB_CMD_GET_PARAM_BY_HASH:
        cmd.key_hash = decode_u32_le(frame_payload);
        break;
    case USB_CMD_SET_PARAM_BY_HASH:
        cmd.key_hash = decode_u32_le(frame_payload);
        cmd.param_type = frame_payload[USB_SET_PARAM_TYPE_OFFSET];
        cmd.param_len = frame_payload[USB_SET_PARAM_LEN_OFFSET];
        if ((uint16_t)(cmd.param_len + USB_SET_PARAM_FIXED_PAYLOAD_LEN) != *frame_len || cmd.param_len > USB_PARAM_MAX_DATA_LEN)
        {
            push_status_response(*frame_opcode, USB_STATUS_PAYLOAD_INVALID);
            reset_rx_frame(state, frame_opcode, frame_len, frame_pos, frame_len_pos);
            return;
        }
        memcpy(cmd.param_data, &frame_payload[USB_SET_PARAM_DATA_OFFSET], cmd.param_len);
        break;
    case USB_CMD_SET_SCAN_LINES:
        cmd.scan_lines = decode_u32_le(frame_payload);
        break;
    default:
        if (is_debug_passthrough_opcode(*frame_opcode) || is_domain_payload_opcode(*frame_opcode))
        {
            cmd.debug_payload_len = *frame_len;
            memcpy(cmd.debug_payload, frame_payload, *frame_len);
        }
        break;
    }

    if (!queue_try_add(&usb_rx_queue, &cmd))
    {
        push_status_response(*frame_opcode, USB_STATUS_QUEUE_FULL);
    }

    reset_rx_frame(state, frame_opcode, frame_len, frame_pos, frame_len_pos);
}

void usb_task_init(void)
{
    queue_init(&usb_rx_queue, sizeof(usb_command_t), USB_TASK_QUEUE_DEPTH);
    queue_init(&usb_tx_queue, sizeof(usb_response_t), USB_TASK_QUEUE_DEPTH);
}

bool usb_task_try_recv(usb_command_t *cmd)
{
    return queue_try_remove(&usb_rx_queue, cmd);
}

void usb_task_send_blocking(const usb_response_t *rsp)
{
    while (!queue_try_add(&usb_tx_queue, rsp))
    {
        tight_loop_contents();
    }
}

bool usb_task_try_send(const usb_response_t *rsp)
{
    return queue_try_add(&usb_tx_queue, rsp);
}

void usb_task_core1_main(void)
{
    flash_safe_execute_core_init();
    tusb_init();

    uint8_t rx_buf[CFG_TUD_VENDOR_EPSIZE];
    usb_rx_state_t rx_state = USB_RX_STATE_WAIT_MARKER;
    uint8_t frame_opcode = 0;
    uint16_t frame_len = 0;
    uint8_t frame_pos = 0;
    uint8_t frame_payload[USB_FRAME_MAX_PAYLOAD];
    uint8_t frame_len_pos = 0;
    uint8_t frame_len_bytes[USB_FRAME_LEN_FIELD_SIZE] = {0};

    while (!tud_mounted())
    {
        tud_task();
    }

    while (true)
    {
        tud_task();

        while (tud_vendor_available())
        {
            uint32_t count = tud_vendor_read(rx_buf, sizeof(rx_buf));
            for (uint32_t i = 0; i < count; i++)
            {
                uint8_t byte = rx_buf[i];

                switch (rx_state)
                {
                case USB_RX_STATE_WAIT_MARKER:
                    if (byte == USB_FRAME_IN_MARKER)
                    {
                        rx_state = USB_RX_STATE_WAIT_OPCODE;
                        frame_opcode = 0;
                        frame_len = 0;
                        frame_pos = 0;
                        frame_len_pos = 0;
                    }
                    break;

                case USB_RX_STATE_WAIT_OPCODE:
                    frame_opcode = byte;
                    if (!is_valid_frame_opcode(frame_opcode))
                    {
                        push_status_response(frame_opcode, USB_STATUS_BAD_FRAME);
                        reset_rx_frame(&rx_state, &frame_opcode, &frame_len, &frame_pos, &frame_len_pos);
                        if (byte == USB_FRAME_IN_MARKER)
                        {
                            rx_state = USB_RX_STATE_WAIT_OPCODE;
                        }
                        break;
                    }

                    rx_state = USB_RX_STATE_WAIT_LEN;
                    frame_len_pos = 0;
                    frame_pos = 0;
                    break;

                case USB_RX_STATE_WAIT_LEN:
                    frame_len_bytes[frame_len_pos++] = byte;
                    if (frame_len_pos < USB_FRAME_LEN_FIELD_SIZE)
                    {
                        break;
                    }

                    frame_len = decode_u16_le(frame_len_bytes);
                    if (!is_valid_frame_len(frame_opcode, frame_len))
                    {
                        push_status_response(frame_opcode, USB_STATUS_PAYLOAD_INVALID);
                        reset_rx_frame(&rx_state, &frame_opcode, &frame_len, &frame_pos, &frame_len_pos);
                        break;
                    }

                    if (frame_len == 0)
                    {
                        try_queue_zero_payload_command(&frame_opcode, &rx_state, &frame_pos, &frame_len_pos, &frame_len);
                        break;
                    }

                    rx_state = USB_RX_STATE_WAIT_PAYLOAD;
                    frame_pos = 0;
                    break;

                case USB_RX_STATE_WAIT_PAYLOAD:
                    frame_payload[frame_pos++] = byte;
                    if (frame_pos >= frame_len)
                    {
                        try_queue_payload_command(&frame_opcode, &frame_len, frame_payload, &rx_state, &frame_pos, &frame_len_pos);
                    }
                    break;
                }
            }
        }

        uint32_t writable = tud_vendor_write_available();
        while (writable >= 5)
        {
            usb_response_t rsp;
            if (!queue_try_remove(&usb_tx_queue, &rsp))
            {
                break;
            }

            uint8_t payload[USB_FRAME_MAX_PAYLOAD];
            uint16_t payload_len = 0;

            if ((rsp.opcode == USB_CMD_GET_PARAM_BY_HASH || rsp.opcode == USB_CMD_SET_PARAM_BY_HASH) && rsp.status == USB_STATUS_OK)
            {
                encode_u32_le(payload, rsp.key_hash);
                payload[USB_SET_PARAM_TYPE_OFFSET] = rsp.param_type;
                payload[USB_SET_PARAM_LEN_OFFSET] = rsp.param_len;
                if (rsp.param_len > USB_PARAM_MAX_DATA_LEN)
                {
                    continue;
                }
                memcpy(&payload[USB_SET_PARAM_DATA_OFFSET], rsp.param_data, rsp.param_len);
                payload_len = (uint16_t)(USB_SET_PARAM_FIXED_PAYLOAD_LEN + rsp.param_len);
            }
            else if (rsp.opcode == USB_CMD_START_SCAN || rsp.opcode == USB_CMD_SET_SCAN_LINES || rsp.opcode == USB_CMD_STOP_SCAN || rsp.opcode == USB_CMD_START_WARMUP)
            {
                encode_u32_le(payload, rsp.target_scan_lines);
                encode_u32_le(&payload[USB_GET_PARAM_PAYLOAD_LEN], rsp.completed_scan_lines);
                payload_len = USB_SCAN_STATUS_PAYLOAD_LEN;
            }
            else if (is_debug_passthrough_opcode(rsp.opcode) || is_domain_payload_opcode(rsp.opcode))
            {
                if (rsp.debug_payload_len > USB_DEBUG_PASSTHROUGH_MAX_FRAME_PAYLOAD)
                {
                    continue;
                }
                memcpy(payload, rsp.debug_payload, rsp.debug_payload_len);
                payload_len = rsp.debug_payload_len;
            }

            uint8_t header[USB_RESPONSE_HEADER_SIZE] = {USB_FRAME_OUT_MARKER, rsp.opcode, rsp.status, 0, 0};
            encode_u16_le(&header[3], payload_len);
            if (writable < (uint32_t)(USB_RESPONSE_HEADER_SIZE + payload_len))
            {
                (void)queue_try_add(&usb_tx_queue, &rsp);
                break;
            }

            tud_vendor_write(header, sizeof(header));
            if (payload_len > 0)
            {
                tud_vendor_write(payload, payload_len);
            }
            writable -= (uint32_t)(USB_RESPONSE_HEADER_SIZE + payload_len);
        }
        tud_vendor_write_flush();
    }
}
