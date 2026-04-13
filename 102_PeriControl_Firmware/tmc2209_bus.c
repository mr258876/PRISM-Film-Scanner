/*
 * Copyright (c) 2026 mr258876
 * SPDX-License-Identifier: MIT
 */

#include "tmc2209_bus.h"

#include "hardware/gpio.h"
#include "hardware/uart.h"
#include "pico/stdlib.h"

#include "Pinouts.h"

#define TMC_UART_ID             uart0
#define TMC_UART_BAUD           115200u
#define TMC_REPLY_TIMEOUT_US    8000u

#define TMC_SYNC_BYTE           0x05u
#define TMC_WRITE_FLAG          0x80u

#define TMC_REG_GCONF           0x00u
#define TMC_REG_IHOLD_IRUN      0x10u
#define TMC_REG_CHOPCONF        0x6Cu

static const uint8_t g_tmc_addresses[MOTOR_COUNT] = {
    TMC_MOTOR1_ADDRESS,
    TMC_MOTOR2_ADDRESS,
    TMC_MOTOR3_ADDRESS,
};

static uint8_t tmc_crc8(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0;
    for (uint8_t i = 0; i < len; i++) {
        uint8_t current = data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (((crc >> 7) ^ (current & 0x01u)) != 0u) {
                crc = (uint8_t)((crc << 1) ^ 0x07u);
            } else {
                crc <<= 1;
            }
            current >>= 1;
        }
    }
    return crc;
}

static uint8_t microsteps_to_mres(uint16_t microsteps)
{
    switch (microsteps) {
        case 256: return 0u;
        case 128: return 1u;
        case 64: return 2u;
        case 32: return 3u;
        case 16: return 4u;
        case 8: return 5u;
        case 4: return 6u;
        case 2: return 7u;
        case 1: return 8u;
        default: return 4u;
    }
}

static void tmc_uart_flush_input(void)
{
    while (uart_is_readable(TMC_UART_ID)) {
        (void)uart_getc(TMC_UART_ID);
    }
}

void tmc2209_bus_init(void)
{
    uart_init(TMC_UART_ID, TMC_UART_BAUD);
    gpio_set_function(TMC_UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(TMC_UART_RX_PIN, GPIO_FUNC_UART);
    uart_set_format(TMC_UART_ID, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(TMC_UART_ID, false);
    tmc_uart_flush_input();
}

bool tmc2209_write_register(uint8_t motor_index, uint8_t reg_addr, uint32_t value)
{
    if (motor_index >= MOTOR_COUNT) {
        return false;
    }

    uint8_t frame[8] = {
        TMC_SYNC_BYTE,
        g_tmc_addresses[motor_index],
        (uint8_t)(reg_addr | TMC_WRITE_FLAG),
        (uint8_t)((value >> 24) & 0xFFu),
        (uint8_t)((value >> 16) & 0xFFu),
        (uint8_t)((value >> 8) & 0xFFu),
        (uint8_t)(value & 0xFFu),
        0u
    };
    frame[7] = tmc_crc8(frame, 7);

    tmc_uart_flush_input();
    uart_write_blocking(TMC_UART_ID, frame, sizeof(frame));
    sleep_us(200);
    tmc_uart_flush_input();
    return true;
}

bool tmc2209_read_register(uint8_t motor_index, uint8_t reg_addr, uint32_t *value_out)
{
    if (motor_index >= MOTOR_COUNT || value_out == NULL) {
        return false;
    }

    uint8_t frame[4] = {
        TMC_SYNC_BYTE,
        g_tmc_addresses[motor_index],
        reg_addr,
        0u
    };
    frame[3] = tmc_crc8(frame, 3);

    uint8_t rx_buf[16] = {0};
    uint32_t rx_count = 0;

    tmc_uart_flush_input();
    uart_write_blocking(TMC_UART_ID, frame, sizeof(frame));

    absolute_time_t deadline = make_timeout_time_us(TMC_REPLY_TIMEOUT_US);
    while (!time_reached(deadline) && rx_count < sizeof(rx_buf)) {
        if (!uart_is_readable(TMC_UART_ID)) {
            tight_loop_contents();
            continue;
        }
        rx_buf[rx_count++] = (uint8_t)uart_getc(TMC_UART_ID);
    }

    if (rx_count < 8u) {
        return false;
    }

    for (uint32_t start = rx_count - 8u + 1u; start > 0u; start--) {
        uint32_t idx = start - 1u;
        uint8_t *candidate = &rx_buf[idx];
        if (candidate[0] != TMC_SYNC_BYTE) {
            continue;
        }
        if (candidate[2] != reg_addr) {
            continue;
        }
        if (tmc_crc8(candidate, 7) != candidate[7]) {
            continue;
        }

        *value_out = ((uint32_t)candidate[3] << 24) |
                     ((uint32_t)candidate[4] << 16) |
                     ((uint32_t)candidate[5] << 8) |
                     (uint32_t)candidate[6];
        return true;
    }

    return false;
}

bool tmc2209_apply_basic_config(uint8_t motor_index, uint8_t irun, uint8_t ihold, uint16_t microsteps, bool stealthchop_enable)
{
    uint8_t mres = microsteps_to_mres(microsteps);
    uint32_t gconf = (1u << 6) | (1u << 7);
    if (!stealthchop_enable) {
        gconf |= (1u << 2);
    }

    uint32_t ihold_irun = ((uint32_t)(ihold & 0x1Fu)) |
                          ((uint32_t)(irun & 0x1Fu) << 8) |
                          (6u << 16);

    uint32_t chopconf = 3u |
                        (5u << 4) |
                        (1u << 7) |
                        (2u << 15) |
                        ((uint32_t)mres << 24) |
                        (1u << 28);

    return tmc2209_write_register(motor_index, TMC_REG_GCONF, gconf) &&
           tmc2209_write_register(motor_index, TMC_REG_IHOLD_IRUN, ihold_irun) &&
           tmc2209_write_register(motor_index, TMC_REG_CHOPCONF, chopconf);
}
