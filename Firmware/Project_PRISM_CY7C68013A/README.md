# Project PRISM Firmware For CY7C68013A

This folder contains user code for CY7C68013A which acts as a USB FIFO buffer in this project. However, you will find these files imcomplete to build.

Almost all firmwares of CY7C68013A have to include serval lib files provided by Cypress(now Infineon). Due to copyright reason, these files are excluded from this repository.

To build this firmware, you need to follow these steps:

0. Clone this repository.

1. Download and install `EZUSB FX2LP CY3684 software development kit` from [Infineon](https://itools.infineon.com/archive/CY3684Setup.exe).

2. Open the installizaton path, find `Firmware/CyStreamer`, copy following files to this repository:
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

4. Compile the firmware, and run following command with `hex2bix` tool in `bin` folder of CY3684 software development kit.
```
hex2bix.exe -i -f 0xC2 -o Project_PRISM_CY7C68103A.iic Project_PRISM_CY7C68013A.hex
```

5. Hold the BOOT button of EEPROM, connect the buffer to PC. Flash the `Project_PRISM_CY7C68013A.iic` file with `CyControl.exe` in `Windows Applications/Application Source files/c_sharp/controlcenter/bin/Release` folder of CY3684 software development kit. You may need driver in `Drivers` folder for the pop-up `No EEPROM Device`, or install WinUSB driver with Zadig.
