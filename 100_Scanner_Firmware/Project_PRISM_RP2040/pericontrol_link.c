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
#include <string.h>

#define PERICONTROL_UART_ID           uart0
#define PERICONTROL_UART_BAUD         115200u
#define PERICONTROL_REPLY_TIMEOUT_US  20000u
#define PERICONTROL_ASYNC_FRAME_TIMEOUT_US 10000u
#define PERICONTROL_REQUEST_HEADER_SIZE 4u
#define PERICONTROL_RESPONSE_HEADER_SIZE 5u
#define PERICONTROL_CRC_SIZE 2u
#define PERICONTROL_ASYNC_QUEUE_DEPTH 4u

typedef struct {
    uint8_t opcode;
    uint8_t status;
    uint16_t payload_len;
    uint8_t payload[PERICONTROL_MAX_PAYLOAD];
} pericontrol_async_frame_t;

static pericontrol_async_frame_t g_async_queue[PERICONTROL_ASYNC_QUEUE_DEPTH];
static uint8_t g_async_head = 0u;
static uint8_t g_async_tail = 0u;
static uint8_t g_async_count = 0u;

static bool wait_uart_byte_until(absolute_time_t deadline, uint8_t *byte_out);
static pericontrol_link_result_t read_response_payload_crc(const uint8_t *response_header,
                                                           absolute_time_t deadline,
                                                           uint8_t *payload_out,
                                                           uint16_t payload_max_len,
                                                           uint16_t *payload_len_out);

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
        case PERICONTROL_CMD_PREPARE_MOTOR_ON_SYNC:
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

static void queue_async_frame(uint8_t opcode,
                              uint8_t status,
                              uint16_t payload_len,
                              const uint8_t *payload)
{
    if (payload_len > PERICONTROL_MAX_PAYLOAD) {
        return;
    }

    pericontrol_async_frame_t *frame = &g_async_queue[g_async_head];
    frame->opcode = opcode;
    frame->status = status;
    frame->payload_len = payload_len;
    if (payload_len > 0u && payload != NULL) {
        memcpy(frame->payload, payload, payload_len);
    }

    g_async_head = (uint8_t)((g_async_head + 1u) % PERICONTROL_ASYNC_QUEUE_DEPTH);
    if (g_async_count < PERICONTROL_ASYNC_QUEUE_DEPTH) {
        g_async_count++;
    } else {
        g_async_tail = (uint8_t)((g_async_tail + 1u) % PERICONTROL_ASYNC_QUEUE_DEPTH);
    }
}

static bool pop_async_frame(uint8_t *opcode_out,
                            uint8_t *status_out,
                            uint16_t *payload_len_out,
                            uint8_t *payload_out,
                            uint16_t payload_max_len)
{
    if (g_async_count == 0u) {
        return false;
    }

    const pericontrol_async_frame_t *frame = &g_async_queue[g_async_tail];
    if (frame->payload_len > payload_max_len) {
        g_async_tail = (uint8_t)((g_async_tail + 1u) % PERICONTROL_ASYNC_QUEUE_DEPTH);
        g_async_count--;
        return false;
    }

    *opcode_out = frame->opcode;
    *status_out = frame->status;
    *payload_len_out = frame->payload_len;
    memcpy(payload_out, frame->payload, frame->payload_len);
    g_async_tail = (uint8_t)((g_async_tail + 1u) % PERICONTROL_ASYNC_QUEUE_DEPTH);
    g_async_count--;
    return true;
}

static bool try_read_async_frame_from_uart(uint8_t *opcode_out,
                                           uint8_t *status_out,
                                           uint16_t *payload_len_out,
                                           uint8_t *payload_out,
                                           uint16_t payload_max_len,
                                           uint32_t timeout_us)
{
    if (!uart_is_readable(PERICONTROL_UART_ID)) {
        return false;
    }

    while (uart_is_readable(PERICONTROL_UART_ID)) {
        uint8_t marker = (uint8_t)uart_getc(PERICONTROL_UART_ID);
        if (marker != PERICONTROL_FRAME_OUT_MARKER) {
            continue;
        }

        uint8_t header[PERICONTROL_RESPONSE_HEADER_SIZE] = {0};
        header[0] = marker;
        absolute_time_t deadline = make_timeout_time_us(timeout_us);
        for (uint32_t i = 1; i < sizeof(header); i++) {
            if (!wait_uart_byte_until(deadline, &header[i])) {
                return false;
            }
        }

        uint8_t opcode = header[1];
        if (!is_valid_board102_opcode(opcode)) {
            continue;
        }

        uint16_t rx_payload_len = 0u;
        if (read_response_payload_crc(header, deadline, payload_out, payload_max_len, &rx_payload_len) != PERICONTROL_LINK_OK) {
            continue;
        }

        *opcode_out = opcode;
        *status_out = header[2];
        *payload_len_out = rx_payload_len;
        return true;
    }

    return false;
}

static void drain_ready_async_frames(void)
{
    for (uint8_t i = 0; i < PERICONTROL_ASYNC_QUEUE_DEPTH; i++) {
        uint8_t opcode = 0u;
        uint8_t status = 0u;
        uint16_t payload_len = 0u;
        uint8_t payload[PERICONTROL_MAX_PAYLOAD] = {0};
        if (!try_read_async_frame_from_uart(&opcode,
                                            &status,
                                            &payload_len,
                                            payload,
                                            sizeof(payload),
                                            PERICONTROL_ASYNC_FRAME_TIMEOUT_US)) {
            return;
        }
        queue_async_frame(opcode, status, payload_len, payload);
    }
}

static bool wait_uart_byte_until(absolute_time_t deadline, uint8_t *byte_out)
{
    while (!uart_is_readable(PERICONTROL_UART_ID)) {
        if (time_reached(deadline)) {
            return false;
        }
        tight_loop_contents();
    }

    *byte_out = (uint8_t)uart_getc(PERICONTROL_UART_ID);
    return true;
}

static pericontrol_link_result_t read_response_payload_crc(const uint8_t *response_header,
                                                           absolute_time_t deadline,
                                                           uint8_t *payload_out,
                                                           uint16_t payload_max_len,
                                                           uint16_t *payload_len_out)
{
    uint16_t rx_payload_len = decode_u16_le(&response_header[3]);
    if (rx_payload_len > payload_max_len || rx_payload_len > PERICONTROL_MAX_PAYLOAD) {
        return PERICONTROL_LINK_BAD_RESPONSE;
    }

    for (uint16_t i = 0; i < rx_payload_len; i++) {
        if (!wait_uart_byte_until(deadline, &payload_out[i])) {
            return PERICONTROL_LINK_TIMEOUT;
        }
    }

    uint8_t rx_crc_bytes[PERICONTROL_CRC_SIZE] = {0};
    for (uint16_t i = 0; i < PERICONTROL_CRC_SIZE; i++) {
        if (!wait_uart_byte_until(deadline, &rx_crc_bytes[i])) {
            return PERICONTROL_LINK_TIMEOUT;
        }
    }

    uint16_t expected_crc = crc16_ccitt(response_header, PERICONTROL_RESPONSE_HEADER_SIZE);
    for (uint16_t i = 0; i < rx_payload_len; i++) {
        expected_crc = crc16_ccitt_update(expected_crc, payload_out[i]);
    }
    if (decode_u16_le(rx_crc_bytes) != expected_crc) {
        return PERICONTROL_LINK_CRC_MISMATCH;
    }

    *payload_len_out = rx_payload_len;
    return PERICONTROL_LINK_OK;
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

    drain_ready_async_frames();

    uart_write_blocking(PERICONTROL_UART_ID, header, sizeof(header));
    if (tx_payload_len > 0u) {
        uart_write_blocking(PERICONTROL_UART_ID, tx_payload, tx_payload_len);
    }
    uart_write_blocking(PERICONTROL_UART_ID, tx_crc_bytes, sizeof(tx_crc_bytes));

    absolute_time_t deadline = make_timeout_time_us(PERICONTROL_REPLY_TIMEOUT_US);

    while (!time_reached(deadline)) {
        uint8_t response_header[PERICONTROL_RESPONSE_HEADER_SIZE] = {0};
        uint32_t response_header_pos = 0;
        while (!time_reached(deadline) && response_header_pos < sizeof(response_header)) {
            uint8_t byte = 0u;
            if (!wait_uart_byte_until(deadline, &byte)) {
                return PERICONTROL_LINK_TIMEOUT;
            }
            if (response_header_pos == 0u && byte != PERICONTROL_FRAME_OUT_MARKER) {
                continue;
            }
            response_header[response_header_pos++] = byte;
        }

        if (response_header_pos != sizeof(response_header)) {
            return PERICONTROL_LINK_TIMEOUT;
        }
        if (response_header[0] != PERICONTROL_FRAME_OUT_MARKER || !is_valid_board102_opcode(response_header[1])) {
            return PERICONTROL_LINK_BAD_RESPONSE;
        }

        uint16_t frame_payload_len = 0u;
        uint8_t frame_payload[PERICONTROL_MAX_PAYLOAD] = {0};
        pericontrol_link_result_t frame_result = read_response_payload_crc(response_header,
                                                                           deadline,
                                                                           frame_payload,
                                                                           sizeof(frame_payload),
                                                                           &frame_payload_len);
        if (frame_result != PERICONTROL_LINK_OK) {
            return frame_result;
        }

        if (response_header[1] == opcode) {
            if (frame_payload_len > rx_payload_max_len) {
                return PERICONTROL_LINK_BAD_RESPONSE;
            }
            memcpy(rx_payload_out, frame_payload, frame_payload_len);
            *status_out = response_header[2];
            *rx_payload_len_out = frame_payload_len;
            return PERICONTROL_LINK_OK;
        }
        return PERICONTROL_LINK_BAD_RESPONSE;
    }

    return PERICONTROL_LINK_TIMEOUT;
}

bool pericontrol_link_try_recv_async(uint8_t *opcode_out,
                                     uint8_t *status_out,
                                     uint16_t *rx_payload_len_out,
                                     uint8_t *rx_payload_out,
                                     uint16_t rx_payload_max_len)
{
    if (opcode_out == NULL || status_out == NULL || rx_payload_len_out == NULL ||
        rx_payload_out == NULL)
    {
        return false;
    }

    if (pop_async_frame(opcode_out, status_out, rx_payload_len_out, rx_payload_out, rx_payload_max_len)) {
        return true;
    }

    return try_read_async_frame_from_uart(opcode_out,
                                          status_out,
                                          rx_payload_len_out,
                                          rx_payload_out,
                                          rx_payload_max_len,
                                          PERICONTROL_ASYNC_FRAME_TIMEOUT_US);
}
