/*
 * Copyright (c) 2026 mr258876
 * SPDX-License-Identifier: MIT
 */

#include "uart_task.h"

#include "hardware/gpio.h"
#include "hardware/uart.h"
#include "pico/stdlib.h"

#include "Pinouts.h"

#define INTERBOARD_UART_ID      uart1
#define INTERBOARD_UART_BAUD    115200u
#define UART_RX_FRAME_TIMEOUT_US 20000u
#define UART_REQUEST_HEADER_SIZE 4u
#define UART_RESPONSE_HEADER_SIZE 5u
#define UART_CRC_SIZE 2u

typedef enum {
    UART_RX_STATE_WAIT_MARKER = 0,
    UART_RX_STATE_WAIT_OPCODE,
    UART_RX_STATE_WAIT_LEN,
    UART_RX_STATE_WAIT_PAYLOAD,
    UART_RX_STATE_WAIT_CRC
} uart_rx_state_t;

static uart_rx_state_t g_rx_state = UART_RX_STATE_WAIT_MARKER;
static uint8_t g_frame_opcode = 0;
static uint16_t g_frame_len = 0;
static uint8_t g_frame_pos = 0;
static uint8_t g_frame_payload[CONTROL_FRAME_MAX_PAYLOAD];
static uint8_t g_frame_len_pos = 0;
static uint8_t g_frame_len_bytes[2] = {0};
static uint8_t g_frame_crc_pos = 0;
static uint8_t g_frame_crc_bytes[UART_CRC_SIZE] = {0};
static uint64_t g_last_rx_byte_us = 0;

static uint16_t decode_u16_le(const uint8_t *in)
{
    return (uint16_t)(((uint16_t)in[0]) | ((uint16_t)in[1] << 8));
}

static void encode_u16_le(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)(value & 0xFFu);
    out[1] = (uint8_t)((value >> 8) & 0xFFu);
}

static uint16_t crc16_ccitt_update(uint16_t crc, uint8_t data)
{
    crc ^= (uint16_t)data << 8;
    for (uint8_t i = 0; i < 8u; i++) {
        if ((crc & 0x8000u) != 0u) {
            crc = (uint16_t)((crc << 1) ^ 0x1021u);
        } else {
            crc <<= 1;
        }
    }
    return crc;
}

static uint16_t crc16_ccitt(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFFu;
    for (uint16_t i = 0; i < len; i++) {
        crc = crc16_ccitt_update(crc, data[i]);
    }
    return crc;
}

static bool is_valid_frame_opcode(uint8_t opcode)
{
    switch (opcode) {
        case CONTROL_CMD_GET_PARAM_BY_HASH:
        case CONTROL_CMD_SET_PARAM_BY_HASH:
        case CONTROL_CMD_GET_STATUS:
        case CONTROL_CMD_SET_LED_LEVELS:
        case CONTROL_CMD_SET_MOTOR_ENABLE:
        case CONTROL_CMD_MOVE_MOTOR_STEPS:
        case CONTROL_CMD_STOP_MOTOR:
        case CONTROL_CMD_READ_TMC_REG:
        case CONTROL_CMD_WRITE_TMC_REG:
        case CONTROL_CMD_APPLY_MOTOR_CONFIG:
        case CONTROL_CMD_CONFIG_LED_SYNC:
            return true;
        default:
            return false;
    }
}

static void reset_rx_frame(void)
{
    g_rx_state = UART_RX_STATE_WAIT_MARKER;
    g_frame_opcode = 0;
    g_frame_len = 0;
    g_frame_pos = 0;
    g_frame_len_pos = 0;
    g_frame_crc_pos = 0;
    g_last_rx_byte_us = 0;
}

void uart_task_init(void)
{
    uart_init(INTERBOARD_UART_ID, INTERBOARD_UART_BAUD);
    gpio_set_function(INTERBOARD_UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(INTERBOARD_UART_RX_PIN, GPIO_FUNC_UART);
    uart_set_format(INTERBOARD_UART_ID, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(INTERBOARD_UART_ID, false);

    reset_rx_frame();
}

bool uart_task_try_recv(control_command_t *cmd)
{
    while (uart_is_readable(INTERBOARD_UART_ID)) {
        uint64_t now = time_us_64();
        if (g_rx_state != UART_RX_STATE_WAIT_MARKER && g_last_rx_byte_us != 0u &&
            (now - g_last_rx_byte_us) > UART_RX_FRAME_TIMEOUT_US)
        {
            reset_rx_frame();
        }

        uint8_t byte = (uint8_t)uart_getc(INTERBOARD_UART_ID);
        g_last_rx_byte_us = now;

        switch (g_rx_state) {
            case UART_RX_STATE_WAIT_MARKER:
                if (byte == CONTROL_FRAME_IN_MARKER) {
                    g_rx_state = UART_RX_STATE_WAIT_OPCODE;
                    g_frame_opcode = 0;
                    g_frame_len = 0;
                    g_frame_pos = 0;
                    g_frame_len_pos = 0;
                }
                break;

            case UART_RX_STATE_WAIT_OPCODE:
                g_frame_opcode = byte;
                if (!is_valid_frame_opcode(g_frame_opcode)) {
                    reset_rx_frame();
                    break;
                }
                g_rx_state = UART_RX_STATE_WAIT_LEN;
                break;

            case UART_RX_STATE_WAIT_LEN:
                g_frame_len_bytes[g_frame_len_pos++] = byte;
                if (g_frame_len_pos < 2u) {
                    break;
                }

                g_frame_len = decode_u16_le(g_frame_len_bytes);
                if (g_frame_len > CONTROL_FRAME_MAX_PAYLOAD) {
                    reset_rx_frame();
                    break;
                }

                if (g_frame_len == 0u) {
                    g_rx_state = UART_RX_STATE_WAIT_CRC;
                    g_frame_crc_pos = 0;
                    break;
                }

                g_rx_state = UART_RX_STATE_WAIT_PAYLOAD;
                g_frame_pos = 0;
                break;

            case UART_RX_STATE_WAIT_PAYLOAD:
                g_frame_payload[g_frame_pos++] = byte;
                if (g_frame_pos >= g_frame_len) {
                    g_rx_state = UART_RX_STATE_WAIT_CRC;
                    g_frame_crc_pos = 0;
                }
                break;

            case UART_RX_STATE_WAIT_CRC: {
                g_frame_crc_bytes[g_frame_crc_pos++] = byte;
                if (g_frame_crc_pos < UART_CRC_SIZE) {
                    break;
                }

                uint8_t header[UART_REQUEST_HEADER_SIZE] = {CONTROL_FRAME_IN_MARKER, g_frame_opcode, 0, 0};
                encode_u16_le(&header[2], g_frame_len);
                uint16_t expected_crc = crc16_ccitt(header, sizeof(header));
                for (uint16_t i = 0; i < g_frame_len; i++) {
                    expected_crc = crc16_ccitt_update(expected_crc, g_frame_payload[i]);
                }

                if (decode_u16_le(g_frame_crc_bytes) != expected_crc) {
                    reset_rx_frame();
                    break;
                }

                cmd->type = g_frame_opcode;
                cmd->payload_len = g_frame_len;
                for (uint16_t i = 0; i < g_frame_len; i++) {
                    cmd->payload[i] = g_frame_payload[i];
                }
                reset_rx_frame();
                return true;
            }
        }
    }

    return false;
}

void uart_task_send_blocking(const control_response_t *rsp)
{
    uint8_t header[UART_RESPONSE_HEADER_SIZE] = {
        CONTROL_FRAME_OUT_MARKER,
        rsp->opcode,
        rsp->status,
        0,
        0
    };
    encode_u16_le(&header[3], rsp->payload_len);

    uint16_t crc = crc16_ccitt(header, sizeof(header));
    for (uint16_t i = 0; i < rsp->payload_len; i++) {
        crc = crc16_ccitt_update(crc, rsp->payload[i]);
    }
    uint8_t crc_bytes[UART_CRC_SIZE] = {0};
    encode_u16_le(crc_bytes, crc);

    uart_write_blocking(INTERBOARD_UART_ID, header, sizeof(header));
    if (rsp->payload_len > 0u) {
        uart_write_blocking(INTERBOARD_UART_ID, rsp->payload, rsp->payload_len);
    }
    uart_write_blocking(INTERBOARD_UART_ID, crc_bytes, sizeof(crc_bytes));
}
