# Project PRISM Firmwares

This folder contains firmware for the RP2040 timing generator and the CY7C68013A USB FIFO buffer used in Project PRISM. Prebuilt images for the current prototype are also provided in this folder.

Prebuilt firmware files:

- `Project_PRISM_RP2040.uf2` - RP2040 firmware image
- `Project_PRISM_CY7C68103A.iic` - CY7C68013A EEPROM image

## Flashing Firmware for RP2040

1. Get `Project_PRISM_RP2040.uf2`.
2. Connect the board (RP2040) to your computer, hold `BOOTSEL` button, and press `RESET`.
3. You will find a small USB drive called `RPI-RP2` pop up, drag the file in.
4. Done! RP2040 will restart automatically.

## Flashing Firmware for CY7C68013A

1. Get `Project_PRISM_CY7C68103A.iic` and `CyUSB Driver And Control Center.zip`.
2. Connect the board (CY7C68013A) to your computer, hold `BOOTSEL` button, and press `RESET`.
3. (If you haven't installed CyUsb driver) A unknown device will pop up in device manager. Right click on it, install driver from the zip file.
4. Open `CyControl.exe`, you should find a `FX2LP No EEPROM Device`. Click `Program` button on the menu bar, choose `FX2` -> `64K`. Select the iic file.
5. Wait for a while, manually reset after flash completed.
