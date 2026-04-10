# Project PRISM RP2040 Control Interface

This document describes the current USB vendor interface exposed by the RP2040 firmware.

## USB Device Identity

- Vendor ID: `0x1D50`
- Product ID: `0x619D`
- Product string: `Project PRISM Control Interface`
- Transport: TinyUSB vendor interface with one OUT endpoint and one IN endpoint

## Frame Format

Host-to-device command frame:

```text
+--------+--------+----------------+-------------------+
| byte 0 | byte 1 | bytes 2..3     | bytes 4..N        |
+--------+--------+----------------+-------------------+
| 0xA5   | opcode | payload_len_le | payload bytes     |
+--------+--------+----------------+-------------------+
```

Device-to-host response frame:

```text
+--------+--------+--------+----------------+-------------------+
| byte 0 | byte 1 | byte 2 | bytes 3..4     | bytes 5..N        |
+--------+--------+--------+----------------+-------------------+
| 0x5A   | opcode | status | payload_len_le | payload bytes     |
+--------+--------+--------+----------------+-------------------+
```

- Multi-byte integers are little-endian.
- The firmware currently accepts payloads up to `32` bytes for parameter writes.
- Invalid markers, unsupported opcodes, or unexpected payload sizes return an error status.

## Commands

### `0x20` - Get Parameter By Hash

Request payload:

```text
+----------------+
| key_hash (u32) |
+----------------+
```

Successful response payload:

```text
+----------------+------------+-----------+----------------+
| key_hash (u32) | param_type | param_len | param_data...  |
+----------------+------------+-----------+----------------+
```

### `0x21` - Set Parameter By Hash

Request payload:

```text
+----------------+------------+-----------+----------------+
| key_hash (u32) | param_type | param_len | param_data...  |
+----------------+------------+-----------+----------------+
```

Successful response payload:

```text
+----------------+------------+-----------+----------------+
| key_hash (u32) | param_type | param_len | param_data...  |
+----------------+------------+-----------+----------------+
```

The response echoes back the stored value after validation.

### `0x30` - Start Scan

- Request payload length: `0`
- Response payload:

```text
+----------------------+-------------------------+
| target_scan_lines(u32) | completed_scan_lines(u32) |
+----------------------+-------------------------+
```

### `0x31` - Set Scan Lines

Request payload:

```text
+-------------------+
| scan_lines (u32)  |
+-------------------+
```

Response payload:

```text
+----------------------+-------------------------+
| target_scan_lines(u32) | completed_scan_lines(u32) |
+----------------------+-------------------------+
```

### `0x32` - Stop Scan

- Request payload length: `0`
- Response payload:

```text
+----------------------+-------------------------+
| target_scan_lines(u32) | completed_scan_lines(u32) |
+----------------------+-------------------------+
```

### `0x33` - Start Warmup

- Request payload length: `0`
- Response payload:

```text
+----------------------+-------------------------+
| target_scan_lines(u32) | completed_scan_lines(u32) |
+----------------------+-------------------------+
```

## Status Codes

- `0x00` - `OK`
- `0xE1` - `QUEUE_FULL`
- `0xE2` - `BAD_FRAME`
- `0xE3` - `FLASH_FAIL`
- `0xE4` - `PARAM_NOT_FOUND`
- `0xE5` - `SCAN_LINES_INVALID`
- `0xE6` - `BUSY`
- `0xE7` - `PARAM_TYPE_MISMATCH`
- `0xE8` - `PARAM_LEN_INVALID`
- `0xE9` - `PAYLOAD_INVALID`

## Parameter Types

- `0x01` - `U8`
- `0x02` - `U16`
- `0x03` - `U32`
- `0x04` - `I32`
- `0x05` - `F32`
- `0x80` and above - byte-array types reserved by the protocol

## Persisted Parameters

The current firmware exposes these persisted parameters through the hash-based parameter API:

- `prism.adc1.gain` - `U16`
- `prism.adc1.offset` - `U16`
- `prism.adc2.gain` - `U16`
- `prism.adc2.offset` - `U16`
- `prism.exposure_ticks` - `U16`
- `prism.sys_clock_khz` - `U32`

Default values currently compiled into firmware:

- `prism.adc1.gain = 0`
- `prism.adc1.offset = 0`
- `prism.adc2.gain = 0`
- `prism.adc2.offset = 0`
- `prism.exposure_ticks = 1404`
- `prism.sys_clock_khz = 125000`

`prism.sys_clock_khz` controls the RP2040 system clock in kHz. The default is `125000`, the accepted range is `30000` to `200000`, and if a host stores a valid value in that range, the firmware reapplies it during boot before initializing the timing generators.

The firmware hashes parameter keys internally with 32-bit FNV-1a. A host application must calculate the same hash for the string key before calling the get/set commands.

## Notes For Host Software

- Parameter values are encoded little-endian.
- `Set Parameter By Hash` must send a `param_type` and `param_len` that exactly match the firmware metadata.
- Scan-control responses always return the configured target line count together with the completed line count, which is useful for host-side progress tracking.
- This protocol description reflects the current in-repo firmware and may change while the project is under active development.
