# Project PRISM Film Scanner

*Precise RGB Image Scanning Module*

This project aims to develop an open-source high-resolution film scanner. The subsystems may also be used for other purposes.

> [!WARNING]
> Project under active development. Use at your own risk.

## Progress Milestones
- Finished - Linear CCD sensor proof of concept (Mar 7, 2026)
  ![Imaging Board PoC](/Resources/Imaging_Board_PoC.jpg)

  *Forgive the poor soldering
- Finished - E-mount apapter (Mar 14, 2026)
- In progress - Control software
- In progress - Synchronized RGB light source
- Planned - Auto focus system
- Planned - Film holder & feeding system
- Planned - Sensor circuit optimization

## Getting Started

At the current stage, the repository is best treated as a prototype bring-up package instead of a finished end-user product. It documents the current electrical prototype and the firmware/software needed to exercise it, but it is not yet a full scanner replication guide with finalized optics, mechanics, autofocus, or film transport.

To reproduce the current milestone, you need the scanner hardware package in `100_Scanner_Hardware/`, the RP2040 timing-generator firmware and CY7C68013A USB FIFO firmware in `100_Scanner_Firmware/`, and the Windows host utility in `Host Software/`.

1. Review the hardware package in `100_Scanner_Hardware/` and prepare a board that matches the committed schematic and Gerber files.
2. Flash or build firmware for both RP2040 and CY7C68013A.
   - Flash precompiled firmware
      - See [Firmware Readme](/100_Scanner_Firmware/README.md)
   - or build the firmware yourself
      *  Build and flash the RP2040 firmware in `100_Scanner_Firmware/Project_PRISM_RP2040/`.
         - The documented workflow is Windows + VSCode + the `Raspberry Pi Pico` extension.
         - See `100_Scanner_Firmware/Project_PRISM_RP2040/README.md` for the timing-design notes and build entry point.
      * Build and flash the CY7C68013A firmware in `100_Scanner_Firmware/Project_PRISM_CY7C68013A/`.
         - This firmware depends on vendor files from the Infineon FX2LP SDK and is not fully self-contained in this repository.
         - See `100_Scanner_Firmware/Project_PRISM_CY7C68013A/README.md` for the required SDK files and EEPROM flashing steps.
3. Initialize the host software submodule if needed and build the desktop utility in `Host Software/`.
   - If you cloned without submodules, run `git submodule update --init --recursive`.
   - See `Host Software/README.md` for Windows, .NET, and Visual Studio requirements.
4. Connect the assembled scanner electronics to the host PC over USB and verify that both USB functions enumerate.
   - Project PRISM Control Interface (RP2040): `VID 0x1D50`, `PID 0x619D`
   - Project PRISM FIFO Buffer (CY7C68013A): `VID 0x1D50`, `PID 0x619C`
5. Use the host utility or your own tooling against the RP2040 control interface to configure scan parameters and start bring-up.
   - Command and frame definitions are documented in `100_Scanner_Firmware/Project_PRISM_RP2040/CONTROL_INTERFACE.md`.


## Project Structure

- `100_Scanner_Hardware/` - scanner mainboard PCB design package, schematic export, Gerber archive, and hardware bring-up notes for the current prototype.
- `100_Scanner_Firmware/` - prebuilt firmware images plus source trees for the scanner electronics.
  - `Project_PRISM_RP2040/` - RP2040 firmware that generates CCD/ADC timing, exposes the USB control interface, and stores persistent scan parameters.
  - `Project_PRISM_CY7C68013A/` - CY7C68013A/FX2LP firmware used as the synchronous USB FIFO buffer for ADC data.
- `101_BackLight_Hardware/` - backlight hardware work area for the illumination subsystem.
- `102_PeriControl_Hardware/` - hardware work area for peripheral-control electronics.
- `102_PeriControl_Firmware/` - firmware work area for the peripheral-control subsystem.
- `Host Software/` - Windows host utility for USB debugging, scan debugging, and early-stage control of supported scanner hardware.
- `Documents/` - datasheets and reference PDFs for the main electrical components.
- `Lens Adapter/` - mechanical adapter assets for the current optical setup.
- `Resources/` - timing diagrams and reference images used by the firmware design notes.

## If You're Curious
- [Hardware Requirements and Bring-up](100_Scanner_Hardware/README.md)
- [Firmware Overview](100_Scanner_Firmware/README.md)
- [Host Software README](Host%20Software/README.md)
- [Timing Sequence Design of Imaging Subsystem - Readme of RP2040 Firmware](100_Scanner_Firmware/Project_PRISM_RP2040/README.md)
- [RP2040 Control Interface](100_Scanner_Firmware/Project_PRISM_RP2040/CONTROL_INTERFACE.md)

## Credits
- [jackw01/scanlight](https://github.com/jackw01/scanlight)

## License

See files in `LICENSES/`.

This repository contains hardware design files and firmware/software source code released
under different licenses. Unless a file states otherwise via `SPDX-License-Identifier`,
the following defaults apply:

1) `100_Scanner_Hardware/`, `101_BackLight_Hardware/`, `102_PeriControl_Hardware/`, and `Lens Adapter/`
   - License: [CERN Open Hardware Licence v2 - Weakly Reciprocal](LICENSES/CERN-OHL-W-2.0.txt)
   - SPDX: `CERN-OHL-W-2.0`

2) `100_Scanner_Firmware/` and `102_PeriControl_Firmware/`
   - License: [MIT License](LICENSES/MIT.txt)
   - SPDX: `MIT`

3) `Host Software/`
   - This folder is tracked as a git submodule and has its own license file.
   - SPDX: `MIT`

Copyright (c) 2026 mr258876
