/*
 * Copyright (c) 2026 mr258876
 * SPDX-License-Identifier: MIT
 */

#ifndef EXPOSURE_SYNC_H
#define EXPOSURE_SYNC_H

#include <stdbool.h>
#include <stdint.h>

#include "Pinouts.h"

typedef struct {
    uint8_t steady_mask;
    uint8_t sync_mask;
    uint8_t active;
    uint32_t pulse_clk[LED_CHANNEL_COUNT];
} exposure_sync_status_t;

void exposure_sync_init(void);
bool exposure_sync_set_steady_mask(uint8_t steady_mask);
bool exposure_sync_set_sync_mask(uint8_t sync_mask);
bool exposure_sync_set_pulse_clk(const uint32_t *pulse_clk);
void exposure_sync_get_status(exposure_sync_status_t *status_out);
void exposure_sync_refresh_outputs(void);

#endif
