/*
 * Copyright (c) 2026 mr258876
 * SPDX-License-Identifier: MIT
 */

#ifndef STEPPER_TASK_H
#define STEPPER_TASK_H

#include <stdbool.h>
#include <stdint.h>

#include "Pinouts.h"

typedef struct {
    bool enabled;
    bool running;
    bool direction;
    uint32_t remaining_steps;
    uint32_t configured_interval_us;
    uint32_t diag_state;
} stepper_status_t;

typedef struct {
    uint8_t motor_index;
    stepper_status_t status;
} stepper_motion_event_t;

void stepper_task_init(void);
bool stepper_task_set_enable(uint8_t motor_index, bool enabled);
bool stepper_task_start_move(uint8_t motor_index, bool direction, uint32_t steps, uint32_t interval_us);
bool stepper_task_prepare_move_on_sync(uint8_t motor_index, bool direction, uint32_t steps, uint32_t interval_us);
bool stepper_task_stop(uint8_t motor_index);
void stepper_task_get_status(uint8_t motor_index, stepper_status_t *status_out);
bool stepper_task_try_pop_motion_complete_event(stepper_motion_event_t *event_out);

#endif
