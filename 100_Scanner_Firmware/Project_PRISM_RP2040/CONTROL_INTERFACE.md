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

## Transport Topology

The Scanner Main Control Board is the only PC-facing control endpoint.

- **PC -> Scanner Main Control Board**: USB vendor interface, frame markers `A5/5A`
- **Scanner Main Control Board -> Peripheral Control Board**: UART0 bridge on GPIO28/GPIO29, frame markers `A6/6A`, CRC-16 on each internal frame

The host should talk only to the Scanner Main Control Board. Peripheral-board details are intentionally kept off the stable USB API surface, except for the dedicated debug passthrough command documented below.

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

## Debug / Service Commands

The stable PC-facing API stops at the Scanner Main Control Board boundary. Raw subordinate-board access is available only through the debug passthrough command below.

### `0xF0` - Debug Passthrough To Board-102

Request payload:

```text
+-------------+----------------------+------------------------+
| target (u8) | subordinate_opcode   | subordinate_payload... |
|             | (u8)                 |                        |
+-------------+----------------------+------------------------+
```

- `target = 0x01` selects the Peripheral Control Board.
- The subordinate payload length is inferred from the outer USB frame length minus 2 bytes.
- This command is for bring-up, service, and low-level diagnostics. It is not part of the stable production host API.
- The current implementation limits subordinate payloads carried through this command to `56` bytes so the USB response still fits in one control frame.

Successful response payload:

```text
+-------------+----------------------+---------------------+------------------------+
| target (u8) | subordinate_opcode   | subordinate_status  | subordinate_payload... |
|             | (u8)                 | (u8)                |                        |
+-------------+----------------------+---------------------+------------------------+
```

The Scanner Main Control Board terminates the internal UART link layer itself. The host does not see the subordinate CRC directly.

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
- `0xEA` - `DEBUG_TARGET_UNSUPPORTED`
- `0xEB` - `SUBORDINATE_TIMEOUT`
- `0xEC` - `SUBORDINATE_LINK_ERROR`

For `0xF0`, the USB status byte remains owned by the Scanner Main Control Board. On success it returns `OK` and carries the subordinate status inside the response payload. Internal UART CRC and framing errors are absorbed by board 100 and surfaced only as coarse board-100-level link failures.

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
- Low-level Peripheral Control Board access is intentionally isolated behind the debug passthrough command. Normal host workflows should not depend on subordinate opcodes, payloads, or link-layer details.
- This protocol description reflects the current in-repo firmware and may change while the project is under active development.
