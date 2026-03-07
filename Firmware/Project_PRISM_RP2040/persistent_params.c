#include "persistent_params.h"

#include <string.h>

#include "hardware/flash.h"
#include "pico/flash.h"
#include "pico/stdlib.h"

#define PRISM_PARAM_STORE_MAGIC     0x50535452u
#define PRISM_PARAM_STORE_VERSION   1u
#define PRISM_PARAM_TABLE_SLOTS     64u

#define PRISM_PARAM_TYPE_U8         1u
#define PRISM_PARAM_TYPE_U16        2u
#define PRISM_PARAM_TYPE_U32        3u
#define PRISM_PARAM_TYPE_I32        4u
#define PRISM_PARAM_TYPE_F32        5u

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t slot_count;
    uint32_t reserved0;
    uint32_t reserved1;
} prism_param_store_header_t;

typedef struct __attribute__((packed)) {
    uint32_t key_hash;
    uint8_t type;
    uint8_t len;
    uint16_t reserved;
    uint32_t value;
    uint32_t crc32;
} prism_param_slot_t;

typedef struct {
    const char *key;
    uint8_t type;
    uint8_t len;
    uint32_t (*encode_scalar)(const prism_params_t *params);
    void (*decode_scalar)(prism_params_t *params, uint32_t value);
    uint32_t hash;
} prism_param_desc_t;

typedef struct {
    uint32_t flash_offset;
    uint8_t sector_buf[FLASH_SECTOR_SIZE];
} flash_write_ctx_t;

static uint32_t encode_adc1_gain(const prism_params_t *params) { return params->adc1_gain; }
static uint32_t encode_adc1_offset(const prism_params_t *params) { return params->adc1_offset; }
static uint32_t encode_adc2_gain(const prism_params_t *params) { return params->adc2_gain; }
static uint32_t encode_adc2_offset(const prism_params_t *params) { return params->adc2_offset; }
static uint32_t encode_exposure_ticks(const prism_params_t *params) { return params->exposure_ticks; }

static void decode_adc1_gain(prism_params_t *params, uint32_t value) { params->adc1_gain = (uint16_t)value; }
static void decode_adc1_offset(prism_params_t *params, uint32_t value) { params->adc1_offset = (uint16_t)value; }
static void decode_adc2_gain(prism_params_t *params, uint32_t value) { params->adc2_gain = (uint16_t)value; }
static void decode_adc2_offset(prism_params_t *params, uint32_t value) { params->adc2_offset = (uint16_t)value; }
static void decode_exposure_ticks(prism_params_t *params, uint32_t value) { params->exposure_ticks = (uint16_t)value; }

static prism_param_desc_t g_param_desc[] = {
    {"prism.adc1.gain", PRISM_PARAM_TYPE_U16, 2, encode_adc1_gain, decode_adc1_gain, 0},
    {"prism.adc1.offset", PRISM_PARAM_TYPE_U16, 2, encode_adc1_offset, decode_adc1_offset, 0},
    {"prism.adc2.gain", PRISM_PARAM_TYPE_U16, 2, encode_adc2_gain, decode_adc2_gain, 0},
    {"prism.adc2.offset", PRISM_PARAM_TYPE_U16, 2, encode_adc2_offset, decode_adc2_offset, 0},
    {"prism.exposure_ticks", PRISM_PARAM_TYPE_U16, 2, encode_exposure_ticks, decode_exposure_ticks, 0},
};

static bool param_len_matches_type(uint8_t type, uint8_t len)
{
    switch (type) {
        case PRISM_PARAM_TYPE_U8:
            return len == 1;
        case PRISM_PARAM_TYPE_U16:
            return len == 2;
        case PRISM_PARAM_TYPE_U32:
        case PRISM_PARAM_TYPE_I32:
        case PRISM_PARAM_TYPE_F32:
            return len == 4;
        default:
            return false;
    }
}

static void encode_scalar_le(uint8_t *out, uint8_t len, uint32_t value)
{
    for (uint8_t i = 0; i < len; i++) {
        out[i] = (uint8_t)((value >> (8u * i)) & 0xFFu);
    }
}

static uint32_t decode_scalar_le(const uint8_t *in, uint8_t len)
{
    uint32_t value = 0;
    for (uint8_t i = 0; i < len; i++) {
        value |= ((uint32_t)in[i]) << (8u * i);
    }
    return value;
}

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
        if (g_param_desc[i].hash == key_hash) {
            *type = g_param_desc[i].type;
            *len = g_param_desc[i].len;
            encode_scalar_le(value, *len, g_param_desc[i].encode_scalar(params));
            return true;
        }
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
        if (g_param_desc[i].hash == key_hash) {
            if (g_param_desc[i].type != type || g_param_desc[i].len != len) {
                return false;
            }

            g_param_desc[i].decode_scalar(params, decode_scalar_le(value, len));
            return true;
        }
    }

    return false;
}

static uint32_t slot_crc(const prism_param_slot_t *slot)
{
    return crc32_compute((const uint8_t *)slot, offsetof(prism_param_slot_t, crc32));
}

static bool slot_valid(const prism_param_slot_t *slot)
{
    if (slot->key_hash == 0xFFFFFFFFu) {
        return false;
    }
    if (!param_len_matches_type(slot->type, slot->len)) {
        return false;
    }
    return slot_crc(slot) == slot->crc32;
}

static const prism_param_slot_t *store_slots(const uint8_t *sector)
{
    return (const prism_param_slot_t *)(sector + sizeof(prism_param_store_header_t));
}

static prism_param_slot_t *store_slots_mut(uint8_t *sector)
{
    return (prism_param_slot_t *)(sector + sizeof(prism_param_store_header_t));
}

static bool find_slot_index(const prism_param_slot_t *slots, uint32_t key_hash, uint32_t *index_out)
{
    uint32_t start = key_hash % PRISM_PARAM_TABLE_SLOTS;
    for (uint32_t i = 0; i < PRISM_PARAM_TABLE_SLOTS; i++) {
        uint32_t idx = (start + i) % PRISM_PARAM_TABLE_SLOTS;
        const prism_param_slot_t *slot = &slots[idx];

        if (slot->key_hash == 0xFFFFFFFFu) {
            return false;
        }
        if (slot_valid(slot) && slot->key_hash == key_hash) {
            *index_out = idx;
            return true;
        }
    }
    return false;
}

static bool insert_slot(prism_param_slot_t *slots, prism_param_slot_t *entry)
{
    uint32_t start = entry->key_hash % PRISM_PARAM_TABLE_SLOTS;
    for (uint32_t i = 0; i < PRISM_PARAM_TABLE_SLOTS; i++) {
        uint32_t idx = (start + i) % PRISM_PARAM_TABLE_SLOTS;
        if (slots[idx].key_hash == 0xFFFFFFFFu) {
            slots[idx] = *entry;
            return true;
        }
    }
    return false;
}

static void flash_write_page_cb(void *param)
{
    flash_write_ctx_t *ctx = (flash_write_ctx_t *)param;
    flash_range_erase(ctx->flash_offset, FLASH_SECTOR_SIZE);
    for (uint32_t off = 0; off < FLASH_SECTOR_SIZE; off += FLASH_PAGE_SIZE) {
        flash_range_program(ctx->flash_offset + off, &ctx->sector_buf[off], FLASH_PAGE_SIZE);
    }
}

static uint32_t params_flash_offset(void)
{
    return PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE;
}

void prism_params_set_defaults(prism_params_t *params)
{
    params->adc1_gain = 0;
    params->adc1_offset = 0;
    params->adc2_gain = 0;
    params->adc2_offset = 0;
    params->exposure_ticks = 1404;
}

bool prism_params_load(prism_params_t *params)
{
    ensure_hashes_initialized();

    uint32_t flash_offset = params_flash_offset();
    const uint8_t *sector = (const uint8_t *)(XIP_BASE + flash_offset);
    const prism_param_store_header_t *header = (const prism_param_store_header_t *)sector;

    if (header->magic != PRISM_PARAM_STORE_MAGIC ||
        header->version != PRISM_PARAM_STORE_VERSION ||
        header->slot_count != PRISM_PARAM_TABLE_SLOTS) {
        return false;
    }

    const prism_param_slot_t *slots = store_slots(sector);
    bool any_found = false;

    for (size_t i = 0; i < (sizeof(g_param_desc) / sizeof(g_param_desc[0])); i++) {
        uint32_t idx;
        if (!find_slot_index(slots, g_param_desc[i].hash, &idx)) {
            continue;
        }

        const prism_param_slot_t *slot = &slots[idx];
        if (slot->type != g_param_desc[i].type || slot->len != g_param_desc[i].len) {
            continue;
        }

        g_param_desc[i].decode_scalar(params, slot->value);
        any_found = true;
    }

    return any_found;
}

bool prism_params_save(const prism_params_t *params)
{
    ensure_hashes_initialized();

    static flash_write_ctx_t ctx;
    memset(&ctx, 0xFF, sizeof(ctx));
    ctx.flash_offset = params_flash_offset();

    prism_param_store_header_t *header = (prism_param_store_header_t *)ctx.sector_buf;
    header->magic = PRISM_PARAM_STORE_MAGIC;
    header->version = PRISM_PARAM_STORE_VERSION;
    header->slot_count = PRISM_PARAM_TABLE_SLOTS;
    header->reserved0 = 0xFFFFFFFFu;
    header->reserved1 = 0xFFFFFFFFu;

    prism_param_slot_t *slots = store_slots_mut(ctx.sector_buf);

    for (size_t i = 0; i < (sizeof(g_param_desc) / sizeof(g_param_desc[0])); i++) {
        prism_param_slot_t slot = {
            .key_hash = g_param_desc[i].hash,
            .type = g_param_desc[i].type,
            .len = g_param_desc[i].len,
            .reserved = 0xFFFFu,
            .value = g_param_desc[i].encode_scalar(params),
            .crc32 = 0,
        };
        slot.crc32 = slot_crc(&slot);

        if (!insert_slot(slots, &slot)) {
            return false;
        }
    }

    int rc = flash_safe_execute(flash_write_page_cb, &ctx, 1000);
    return rc == PICO_OK;
}
