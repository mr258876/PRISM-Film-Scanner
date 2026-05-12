/*
 * Copyright (c) 2026 mr258876
 * SPDX-License-Identifier: MIT
 */

#include "persistent_params.h"

#include <stddef.h>
#include <string.h>

#include "hardware/flash.h"
#include "pico/flash.h"
#include "pico/stdlib.h"

#include "pericontrol_defaults.h"

#define PRISM_PARAM_STORE_MAGIC    0x50504354u
#define PRISM_PARAM_STORE_VERSION  2u

#define PRISM_PARAM_TYPE_U8        1u
#define PRISM_PARAM_TYPE_U16       2u
#define PRISM_PARAM_TYPE_U32       3u

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t payload_len;
    uint32_t crc32;
    prism_params_t params;
} prism_param_store_t;

typedef struct {
    const char *key;
    uint8_t type;
    uint8_t len;
    size_t offset;
    uint32_t hash;
} prism_param_desc_t;

static prism_param_desc_t g_param_desc[] = {
    {"prism.sys_clock_khz", PRISM_PARAM_TYPE_U32, 4, offsetof(prism_params_t, sys_clock_khz), 0},
    {"prism.led_pwm_wrap", PRISM_PARAM_TYPE_U16, 2, offsetof(prism_params_t, led_pwm_wrap), 0},
    {"prism.led1.level", PRISM_PARAM_TYPE_U16, 2, offsetof(prism_params_t, led_level[0]), 0},
    {"prism.led2.level", PRISM_PARAM_TYPE_U16, 2, offsetof(prism_params_t, led_level[1]), 0},
    {"prism.led3.level", PRISM_PARAM_TYPE_U16, 2, offsetof(prism_params_t, led_level[2]), 0},
    {"prism.led4.level", PRISM_PARAM_TYPE_U16, 2, offsetof(prism_params_t, led_level[3]), 0},
    {"prism.motor1.irun", PRISM_PARAM_TYPE_U8, 1, offsetof(prism_params_t, motor_irun[0]), 0},
    {"prism.motor2.irun", PRISM_PARAM_TYPE_U8, 1, offsetof(prism_params_t, motor_irun[1]), 0},
    {"prism.motor3.irun", PRISM_PARAM_TYPE_U8, 1, offsetof(prism_params_t, motor_irun[2]), 0},
    {"prism.motor1.ihold", PRISM_PARAM_TYPE_U8, 1, offsetof(prism_params_t, motor_ihold[0]), 0},
    {"prism.motor2.ihold", PRISM_PARAM_TYPE_U8, 1, offsetof(prism_params_t, motor_ihold[1]), 0},
    {"prism.motor3.ihold", PRISM_PARAM_TYPE_U8, 1, offsetof(prism_params_t, motor_ihold[2]), 0},
    {"prism.motor1.microsteps", PRISM_PARAM_TYPE_U16, 2, offsetof(prism_params_t, motor_microsteps[0]), 0},
    {"prism.motor2.microsteps", PRISM_PARAM_TYPE_U16, 2, offsetof(prism_params_t, motor_microsteps[1]), 0},
    {"prism.motor3.microsteps", PRISM_PARAM_TYPE_U16, 2, offsetof(prism_params_t, motor_microsteps[2]), 0},
    {"prism.motor1.stealthchop_enable", PRISM_PARAM_TYPE_U8, 1, offsetof(prism_params_t, motor_stealthchop_enable[0]), 0},
    {"prism.motor2.stealthchop_enable", PRISM_PARAM_TYPE_U8, 1, offsetof(prism_params_t, motor_stealthchop_enable[1]), 0},
    {"prism.motor3.stealthchop_enable", PRISM_PARAM_TYPE_U8, 1, offsetof(prism_params_t, motor_stealthchop_enable[2]), 0},
    {"prism.motor1.step_interval_ns", PRISM_PARAM_TYPE_U32, 4, offsetof(prism_params_t, motor_step_interval_ns[0]), 0},
    {"prism.motor2.step_interval_ns", PRISM_PARAM_TYPE_U32, 4, offsetof(prism_params_t, motor_step_interval_ns[1]), 0},
    {"prism.motor3.step_interval_ns", PRISM_PARAM_TYPE_U32, 4, offsetof(prism_params_t, motor_step_interval_ns[2]), 0},
};

static uint32_t crc32_compute(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
            crc = (crc >> 1u) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

uint32_t prism_param_hash_key(const char *str)
{
    uint32_t hash = 2166136261u;
    while (*str) {
        hash ^= (uint8_t)*str++;
        hash *= 16777619u;
    }
    return hash;
}

static void ensure_hashes_initialized(void)
{
    static bool initialized = false;
    if (initialized) {
        return;
    }

    for (size_t i = 0; i < (sizeof(g_param_desc) / sizeof(g_param_desc[0])); i++) {
        g_param_desc[i].hash = prism_param_hash_key(g_param_desc[i].key);
    }
    initialized = true;
}

static uint32_t params_flash_offset(void)
{
    return PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE;
}

static bool microsteps_valid(uint16_t microsteps)
{
    switch (microsteps) {
        case 1:
        case 2:
        case 4:
        case 8:
        case 16:
        case 32:
        case 64:
        case 128:
        case 256:
            return true;
        default:
            return false;
    }
}

static void normalize_params(prism_params_t *params)
{
    if (params->sys_clock_khz < PRISM_MIN_SYS_CLOCK_KHZ || params->sys_clock_khz > PRISM_MAX_SYS_CLOCK_KHZ) {
        params->sys_clock_khz = PRISM_DEFAULT_SYS_CLOCK_KHZ;
    }

    if (params->led_pwm_wrap == 0) {
        params->led_pwm_wrap = PRISM_DEFAULT_LED_PWM_WRAP;
    }

    for (uint32_t i = 0; i < LED_CHANNEL_COUNT; i++) {
        if (params->led_level[i] > params->led_pwm_wrap) {
            params->led_level[i] = params->led_pwm_wrap;
        }
    }

    for (uint32_t i = 0; i < MOTOR_COUNT; i++) {
        if (params->motor_irun[i] > 31u) {
            params->motor_irun[i] = PRISM_DEFAULT_TMC_IRUN;
        }
        if (params->motor_ihold[i] > 31u) {
            params->motor_ihold[i] = PRISM_DEFAULT_TMC_IHOLD;
        }
        if (!microsteps_valid(params->motor_microsteps[i])) {
            params->motor_microsteps[i] = PRISM_DEFAULT_TMC_MICROSTEPS;
        }
        params->motor_stealthchop_enable[i] = params->motor_stealthchop_enable[i] ? 1u : 0u;
        if (params->motor_step_interval_ns[i] == 0u) {
            params->motor_step_interval_ns[i] = PRISM_DEFAULT_STEP_INTERVAL_NS;
        }
    }
}

void prism_params_set_defaults(prism_params_t *params)
{
    memset(params, 0, sizeof(*params));
    params->sys_clock_khz = PRISM_DEFAULT_SYS_CLOCK_KHZ;
    params->led_pwm_wrap = PRISM_DEFAULT_LED_PWM_WRAP;

    for (uint32_t i = 0; i < LED_CHANNEL_COUNT; i++) {
        params->led_level[i] = PRISM_DEFAULT_LED_LEVEL;
    }

    for (uint32_t i = 0; i < MOTOR_COUNT; i++) {
        params->motor_irun[i] = PRISM_DEFAULT_TMC_IRUN;
        params->motor_ihold[i] = PRISM_DEFAULT_TMC_IHOLD;
        params->motor_microsteps[i] = PRISM_DEFAULT_TMC_MICROSTEPS;
        params->motor_stealthchop_enable[i] = PRISM_DEFAULT_TMC_STEALTHCHOP_ENABLE;
        params->motor_step_interval_ns[i] = PRISM_DEFAULT_STEP_INTERVAL_NS;
    }
}

bool prism_params_load(prism_params_t *params)
{
    const prism_param_store_t *store = (const prism_param_store_t *)(XIP_BASE + params_flash_offset());
    if (store->magic != PRISM_PARAM_STORE_MAGIC ||
        store->version != PRISM_PARAM_STORE_VERSION ||
        store->payload_len != sizeof(prism_params_t))
    {
        return false;
    }

    uint32_t crc = crc32_compute((const uint8_t *)&store->params, sizeof(store->params));
    if (crc != store->crc32) {
        return false;
    }

    *params = store->params;
    normalize_params(params);
    return true;
}

static void flash_write_params_cb(void *param)
{
    const prism_param_store_t *store = (const prism_param_store_t *)param;
    flash_range_erase(params_flash_offset(), FLASH_SECTOR_SIZE);
    flash_range_program(params_flash_offset(), (const uint8_t *)store, FLASH_PAGE_SIZE);
}

bool prism_params_save(const prism_params_t *params)
{
    uint8_t page_buf[FLASH_PAGE_SIZE];
    prism_params_t normalized = *params;
    memset(page_buf, 0xFF, sizeof(page_buf));

    prism_param_store_t *store = (prism_param_store_t *)page_buf;
    store->magic = PRISM_PARAM_STORE_MAGIC;
    store->version = PRISM_PARAM_STORE_VERSION;
    store->payload_len = sizeof(prism_params_t);
    normalize_params(&normalized);
    store->params = normalized;
    store->crc32 = crc32_compute((const uint8_t *)&store->params, sizeof(store->params));

    int rc = flash_safe_execute(flash_write_params_cb, store, UINT32_MAX);
    return rc == PICO_OK;
}

static bool param_len_matches_type(uint8_t type, uint8_t len)
{
    switch (type) {
        case PRISM_PARAM_TYPE_U8:
            return len == 1;
        case PRISM_PARAM_TYPE_U16:
            return len == 2;
        case PRISM_PARAM_TYPE_U32:
            return len == 4;
        default:
            return false;
    }
}

bool prism_param_meta_by_hash(uint32_t key_hash, uint8_t *type, uint8_t *len)
{
    ensure_hashes_initialized();
    for (size_t i = 0; i < (sizeof(g_param_desc) / sizeof(g_param_desc[0])); i++) {
        if (g_param_desc[i].hash == key_hash) {
            *type = g_param_desc[i].type;
            *len = g_param_desc[i].len;
            return true;
        }
    }
    return false;
}

bool prism_param_get_by_hash(const prism_params_t *params, uint32_t key_hash, uint8_t *type, uint8_t *len, uint8_t *value)
{
    ensure_hashes_initialized();
    for (size_t i = 0; i < (sizeof(g_param_desc) / sizeof(g_param_desc[0])); i++) {
        if (g_param_desc[i].hash != key_hash) {
            continue;
        }

        *type = g_param_desc[i].type;
        *len = g_param_desc[i].len;
        memcpy(value, ((const uint8_t *)params) + g_param_desc[i].offset, *len);
        return true;
    }
    return false;
}

bool prism_param_set_by_hash(prism_params_t *params, uint32_t key_hash, uint8_t type, uint8_t len, const uint8_t *value)
{
    ensure_hashes_initialized();
    if (!param_len_matches_type(type, len)) {
        return false;
    }

    for (size_t i = 0; i < (sizeof(g_param_desc) / sizeof(g_param_desc[0])); i++) {
        if (g_param_desc[i].hash != key_hash) {
            continue;
        }

        if (g_param_desc[i].type != type || g_param_desc[i].len != len) {
            return false;
        }

        memcpy(((uint8_t *)params) + g_param_desc[i].offset, value, len);
        normalize_params(params);
        return true;
    }
    return false;
}
