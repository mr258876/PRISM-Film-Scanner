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

void usb_task_init(void)
{
    queue_init(&usb_rx_queue, sizeof(usb_command_t), 64);
    queue_init(&usb_tx_queue, sizeof(usb_response_t), 64);
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

void usb_task_core1_main(void)
{
    flash_safe_execute_core_init();
    tusb_init();

    uint8_t rx_buf[CFG_TUD_VENDOR_EPSIZE];
    uint8_t frame_opcode = 0;
    uint16_t frame_len = 0;
    uint8_t frame_pos = 0;
    uint8_t frame_payload[64];
    bool frame_wait_opcode = false;
    uint8_t frame_len_pos = 0;
    uint8_t frame_len_bytes[2] = {0};

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

                if (i == 0)
                {
                    if (byte != USB_FRAME_IN_MARKER)
                    {
                        push_status_response(frame_opcode, USB_STATUS_BAD_FRAME);
                        i = count; // break out of loop
                        continue;
                    }
                    else
                    {
                        frame_wait_opcode = true;
                        frame_len_pos = 0;
                        frame_pos = 0;
                        continue;
                    }
                }

                if (frame_wait_opcode)
                {
                    frame_opcode = byte;
                    switch (frame_opcode)
                    {
                    case USB_CMD_GET_PARAM_BY_HASH:
                    case USB_CMD_SET_PARAM_BY_HASH:
                    case USB_CMD_START_SCAN:
                    case USB_CMD_SET_SCAN_LINES:
                    case USB_CMD_STOP_SCAN:
                    case USB_CMD_START_WARMUP:
                        break;
                    default:
                        push_status_response(frame_opcode, USB_STATUS_BAD_FRAME);
                        i = count; // break out of loop
                        continue;
                    }
                    frame_wait_opcode = false;
                    frame_len_pos = 0;
                    frame_pos = 0;
                    continue;
                }

                if (frame_len_pos < 2)
                {
                    frame_len_bytes[frame_len_pos++] = byte;
                    if (frame_len_pos < 2)
                    {
                        continue;
                    }

                    frame_len = decode_u16_le(frame_len_bytes);

                    bool len_valid = false;
                    switch (frame_opcode)
                    {
                    case USB_CMD_START_SCAN:
                    case USB_CMD_STOP_SCAN:
                    case USB_CMD_START_WARMUP:
                        len_valid = (frame_len == 0);
                        break;
                    case USB_CMD_GET_PARAM_BY_HASH:
                    case USB_CMD_SET_SCAN_LINES:
                        len_valid = (frame_len == 4);
                        break;
                    case USB_CMD_SET_PARAM_BY_HASH:
                        len_valid = (frame_len >= 6 && frame_len <= (uint16_t)(6 + USB_PARAM_MAX_DATA_LEN));
                        break;
                    default:
                        frame_len = 0;
                        frame_len_pos = 0;
                        continue;
                    }

                    if (!len_valid)
                    {
                        push_status_response(frame_opcode, USB_STATUS_PAYLOAD_INVALID);
                        frame_len = 0;
                        frame_len_pos = 0;
                        continue;
                    }

                    if (frame_len == 0)
                    {
                        usb_command_t cmd = {
                            .type = frame_opcode,
                        };
                        if (!queue_try_add(&usb_rx_queue, &cmd))
                        {
                            push_status_response(frame_opcode, USB_STATUS_QUEUE_FULL);
                        }
                        frame_len_pos = 0;
                    }
                    continue;
                }

                if (frame_len > 0)
                {
                    frame_payload[frame_pos++] = byte;
                    if (frame_pos >= frame_len)
                    {
                        usb_command_t cmd = {
                            .type = frame_opcode,
                        };
                        switch (frame_opcode)
                        {
                        case USB_CMD_GET_PARAM_BY_HASH:
                            cmd.key_hash = decode_u32_le(frame_payload);
                            break;
                        case USB_CMD_SET_PARAM_BY_HASH:
                            cmd.key_hash = decode_u32_le(frame_payload);
                            cmd.param_type = frame_payload[4];
                            cmd.param_len = frame_payload[5];
                            if ((uint16_t)(cmd.param_len + 6u) != frame_len || cmd.param_len > USB_PARAM_MAX_DATA_LEN)
                            {
                                push_status_response(frame_opcode, USB_STATUS_PAYLOAD_INVALID);
                                frame_len = 0;
                                frame_len_pos = 0;
                                continue;
                            }
                            memcpy(cmd.param_data, &frame_payload[6], cmd.param_len);
                            break;
                        case USB_CMD_SET_SCAN_LINES:
                            cmd.scan_lines = decode_u32_le(frame_payload);
                            break;

                        default:
                            break;
                        }

                        if (!queue_try_add(&usb_rx_queue, &cmd))
                        {
                            push_status_response(frame_opcode, USB_STATUS_QUEUE_FULL);
                        }
                        frame_len = 0;
                        frame_len_pos = 0;
                    }
                    continue;
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

            uint8_t payload[64];
            uint16_t payload_len = 0;

            if ((rsp.opcode == USB_CMD_GET_PARAM_BY_HASH || rsp.opcode == USB_CMD_SET_PARAM_BY_HASH) && rsp.status == USB_STATUS_OK)
            {
                encode_u32_le(payload, rsp.key_hash);
                payload[4] = rsp.param_type;
                payload[5] = rsp.param_len;
                if (rsp.param_len > USB_PARAM_MAX_DATA_LEN)
                {
                    continue;
                }
                memcpy(&payload[6], rsp.param_data, rsp.param_len);
                payload_len = (uint16_t)(6 + rsp.param_len);
            }
            else if (rsp.opcode == USB_CMD_START_SCAN || rsp.opcode == USB_CMD_SET_SCAN_LINES || rsp.opcode == USB_CMD_STOP_SCAN || rsp.opcode == USB_CMD_START_WARMUP)
            {
                encode_u32_le(payload, rsp.target_scan_lines);
                encode_u32_le(&payload[4], rsp.completed_scan_lines);
                payload_len = 8;
            }

            uint8_t header[5] = {USB_FRAME_OUT_MARKER, rsp.opcode, rsp.status, 0, 0};
            encode_u16_le(&header[3], payload_len);
            if (writable < (uint32_t)(5 + payload_len))
            {
                (void)queue_try_add(&usb_tx_queue, &rsp);
                break;
            }

            tud_vendor_write(header, sizeof(header));
            if (payload_len > 0)
            {
                tud_vendor_write(payload, payload_len);
            }
            writable -= (uint32_t)(5 + payload_len);
        }
        tud_vendor_write_flush();
    }
}
