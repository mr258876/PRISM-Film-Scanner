/*
 * Copyright (c) 2026 mr258876
 * SPDX-License-Identifier: MIT
 */

#ifndef USB_TASK_H
#define USB_TASK_H

#include <stdbool.h>
#include <stdint.h>

#include "persistent_params.h"

enum {
    USB_CMD_GET_PARAM_BY_HASH = 0x20,
    USB_CMD_SET_PARAM_BY_HASH = 0x21,
    USB_CMD_START_SCAN = 0x30,
    USB_CMD_SET_SCAN_LINES = 0x31,
    USB_CMD_STOP_SCAN = 0x32,
    USB_CMD_START_WARMUP = 0x33,
    USB_CMD_DEBUG_PASSTHROUGH = 0xF0
};

enum {
    USB_DEBUG_TARGET_BOARD102 = 0x01
};

enum {
    USB_FRAME_IN_MARKER = 0xA5,
    USB_FRAME_OUT_MARKER = 0x5A
};

enum {
    USB_PARAM_TYPE_U8 = 1,
    USB_PARAM_TYPE_U16 = 2,
    USB_PARAM_TYPE_U32 = 3,
    USB_PARAM_TYPE_I32 = 4,
    USB_PARAM_TYPE_F32 = 5,
    USB_PARAM_TYPE_BYTES = 0x80
};

enum {
    USB_PARAM_MAX_DATA_LEN = 32,
    USB_DEBUG_PASSTHROUGH_MAX_SUBPAYLOAD = 56,
    USB_DEBUG_PASSTHROUGH_MAX_FRAME_PAYLOAD = 59
};

enum {
    USB_STATUS_OK = 0x00,
    USB_STATUS_QUEUE_FULL = 0xE1,
    USB_STATUS_BAD_FRAME = 0xE2,
    USB_STATUS_FLASH_FAIL = 0xE3,
    USB_STATUS_PARAM_NOT_FOUND = 0xE4,
    USB_STATUS_SCAN_LINES_INVALID = 0xE5,
    USB_STATUS_BUSY = 0xE6,
    USB_STATUS_PARAM_TYPE_MISMATCH = 0xE7,
    USB_STATUS_PARAM_LEN_INVALID = 0xE8,
    USB_STATUS_PAYLOAD_INVALID = 0xE9,
    USB_STATUS_DEBUG_TARGET_UNSUPPORTED = 0xEA,
    USB_STATUS_SUBORDINATE_TIMEOUT = 0xEB,
    USB_STATUS_SUBORDINATE_LINK_ERROR = 0xEC
};

typedef struct {
    uint8_t type;
    uint32_t key_hash;
    uint8_t param_type;
    uint8_t param_len;
    uint8_t param_data[USB_PARAM_MAX_DATA_LEN];
    uint32_t scan_lines;
    uint16_t debug_payload_len;
    uint8_t debug_payload[USB_DEBUG_PASSTHROUGH_MAX_FRAME_PAYLOAD];
    prism_params_t params;
} usb_command_t;

typedef struct {
    uint8_t status;
    uint8_t opcode;
    uint8_t param_type;
    uint8_t param_len;
    uint32_t key_hash;
    uint8_t param_data[USB_PARAM_MAX_DATA_LEN];
    uint32_t target_scan_lines;
    uint32_t completed_scan_lines;
    uint16_t debug_payload_len;
    uint8_t debug_payload[USB_DEBUG_PASSTHROUGH_MAX_FRAME_PAYLOAD];
    prism_params_t params;
} usb_response_t;

void usb_task_init(void);
void usb_task_core1_main(void);
bool usb_task_try_recv(usb_command_t *cmd);
void usb_task_send_blocking(const usb_response_t *rsp);

#endif
