/*
 * Copyright (c) 2026 mr258876
 * SPDX-License-Identifier: MIT
 */

#include "tusb.h"
#include "pico/unique_id.h"
#include <string.h>

enum {
    ITF_NUM_VENDOR = 0,
    ITF_NUM_TOTAL
};

enum {
    EPNUM_VENDOR_OUT = 0x01,
    EPNUM_VENDOR_IN  = 0x81
};

enum {
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
    STRID_VENDOR_IF
};

#define USB_VID         0x1D50
#define USB_PID         0x619D
#define USB_BCD         0x0200

#define MS_OS_10_VENDOR_CODE   0x20
#define MS_OS_10_DESC_INDEX    0xEE

#define CONFIG_TOTAL_LEN    (TUD_CONFIG_DESC_LEN + TUD_VENDOR_DESC_LEN)

static const tusb_desc_device_t desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = USB_BCD,
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = USB_VID,
    .idProduct          = USB_PID,
    .bcdDevice          = 0x0100,
    .iManufacturer      = STRID_MANUFACTURER,
    .iProduct           = STRID_PRODUCT,
    .iSerialNumber      = STRID_SERIAL,
    .bNumConfigurations = 1
};

static const uint8_t desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 500),
    TUD_VENDOR_DESCRIPTOR(ITF_NUM_VENDOR, STRID_VENDOR_IF, EPNUM_VENDOR_OUT, EPNUM_VENDOR_IN, CFG_TUD_VENDOR_EPSIZE)
};

static const uint16_t desc_ms_os_10_string[] = {
    0x0312,
    0x004D, 0x0053, 0x0046, 0x0054, 0x0031, 0x0030, 0x0030,
    MS_OS_10_VENDOR_CODE
};

static const uint8_t desc_ms_os_10_compat_id[] = {
    U32_TO_U8S_LE(0x00000028),
    U16_TO_U8S_LE(0x0100),
    U16_TO_U8S_LE(0x0004),
    0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

    ITF_NUM_VENDOR,
    0x01,
    'W', 'I', 'N', 'U', 'S', 'B', 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const uint8_t desc_ms_os_10_ext_props[] = {
    U32_TO_U8S_LE(0x00000092),
    U16_TO_U8S_LE(0x0100),
    U16_TO_U8S_LE(0x0005),
    U16_TO_U8S_LE(0x0001),

    U32_TO_U8S_LE(0x00000088),
    U32_TO_U8S_LE(0x00000007),
    U16_TO_U8S_LE(0x002A),
    'D', 0, 'e', 0, 'v', 0, 'i', 0, 'c', 0, 'e', 0,
    'I', 0, 'n', 0, 't', 0, 'e', 0, 'r', 0, 'f', 0, 'a', 0, 'c', 0, 'e', 0,
    'G', 0, 'U', 0, 'I', 0, 'D', 0, 's', 0,
    0x00, 0x00,

    U32_TO_U8S_LE(0x00000050),  // {c53eff31-7ff8-b1c3-dc8d-d6ab661c9c5b} MD5 of "Project PRISM Control Interface"
    '{', 0,
    'c', 0, '5', 0, '3', 0, 'e', 0, 'f', 0, 'f', 0, '3', 0, '1', 0,
    '-', 0,
    '7', 0, 'f', 0, 'f', 0, '8', 0,
    '-', 0,
    'b', 0, '1', 0, 'c', 0, '3', 0,
    '-', 0,
    'd', 0, 'c', 0, '8', 0, 'd', 0,
    '-', 0,
    'd', 0, '6', 0, 'a', 0, 'b', 0, '6', 0, '6', 0, '1', 0, 'c', 0, '9', 0, 'c', 0, '5', 0, 'b', 0,
    '}', 0,
    0x00, 0x00,
    0x00, 0x00
};

static const char *const string_desc_arr[] = {
    (const char[]){0x09, 0x04},
    "mr258876",
    "Project PRISM Control Interface",
    "",
    "Project PRISM Control Interface"
};

static uint16_t desc_str[32];
static char serial_str[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2 + 1];

const uint8_t *tud_descriptor_device_cb(void)
{
    return (const uint8_t *)&desc_device;
}

const uint8_t *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return desc_configuration;
}

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;

    if (index == MS_OS_10_DESC_INDEX) {
        return desc_ms_os_10_string;
    }

    uint8_t chr_count;

    if (index == STRID_LANGID) {
        memcpy(&desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
    } else {
        if (!(index < (sizeof(string_desc_arr) / sizeof(string_desc_arr[0])))) {
            return NULL;
        }

        const char *str = string_desc_arr[index];
        if (index == STRID_SERIAL) {
            pico_get_unique_board_id_string(serial_str, sizeof(serial_str));
            str = serial_str;
        }
        chr_count = (uint8_t)strlen(str);
        if (chr_count > 31) {
            chr_count = 31;
        }

        for (uint8_t i = 0; i < chr_count; i++) {
            desc_str[1 + i] = str[i];
        }
    }

    desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return desc_str;
}

bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, const tusb_control_request_t *request)
{
    if (stage != CONTROL_STAGE_SETUP) {
        return true;
    }

    if (request->bmRequestType_bit.type != TUSB_REQ_TYPE_VENDOR) {
        return false;
    }

    if (request->bmRequestType_bit.direction != TUSB_DIR_IN) {
        return false;
    }

    if (request->bRequest != MS_OS_10_VENDOR_CODE) {
        return false;
    }

    if (request->wIndex == 0x0004) {
        return tud_control_xfer(rhport, request, (void *)(uintptr_t)desc_ms_os_10_compat_id, sizeof(desc_ms_os_10_compat_id));
    }

    if (request->wIndex == 0x0005 && request->wValue == ITF_NUM_VENDOR) {
        return tud_control_xfer(rhport, request, (void *)(uintptr_t)desc_ms_os_10_ext_props, sizeof(desc_ms_os_10_ext_props));
    }

    return false;
}
