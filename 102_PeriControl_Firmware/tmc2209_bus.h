/*
 * Copyright (c) 2026 mr258876
 * SPDX-License-Identifier: MIT
 */

#ifndef TMC2209_BUS_H
#define TMC2209_BUS_H

#include <stdbool.h>
#include <stdint.h>

void tmc2209_bus_init(void);
bool tmc2209_write_register(uint8_t motor_index, uint8_t reg_addr, uint32_t value);
bool tmc2209_read_register(uint8_t motor_index, uint8_t reg_addr, uint32_t *value_out);
bool tmc2209_apply_basic_config(uint8_t motor_index, uint8_t irun, uint8_t ihold, uint16_t microsteps, bool stealthchop_enable);

#endif
