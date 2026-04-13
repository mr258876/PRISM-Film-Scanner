/*
 * Copyright (c) 2026 mr258876
 * SPDX-License-Identifier: MIT
 */

#include "pico/stdlib.h"
#include "command_dispatcher.h"
#include "control_protocol.h"
#include "hardware/gpio.h"
#include "Pinouts.h"
#include "uart_task.h"


int main()
{
    if (!command_dispatcher_init()) {
        while (true) {
            tight_loop_contents();
        }
    }

    uart_task_init();

    while (true) {
        control_command_t cmd;
        control_response_t rsp;

        if (uart_task_try_recv(&cmd)) {
            command_dispatcher_process(&cmd, &rsp);
            uart_task_send_blocking(&rsp);
        }

        command_dispatcher_background_step();
    }
}
