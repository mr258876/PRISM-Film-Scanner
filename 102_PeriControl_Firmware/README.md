# Project PRISM Peripheral Control Firmware

This folder contains the RP2040 firmware for `PRISM102 Peripheral Control`.

## Hardware mapping from the committed schematic

- LED PWM outputs
  - `LED_PWM1` -> GPIO7
  - `LED_PWM2` -> GPIO8
  - `LED_PWM3` -> GPIO9
  - `LED_PWM4` -> GPIO10
- TMC2209 control
  - Motor 1: `DIR/STEP/EN/DIAG` -> GPIO16/17/18/19
  - Motor 2: `DIR/STEP/EN/DIAG` -> GPIO20/21/22/23
  - Motor 3: `DIR/STEP/EN/DIAG` -> GPIO24/25/26/27
  - Shared UART bus: `UART0 TX/RX` -> GPIO28/29
- Board-to-board link to the Scanner Main Control Board
  - `UART1 TX` -> GPIO4
  - `UART1 RX` -> GPIO5
  - `EXPOSURE_SYNC` -> GPIO6

## Notes

- The default production build is a UART-controlled subordinate device for the Scanner Main Control Board.
- The framed UART protocol still uses the Project PRISM control-interface style with `A6/6A` markers and now appends a CRC-16 integrity field to each request/response frame.
- LED outputs are currently driven as binary on/off channels. Any non-zero configured level enables the corresponding PT4115 channel, and `0` disables it.
- `EXPOSURE_SYNC` falling edges are handled by RP2040 PIO so selected LED channels can be driven on for a fixed locally timed pulse width configured over the UART protocol.
- Motor stepping is implemented with a lightweight repeating-timer pulse generator, while TMC2209 driver configuration happens over the shared UART bus.
- The current implementation targets bring-up and host-side integration first: finite step moves, enable control, LED brightness control, and raw TMC register access are supported.
