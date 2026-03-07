#ifndef _PERSISTENT_PARAMS_H_
#define _PERSISTENT_PARAMS_H_

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint16_t adc1_gain;
    uint16_t adc1_offset;
    uint16_t adc2_gain;
    uint16_t adc2_offset;
    uint16_t exposure_ticks;
} prism_params_t;

void prism_params_set_defaults(prism_params_t *params);
bool prism_params_load(prism_params_t *params);
bool prism_params_save(const prism_params_t *params);
uint32_t prism_param_hash_key(const char *key);
bool prism_param_meta_by_hash(uint32_t key_hash, uint8_t *type, uint8_t *len);
bool prism_param_get_by_hash(const prism_params_t *params, uint32_t key_hash, uint8_t *type, uint8_t *len, uint8_t *value);
bool prism_param_set_by_hash(prism_params_t *params, uint32_t key_hash, uint8_t type, uint8_t len, const uint8_t *value);

#endif
