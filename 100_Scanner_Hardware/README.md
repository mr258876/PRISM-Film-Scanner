# Project PRISM Hardware

This folder contains hardware design files of Project PRISM.

## Hardware Requirements

The committed hardware files describe the current RP2040-based prototype revision. Before building, review these files first:

- `SCH_PRISM RP2040_2026-03-07.pdf` - exported schematic of the current prototype.
- `Gerber_PRISM_RP2040_260307_2026-03-07.zip` - PCB manufacturing package.
- `EadyEDA/` - source design files.

Core devices referenced by the repository and firmware documentation:

- `RP2040` - timing generator and USB control interface.
- `CY7C68013A` - synchronous USB FIFO buffer.
- `2x AD9826` - analog front-end / ADC path.
- `TCD1708D` - linear CCD sensor.
- `4x UCC27524` - MOS driver for CCD clocks.

This is still a development-stage design. Optical parts, illumination, autofocus, and film transport are not yet packaged here as a complete build recipe.

## Bring-up Guide

Use this as a minimal prototype bring-up checklist.

1. Inspect the schematic PDF and confirm your assembled board matches the committed revision.
2. Program the RP2040 using the workflow documented in `../100_Scanner_Firmware/Project_PRISM_RP2040/README.md`.
3. Program the CY7C68013A EEPROM using the workflow documented in `../100_Scanner_Firmware/Project_PRISM_CY7C68013A/README.md`.
4. Connect the board to the host PC and verify that the RP2040 USB control interface enumerates as `VID 0x1D50` and `PID 0x619D`.
5. Use the command protocol documented in `../100_Scanner_Firmware/Project_PRISM_RP2040/CONTROL_INTERFACE.md` to:
   - read back persisted parameters,
   - set scan line count,
   - start warmup or scan commands,
   - stop acquisition if needed.
6. Bring up the analog/optical path only after digital control and USB communication are confirmed working.

Known limitations for this stage:

- The repository currently focuses on electrical bring-up rather than a polished end-user scanning workflow.
- The mechanical film path and autofocus subsystems are still planned work.
- The repository documents the electrical prototype better than the final scanner assembly.
