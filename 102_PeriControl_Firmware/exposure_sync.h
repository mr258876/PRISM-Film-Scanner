/*
 * Copyright (c) 2026 mr258876
 * SPDX-License-Identifier: MIT
 */

#ifndef EXPOSURE_SYNC_H
#define EXPOSURE_SYNC_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t mode;
    uint8_t active;
    uint8_t led_mask;
    uint32_t pulse_us;
} exposure_sync_status_t;

void exposure_sync_init(void);
bool exposure_sync_configure(uint8_t mode, uint8_t led_mask, uint32_t pulse_us);
void exposure_sync_get_status(exposure_sync_status_t *status_out);
void exposure_sync_refresh_outputs(void);

#endif
