# Project PRISM Firmwares

This folder contains code to build firmware for RP2040 and CY7C68013A in project. Compiled binaries are also provided if you are looking for them.

## Flashing Firmware for RP2040

1. Get `Project_PRISM_RP2040.uf2`.
2. Connect the board (RP2040) to your computer, hold `BOOTSEL` button, and press `RESET`.
3. You will find a small USB drive called `RPI-RP2` pop up, drag the file in.
4. Done! RP2040 will restart automatically.

## Flashing Firmware for CY7C68013A

1. Get `Project_PRISM_CY7C68013A.iic` and `CyUSB Driver And Control Center.zip`.
2. Connect the board (CY7C68013A) to your computer, hold `BOOTSEL` button, and press `RESET`.
3. (If you haven't installed CyUsb driver) A unknown device will pop up in device manager. Right click on it, install driver from the zip file.
4. Open `CyControl.exe`, you should find a `FX2LP No EEPROM Device`. Click `Program` button on the menu bar, choose `FX2` -> `64K`. Select the iic file.
5. Wait for a while, manually reset after flash completed.
