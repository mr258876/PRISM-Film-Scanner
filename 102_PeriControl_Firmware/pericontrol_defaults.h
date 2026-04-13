/*
 * Copyright (c) 2026 mr258876
 * SPDX-License-Identifier: MIT
 */

#ifndef _PERICONTROL_DEFAULTS_H_
#define _PERICONTROL_DEFAULTS_H_

#define PRISM_DEFAULT_SYS_CLOCK_KHZ           125000u
#define PRISM_DEFAULT_LED_PWM_WRAP            1000u
#define PRISM_DEFAULT_LED_LEVEL               0u
#define PRISM_DEFAULT_TMC_IRUN                20u
#define PRISM_DEFAULT_TMC_IHOLD               8u
#define PRISM_DEFAULT_TMC_MICROSTEPS          16u
#define PRISM_DEFAULT_TMC_STEALTHCHOP_ENABLE  1u
#define PRISM_DEFAULT_STEP_INTERVAL_US        500u

#define PRISM_MIN_SYS_CLOCK_KHZ               30000u
#define PRISM_MAX_SYS_CLOCK_KHZ               200000u
#define PRISM_PARAM_SAVE_DEBOUNCE_MS          1000u

#endif
