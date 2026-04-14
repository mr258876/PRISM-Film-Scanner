/*
 * Copyright (c) 2026 mr258876
 * SPDX-License-Identifier: MIT
 */

#include "pericontrol_link.h"

#include "hardware/gpio.h"
#include "hardware/uart.h"
#include "pico/stdlib.h"

#include "Pinouts.h"
#include <stdint.h>

#define PERICONTROL_UART_ID           uart0
#define PERICONTROL_UART_BAUD         115200u
#define PERICONTROL_REPLY_TIMEOUT_US  20000u
#define PERICONTROL_REQUEST_HEADER_SIZE 4u
#define PERICONTROL_RESPONSE_HEADER_SIZE 5u
#define PERICONTROL_CRC_SIZE 2u

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

static bool is_valid_board102_opcode(uint8_t opcode)
{
    switch (opcode) {
        case PERICONTROL_CMD_GET_PARAM_BY_HASH:
        case PERICONTROL_CMD_SET_PARAM_BY_HASH:
        case PERICONTROL_CMD_GET_STATUS:
        case PERICONTROL_CMD_GET_ILLUMINATION_STATUS:
        case PERICONTROL_CMD_SET_LED_LEVELS:
        case PERICONTROL_CMD_SET_STEADY_ILLUMINATION:
        case PERICONTROL_CMD_CONFIG_LED_SYNC:
        case PERICONTROL_CMD_SET_SYNC_PULSE_CLK:
        case PERICONTROL_CMD_GET_MOTION_STATUS:
        case PERICONTROL_CMD_SET_MOTOR_ENABLE:
        case PERICONTROL_CMD_MOVE_MOTOR_STEPS:
        case PERICONTROL_CMD_STOP_MOTOR:
        case PERICONTROL_CMD_APPLY_MOTOR_CONFIG:
        case PERICONTROL_CMD_READ_TMC_REG:
        case PERICONTROL_CMD_WRITE_TMC_REG:
            return true;
        default:
            return false;
    }
}

static void pericontrol_uart_flush_input(void)
{
    while (uart_is_readable(PERICONTROL_UART_ID)) {
        (void)uart_getc(PERICONTROL_UART_ID);
    }
}

void pericontrol_link_init(void)
{
    uart_init(PERICONTROL_UART_ID, PERICONTROL_UART_BAUD);
    gpio_set_function(PERICONTROL_UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(PERICONTROL_UART_RX_PIN, GPIO_FUNC_UART);
    uart_set_format(PERICONTROL_UART_ID, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(PERICONTROL_UART_ID, false);
    pericontrol_uart_flush_input();
}

pericontrol_link_result_t pericontrol_link_transceive(uint8_t opcode,
                                                      const uint8_t *tx_payload,
                                                      uint16_t tx_payload_len,
                                                      uint8_t *status_out,
                                                      uint16_t *rx_payload_len_out,
                                                      uint8_t *rx_payload_out,
                                                      uint16_t rx_payload_max_len)
{
    if (!is_valid_board102_opcode(opcode) || tx_payload_len > PERICONTROL_MAX_PAYLOAD ||
        (tx_payload_len > 0u && tx_payload == NULL) ||
        status_out == NULL || rx_payload_len_out == NULL || rx_payload_out == NULL)
    {
        return PERICONTROL_LINK_INVALID_ARGUMENT;
    }

    uint8_t header[PERICONTROL_REQUEST_HEADER_SIZE] = {PERICONTROL_FRAME_IN_MARKER, opcode, 0, 0};
    encode_u16_le(&header[2], tx_payload_len);
    uint16_t tx_crc = crc16_ccitt(header, sizeof(header));
    if (tx_payload_len > 0u) {
        for (uint16_t i = 0; i < tx_payload_len; i++) {
            tx_crc = crc16_ccitt_update(tx_crc, tx_payload[i]);
        }
    }
    uint8_t tx_crc_bytes[PERICONTROL_CRC_SIZE] = {0};
    encode_u16_le(tx_crc_bytes, tx_crc);

    pericontrol_uart_flush_input();
    uart_write_blocking(PERICONTROL_UART_ID, header, sizeof(header));
    if (tx_payload_len > 0u) {
        uart_write_blocking(PERICONTROL_UART_ID, tx_payload, tx_payload_len);
    }
    uart_write_blocking(PERICONTROL_UART_ID, tx_crc_bytes, sizeof(tx_crc_bytes));

    uint8_t response_header[PERICONTROL_RESPONSE_HEADER_SIZE] = {0};
    uint32_t response_header_pos = 0;
    absolute_time_t deadline = make_timeout_time_us(PERICONTROL_REPLY_TIMEOUT_US);

    while (!time_reached(deadline) && response_header_pos < sizeof(response_header)) {
        if (!uart_is_readable(PERICONTROL_UART_ID)) {
            tight_loop_contents();
            continue;
        }

        uint8_t byte = (uint8_t)uart_getc(PERICONTROL_UART_ID);
        if (response_header_pos == 0u && byte != PERICONTROL_FRAME_OUT_MARKER) {
            continue;
        }

        response_header[response_header_pos++] = byte;
    }

    if (response_header_pos != sizeof(response_header)) {
        return PERICONTROL_LINK_TIMEOUT;
    }

    if (response_header[0] != PERICONTROL_FRAME_OUT_MARKER || response_header[1] != opcode) {
        return PERICONTROL_LINK_BAD_RESPONSE;
    }

    uint16_t rx_payload_len = decode_u16_le(&response_header[3]);
    if (rx_payload_len > rx_payload_max_len || rx_payload_len > PERICONTROL_MAX_PAYLOAD) {
        return PERICONTROL_LINK_BAD_RESPONSE;
    }

    uint16_t rx_payload_pos = 0;
    while (!time_reached(deadline) && rx_payload_pos < rx_payload_len) {
        if (!uart_is_readable(PERICONTROL_UART_ID)) {
            tight_loop_contents();
            continue;
        }

        rx_payload_out[rx_payload_pos++] = (uint8_t)uart_getc(PERICONTROL_UART_ID);
    }

    if (rx_payload_pos != rx_payload_len) {
        return PERICONTROL_LINK_TIMEOUT;
    }

    uint8_t rx_crc_bytes[PERICONTROL_CRC_SIZE] = {0};
    uint16_t rx_crc_pos = 0;
    while (!time_reached(deadline) && rx_crc_pos < PERICONTROL_CRC_SIZE) {
        if (!uart_is_readable(PERICONTROL_UART_ID)) {
            tight_loop_contents();
            continue;
        }

        rx_crc_bytes[rx_crc_pos++] = (uint8_t)uart_getc(PERICONTROL_UART_ID);
    }

    if (rx_crc_pos != PERICONTROL_CRC_SIZE) {
        return PERICONTROL_LINK_TIMEOUT;
    }

    uint16_t expected_crc = crc16_ccitt(response_header, sizeof(response_header));
    for (uint16_t i = 0; i < rx_payload_len; i++) {
        expected_crc = crc16_ccitt_update(expected_crc, rx_payload_out[i]);
    }
    uint16_t received_crc = decode_u16_le(rx_crc_bytes);
    if (received_crc != expected_crc) {
        return PERICONTROL_LINK_CRC_MISMATCH;
    }

    *status_out = response_header[2];
    *rx_payload_len_out = rx_payload_len;
    return PERICONTROL_LINK_OK;
}
