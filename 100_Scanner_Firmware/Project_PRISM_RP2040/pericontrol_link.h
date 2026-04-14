/*
 * Copyright (c) 2026 mr258876
 * SPDX-License-Identifier: MIT
 */

#ifndef PERICONTROL_LINK_H
#define PERICONTROL_LINK_H

#include <stdbool.h>
#include <stdint.h>

enum {
    PERICONTROL_CMD_GET_PARAM_BY_HASH   = 0x20,
    PERICONTROL_CMD_SET_PARAM_BY_HASH   = 0x21,
    PERICONTROL_CMD_GET_STATUS          = 0x22,
    PERICONTROL_CMD_GET_ILLUMINATION_STATUS = 0x40,
    PERICONTROL_CMD_SET_LED_LEVELS      = 0x41,
    PERICONTROL_CMD_SET_STEADY_ILLUMINATION = 0x42,
    PERICONTROL_CMD_CONFIG_LED_SYNC     = 0x43,
    PERICONTROL_CMD_SET_SYNC_PULSE_CLK  = 0x44,
    PERICONTROL_CMD_GET_MOTION_STATUS   = 0x50,
    PERICONTROL_CMD_SET_MOTOR_ENABLE    = 0x51,
    PERICONTROL_CMD_MOVE_MOTOR_STEPS    = 0x52,
    PERICONTROL_CMD_STOP_MOTOR          = 0x53,
    PERICONTROL_CMD_APPLY_MOTOR_CONFIG  = 0x54,
    PERICONTROL_CMD_READ_TMC_REG        = 0x55,
    PERICONTROL_CMD_WRITE_TMC_REG       = 0x56
};

enum {
    PERICONTROL_FRAME_IN_MARKER = 0xA6,
    PERICONTROL_FRAME_OUT_MARKER = 0x6A,
    PERICONTROL_MAX_PAYLOAD = 58u
};

enum {
    PERICONTROL_LINK_OK = 0,
    PERICONTROL_LINK_INVALID_ARGUMENT,
    PERICONTROL_LINK_TIMEOUT,
    PERICONTROL_LINK_BAD_RESPONSE,
    PERICONTROL_LINK_CRC_MISMATCH
};

typedef uint8_t pericontrol_link_result_t;

void pericontrol_link_init(void);
pericontrol_link_result_t pericontrol_link_transceive(uint8_t opcode,
                                                      const uint8_t *tx_payload,
                                                      uint16_t tx_payload_len,
                                                      uint8_t *status_out,
                                                      uint16_t *rx_payload_len_out,
                                                      uint8_t *rx_payload_out,
                                                      uint16_t rx_payload_max_len);

#endif
