# Project PRISM Peripheral Control Interface

This document describes the logical command protocol used by the Peripheral Control Board. The transport is the board-to-board UART link from the Scanner Main Control Board to the Peripheral Control Board.

This protocol follows the existing Project PRISM RP2040 vendor-interface style, but changes the frame markers to:

- Host to device marker: `0xA6`
- Device to host marker: `0x6A`

## Frame Format

Request frame:

```text
+--------+--------+----------------+-------------------+-------------+
| byte 0 | byte 1 | bytes 2..3     | bytes 4..N        | bytes N+1.. |
+--------+--------+----------------+-------------------+-------------+
| 0xA6   | opcode | payload_len_le | payload bytes     | crc16_le     |
+--------+--------+----------------+-------------------+-------------+
```

Response frame:

```text
+--------+--------+--------+----------------+-------------------+-------------+
| byte 0 | byte 1 | byte 2 | bytes 3..4     | bytes 5..N        | bytes N+1.. |
+--------+--------+--------+----------------+-------------------+-------------+
| 0x6A   | opcode | status | payload_len_le | payload bytes     | crc16_le     |
+--------+--------+--------+----------------+-------------------+-------------+
```

The CRC is calculated over the marker, opcode, status/length fields, and payload bytes using CRC-16/CCITT-FALSE. COBS is not used on this link.

## Production transport

- Board-to-board transport: UART1 on the Peripheral Control Board
- `RPI_UART1_TX` -> GPIO4
- `RPI_UART1_RX` -> GPIO5
- `EXPOSURE_SYNC` -> GPIO6 (separate sync signal, not part of the framed command stream)
- Current firmware uses only the falling edge of `EXPOSURE_SYNC` for LED exposure-sync triggering. The rising edge is ignored.

## Commands

### `0x20` Get Parameter By Hash

Request payload: `key_hash(u32)`

Successful response payload:

```text
+----------------+------------+-----------+----------------+
| key_hash (u32) | param_type | param_len | param_data...  |
+----------------+------------+-----------+----------------+
```

### `0x21` Set Parameter By Hash

Request payload:

```text
+----------------+------------+-----------+----------------+
| key_hash (u32) | param_type | param_len | param_data...  |
+----------------+------------+-----------+----------------+
```

Response payload echoes the stored value using the same layout.

### `0x40` Get Status

Response payload:

```text
+----------------------+------------------------------------------+
| sys_clock_khz (u32) | led1..led4 levels (4 x u16)              |
+----------------------+------------------------------------------+
| for each motor: motor_id(u8), enabled(u8), running(u8),       |
| direction(u8), diag(u8), reserved(u8), interval_us(u16),      |
| remaining_steps(u32)                                           |
+---------------------------------------------------------------+
| led_sync_mode(u8) | led_sync_active(u8) | led_sync_mask(u8)   |
+---------------------------------------------------------------+
| reserved(u8) | led_sync_pulse_us(u32)                         |
+---------------------------------------------------------------+
```

### `0x41` Set LED Levels

Request payload: `led1(u16), led2(u16), led3(u16), led4(u16)`

### `0x42` Set Motor Enable

Request payload: `motor_id(u8), enabled(u8)`

### `0x43` Move Motor Steps

Request payload: `motor_id(u8), direction(u8), steps(u32), interval_us(u32)`

### `0x44` Stop Motor

Request payload: `motor_id(u8)`

### `0x45` Read TMC Register

Request payload: `motor_id(u8), reg_addr(u8)`

Response payload: `motor_id(u8), reg_addr(u8), reg_value(u32)`

### `0x46` Write TMC Register

Request payload: `motor_id(u8), reg_addr(u8), reg_value(u32)`

### `0x47` Apply Motor Config

Request payload: `motor_id(u8)`

This applies the persisted motor parameters to the selected TMC2209.

### `0x48` Configure LED Exposure Sync

Request payload:

```text
+----------------+--------------+----------------+-------------------+
| sync_mode (u8) | led_mask(u8) | reserved (u16) | pulse_us (u32)    |
+----------------+--------------+----------------+-------------------+
```

- `sync_mode = 0`: disable exposure-sync gating and restore steady board-102 LED output.
- `sync_mode = 1`: on each falling edge of `EXPOSURE_SYNC`, board 102 uses PIO to drive the selected LEDs on locally for `pulse_us` microseconds and then drives them back off.
- `led_mask` uses bit0..bit3 for LED1..LED4.
- `pulse_us` should already include any timing compensation agreed by board 100 for PT4115 DIM response, optical settling, or other downstream delay.

In the current PIO-oriented implementation, LED level values are treated as binary enable flags for the LED channels: `0` means off and any non-zero value means on.

Response payload echoes the same 8-byte payload.

## Status Codes

- `0x00` `OK`
- `0xE1` `QUEUE_FULL`
- `0xE2` `BAD_FRAME`
- `0xE3` `FLASH_FAIL`
- `0xE4` `PARAM_NOT_FOUND`
- `0xE7` `PARAM_TYPE_MISMATCH`
- `0xE8` `PARAM_LEN_INVALID`
- `0xE9` `PAYLOAD_INVALID`
- `0xEA` `BUSY`
- `0xEB` `RANGE_INVALID`
- `0xEC` `HW_ERROR`

## Persisted Parameters

- `prism.sys_clock_khz`
- `prism.led_pwm_wrap`
- `prism.led1.level`
- `prism.led2.level`
- `prism.led3.level`
- `prism.led4.level`
- `prism.motor{1,2,3}.irun`
- `prism.motor{1,2,3}.ihold`
- `prism.motor{1,2,3}.microsteps`
- `prism.motor{1,2,3}.stealthchop_enable`
- `prism.motor{1,2,3}.step_interval_us`
