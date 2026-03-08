# Project PRISM Film Scanner

*Precise RGB Image Scanning Module*

This project aims to develop an open-sourced high-resolution film scanner. The subsystems may also be used for other proposes.

> [!WARNING]
> Project under active development. Use at your own risk.

## Progress Milestones
- Finished - Linear CCD sensor proof of concept (Mar 7, 2026)
- In progress - Control software
- In progress - Lens system
- In progress - Synchronized RGB light source
- Planned - Auto focus system
- Planned - Film holder & feeding system
- Planned - Sensor curcit optimization

## Getting Started

At the current stage, the repository is best treated as a prototype bring-up package instead of a finished end-user product. To reproduce the current milestone, you need the hardware in `Hardware/`, the RP2040 timing-generator firmware, the CY7C68013A USB FIFO firmware, and the Windows host utility in `Host Software/`.

1. Review the hardware package in `Hardware/` and prepare a board that matches the committed schematic and Gerber files.
2. Build and flash the RP2040 firmware in `Firmware/Project_PRISM_RP2040/`.
   - The documented workflow is Windows + VSCode + the `Raspberry Pi Pico` extension.
   - See `Firmware/Project_PRISM_RP2040/README.md` for the timing-design notes and build entry point.
3. Build and flash the CY7C68013A firmware in `Firmware/Project_PRISM_CY7C68013A/`.
   - This firmware depends on vendor files from the Infineon FX2LP SDK and is not fully self-contained in this repository.
   - See `Firmware/Project_PRISM_CY7C68013A/README.md` for the required SDK files and EEPROM flashing steps.
4. Initialize the host software submodule if needed and build the desktop utility in `Host Software/`.
   - If you cloned without submodules, run `git submodule update --init --recursive`.
   - See `Host Software/README.md` for Windows, .NET, and Visual Studio requirements.
5. Connect the assembled scanner electronics to the host PC over USB and verify that the CY7C68013A FIFO buffer and the RP2040 control interface enumerate.
6. Use the host utility or your own tooling against the RP2040 control interface to configure scan parameters and start bring-up.
   - Command and frame definitions are documented in `Firmware/Project_PRISM_RP2040/CONTROL_INTERFACE.md`.


## Project Structure

- `Hardware/` - PCB design package, schematic export, Gerber archive, and hardware bring-up notes for the current prototype.
- `Firmware/Project_PRISM_RP2040/` - RP2040 firmware that generates CCD/ADC timing, exposes the USB control interface, and stores persistent scan parameters.
- `Firmware/Project_PRISM_CY7C68013A/` - CY7C68013A/FX2LP firmware used as the synchronous USB FIFO buffer for ADC data.
- `Host Software/` - Windows host utility for USB debugging, scan debugging, and early-stage control of supported scanner hardware.
- `Resources/` - timing diagrams and reference images used by the firmware design notes.

## If You're Curious
- [Hardware Requirements and Bring-up](Hardware/README.md)
- [Host Software README](Host%20Software/README.md)
- [Timing Sequence Design of Imaging Subsystem - Readme of RP2040 Firmware](Firmware/Project_PRISM_RP2040/README.md)
- [RP2040 Control Interface](Firmware/Project_PRISM_RP2040/CONTROL_INTERFACE.md)

## Credits
- [jackw01/scanlight](https://github.com/jackw01/scanlight)

## License

See files in `LICENSES/`.

This repository contains hardware design files and firmware/software source code released
under different licenses. Unless a file states otherwise via `SPDX-License-Identifier`,
the following defaults apply:

1) `Hardware/`
   - License: [CERN Open Hardware Licence v2 - Weakly Reciprocal](LICENSES/CERN-OHL-W-2.0.txt)
   - SPDX: `CERN-OHL-W-2.0`

2) `Firmware/`
   - License: [MIT License](LICENSES/MIT.txt)
   - SPDX: `MIT`

3) `Host Software/`
   - This folder is tracked as a git submodule and has its own license file.
   - SPDX: `MIT`

Copyright (c) 2026 mr258876
