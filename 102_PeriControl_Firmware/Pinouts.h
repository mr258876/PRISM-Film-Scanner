/*
 * Copyright (c) 2026 mr258876
 * SPDX-License-Identifier: MIT
 */

#ifndef _PINOUTS_H_
#define _PINOUTS_H_

#define LED_PWM1_PIN            7
#define LED_PWM2_PIN            8
#define LED_PWM3_PIN            9
#define LED_PWM4_PIN            10

#define INTERBOARD_UART_RX_PIN  5
#define INTERBOARD_UART_TX_PIN  4
#define EXPOSURE_SYNC_PIN       6

#define M1_DIR_PIN              16
#define M1_STEP_PIN             17
#define M1_EN_PIN               18
#define M1_DIAG_PIN             19

#define M2_DIR_PIN              20
#define M2_STEP_PIN             21
#define M2_EN_PIN               22
#define M2_DIAG_PIN             23

#define M3_DIR_PIN              24
#define M3_STEP_PIN             25
#define M3_EN_PIN               26
#define M3_DIAG_PIN             27

#define TMC_UART_TX_PIN         28
#define TMC_UART_RX_PIN         29

#define LED_CHANNEL_COUNT       4u
#define MOTOR_COUNT             3u

/*
 * The schematic straps MS1/MS2 as:
 * M1 = 0/0, M2 = 1/0, M3 = 0/1.
 * This firmware maps those to the common TMC2209 serial address order 0, 1, 2.
 */
#define TMC_MOTOR1_ADDRESS      0u
#define TMC_MOTOR2_ADDRESS      1u
#define TMC_MOTOR3_ADDRESS      2u

#endif
