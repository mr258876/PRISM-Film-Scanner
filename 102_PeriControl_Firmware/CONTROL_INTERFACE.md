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

### `0x22` Get Status (Legacy / Service)

Response payload:

```text
+----------------------+------------------------------------------+
| sys_clock_khz (u32) | led1..led4 levels (4 x u16)              |
+----------------------+------------------------------------------+
| for each motor: motor_id(u8), enabled(u8), running(u8),       |
| direction(u8), diag(u8), reserved(u8), interval_us(u16),      |
| remaining_steps(u32)                                           |
+---------------------------------------------------------------+
| steady_mask(u8) | sync_mask(u8) | sync_active(u8)       |
+---------------------------------------------------------------+
| reserved(u8) | legacy_sync_pulse_us(u32)                      |
+---------------------------------------------------------------+
```

The shared legacy status response keeps a single-pulse slot only to preserve its fixed payload size. New integrations should prefer the dedicated illumination and motion domain getters below.

## Illumination Domain

### `0x40` Get Illumination Status

Response payload:

```text
+--------+--------+--------+--------+
| led1   | led2   | led3   | led4   |
| (u16)  | (u16)  | (u16)  | (u16)  |
+--------+--------+--------+--------+
| steady_mask (u8) | sync_mask (u8)   | sync_active(u8)  | reserved(u8) |
+------------------+------------------+------------------+--------------+
| led1_pulse_clk (u32)                                                 |
+-----------------------------------------------------------------------+
| led2_pulse_clk (u32)                                                 |
+-----------------------------------------------------------------------+
| led3_pulse_clk (u32)                                                 |
+-----------------------------------------------------------------------+
| led4_pulse_clk (u32)                                                 |
+-----------------------------------------------------------------------+
```

### `0x41` Set LED Levels

Request payload: `led1(u16), led2(u16), led3(u16), led4(u16)`

These are the configured brightness levels used when a channel is steady-on, and also the brightness basis used during sync pulses.

### `0x42` Set Steady Illumination

Request payload:

```text
+------------------+--------------+
| steady_mask (u8) | reserved(u24)|
+------------------+--------------+
```

- `steady_mask` uses bit0..bit3 for LED1..LED4.
- Channels in `steady_mask` are held continuously on.
- Channels not in `steady_mask` remain off unless separately armed for sync mode.

Response payload echoes the same 4-byte payload.

### `0x43` Configure LED Exposure Sync

Request payload:

```text
+----------------+--------------+
| sync_mask(u8)  | reserved(u24)|
+----------------+--------------+
```

- `sync_mask` uses bit0..bit3 for LED1..LED4.
- `sync_mask = 0` disables sync participation for all channels.
- Sync-armed channels pulse using their configured `ledN` brightness level from `0x41`.

Channels cannot currently be both steady-on and sync-armed at the same time; overlapping bits are rejected as invalid.

Response payload echoes the same 4-byte payload.

### `0x44` Set Sync Pulse Clocks

Request payload:

```text
+------------------------+------------------------+
| led1_pulse_clk (u32)   | led2_pulse_clk (u32)   |
+------------------------+------------------------+
| led3_pulse_clk (u32)   | led4_pulse_clk (u32)   |
+------------------------+------------------------+
```

`pulse_clk` is expressed directly in board-102 PIO clock cycles. Sync-enabled channels require a non-zero pulse clock value.

Response payload echoes the same 16-byte payload.

## Motion Domain

### `0x50` Get Motion State

Response payload:

```text
+---------------------------------------------------------------+
| for each motor: motor_id(u8), enabled(u8), running(u8),       |
| direction(u8), diag(u8), reserved(u8), interval_us(u16),      |
| remaining_steps(u32)                                           |
+---------------------------------------------------------------+
```

### `0x51` Set Motor Enable

Request payload: `motor_id(u8), enabled(u8)`

### `0x52` Move Motor Steps

Request payload: `motor_id(u8), direction(u8), steps(u32), interval_us(u32)`

### `0x53` Stop Motor

Request payload: `motor_id(u8)`

### `0x54` Apply Motor Config

Request payload: `motor_id(u8)`

This applies the persisted motor parameters to the selected TMC2209.

### `0x55` Read TMC Register

Request payload: `motor_id(u8), reg_addr(u8)`

Response payload: `motor_id(u8), reg_addr(u8), reg_value(u32)`

### `0x56` Write TMC Register

Request payload: `motor_id(u8), reg_addr(u8), reg_value(u32)`

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
