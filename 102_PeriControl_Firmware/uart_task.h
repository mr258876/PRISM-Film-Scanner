/*
 * Copyright (c) 2026 mr258876
 * SPDX-License-Identifier: MIT
 */

#ifndef UART_TASK_H
#define UART_TASK_H

#include <stdbool.h>

#include "control_protocol.h"

void uart_task_init(void);
bool uart_task_try_recv(control_command_t *cmd);
void uart_task_send_blocking(const control_response_t *rsp);

#endif
