# Project PRISM Firmware For CY7C68013A

This folder contains the CY7C68013A user firmware used as the USB FIFO buffer in Project PRISM.

The build depends on several SDK files provided by Cypress / Infineon. Those vendor files are intentionally not tracked in this repository, so a clean checkout is not self-contained until you copy them in from the FX2LP SDK.

To build this firmware, you need to follow these steps:

0. Clone this repository.

1. Download and install `EZUSB FX2LP CY3684 software development kit` from [Infineon](https://itools.infineon.com/archive/CY3684Setup.exe).

2. Open the installation path, find `Firmware/CyStreamer`, and copy the following files to this repository:
```
fw.c
Fx2.h
fx2regs.h
fx2sdly.h
EZUSB.LIB
USBJmpTb.a51
```

3. Run 
```
git apply fw.c.diff
git apply Fx2.h.diff
```

4. Compile the firmware, then run the following command with the `hex2bix` tool in the `bin` folder of the CY3684 software development kit.
```
hex2bix.exe -i -f 0xC2 -o Project_PRISM_CY7C68103A.iic Project_PRISM_CY7C68103A.hex
```

5. Hold the BOOT button of EEPROM, connect the buffer to PC. Flash the `Project_PRISM_CY7C68103A.iic` file with `CyControl.exe` in `Windows Applications/Application Source files/c_sharp/controlcenter/bin/Release` folder of CY3684 software development kit. You may need driver in `Drivers` folder for the pop-up `No EEPROM Device`.
