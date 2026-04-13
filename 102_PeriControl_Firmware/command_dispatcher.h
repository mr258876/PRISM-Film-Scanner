/*
 * Copyright (c) 2026 mr258876
 * SPDX-License-Identifier: MIT
 */

#ifndef COMMAND_DISPATCHER_H
#define COMMAND_DISPATCHER_H

#include <stdbool.h>

#include "control_protocol.h"

bool command_dispatcher_init(void);
void command_dispatcher_process(const control_command_t *cmd, control_response_t *rsp);
void command_dispatcher_background_step(void);

#endif
