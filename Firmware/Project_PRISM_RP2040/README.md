# Project PRISM RP2040 Firmware

This folder contains all code to build firmware for RP2040 timing generator. 

Instead of just boring building guide, I decide to make this readme a small record of how this firmware is designed, as well as some experience sharing in RP2040's PIO programming.

## Replication Guide

1. Install VSCode
2. Install `Raspberry Pi Pico` extension
3. Click the `Pi Pico` icon, then `Run Project (USB)`, and you're good to go!

## Introduction

Existing project of high-performance open-sourced CCD cameras usually uses FPGA to generate clock signals for CCD, as well as A/D converters ([CameraNexus/Sitna1](https://github.com/CameraNexus/sitina1), 2025; [BellssGit/ICX453_CCD_Mirrorless_Camera](https://github.com/BellssGit/ICX453_CCD_Mirrorless_Camera), 2025). The benefit is obvious, large amount of avaliable GPIOs, precise control of timing, as well as fast customized calculations such as intensity histograms demostrated in those projects. However, FPGAs also have serval noticeable wasknesses: price, power consumption, as well as difficulty in development.

Therefore, most projects, especially linear CCD projects, still sticks to MCUs or USB chips as timing generator ([smr547/cam86](https://github.com/smr547/cam86), 2016; [divertingPan/Line_Scan_Camera](https://github.com/divertingPan/Line_Scan_Camera), 2021; [openlux/openlux-v1](https://github.com/openlux/openlux-v1), 2022; [drmcnelson/TCD1304-Sensor-Device-with-Linear-Response-and-16-Bit-Differential-ADC](https://github.com/drmcnelson/TCD1304-Sensor-Device-with-Linear-Response-and-16-Bit-Differential-ADC), 2025). Although easier to develop, most of theses projects are limited to the MCUs, or limited to how these MCUs can generate clock signals, and could only reach a readout speed of 0.5-3 million pixels per second (MSPS). 

Release of RP2040 changed the game. RP2040, as well as later RP2350, are shipped with an unique peripheral called Programmable I/O (PIO), which allows you to control IO pins by exact system clock. Thanks to this feature, the chip was quickly adopted in a wide range of applications, espacially in modding a multi-play-mode gaming device. In this project, by utilizing this feature, we are able to push our system pixel rate dramaticly up to 20.5MSPS without a FPGA.

## Methodology

### Overview of PIO in RP2040
Each RP2040 is equipped with 2 PIO peripherals, with 4 State Machines (SM)s in each PIO. For each PIO, it has its own 32-slot instruction memory, 4-SMs shared. And each SM has its own TX/RX FIFO buffer sized 4x32bit each, and could be merged into a 8x32bit TX/RX buffer if only using TX or RX.

![RP2040_PIO_Overview](../../Resources/RP2040_PIO_Overview.png)

When SMs are configured running, each SM execute 1 instruction in parallel in 1 clock cycle (unless stalled). Clock speed of SMs could be adjusted seperately, using a clock divider. When configured, a SM may control up to 10 IOs, with 5 regular pins, which need a SET instruction and consume clock cycle, and 5 side-set pins, which could be changed along with other instructions' clock cycle.

### State Machines in Project PRISM

In our project, we need to generate clock signals for TCD1708 CCD, 2xAD9826 ADCs, and CY7C68013A working as ADC FIFO buffer. To achieve the generation, we configured all 4 SMs of PIO0. Details of these 4 SMs are described in table below:

| SM# | Pin Assignments | Function Description                                                                                     | Instruction Length |
| --- | --------------- | -------------------------------------------------------------------------------------------------------- | ------------------ |
| 0   | 0-5             | Generates CCD clocks (SH, φ1, φ2, φ2B, RS, CP), sync to CY7C68013A's IFCLK                               | 13                 |
| 1   | 12-18           | Generates ADC1/2 clocks (ADCCLK, CDSCLK2, CDSCLK1), as well as a exposure sync signal, sync to CCD clock | 9                  |
| 2   | 19, 21          | Generates SLWR/PKTEND signal for CY7C68013A, sync to CCD clock                                           | 7                  |
| 3   | 20              | Generates an always-on IFCLK clock for CY7C68013A                                                        | 2                  |

Instead of 1 SM per chip, we use SM1 to generate clocks for both ADCs, since their timings are identical, and we may also save a SM to generate IFCLK for USB FIFO buffer, which is a CY7C68013A in synchronous mode. 

#### SM0
```asm
.program line_sig_generate
.side_set 1
.wrap_target
line_sig_init:                                  ; 54321(0) <- GPIO 5-0
    set pins, 0b01001       [1]     side 0      ; HHLLH(L), RS high CP low (16ns)
    set pins, 0b10001       [1]     side 0      ; HLLLH(L), RS low CP high (16ns)
    set pins, 0b00001       [15]    side 0      ; ┌ LLLLH(L), RW/CP low (128ns)
    out x, 16               [15]    side 0      ; | LLLLH(L), SH high (8ns + 120ns)   Load & controls exposure time, as well as enable of exposure
                                                ; └-> >= 200ns in total (t18)
sh_high_loop:                                   ; ┌
    jmp x-- sh_high_loop    [11]    side 1      ; | LLLLH(H), SH high (96ns)*(x+1)
                                                ; └-> >= 1000ns in total (t3)
    out x, 16               [15]    side 0      ; ┌ LLLLH(L), SH low, Load counter value=3796 (128ns)
    irq wait  5             [14]    side 0      ; |  Sync with FIFO clk (120ns)    # idk why it only need 1 cycle delay, should be 2 cycles in theory
                                                ; └-> >= 200ns in total (t19)
    irq clear 4             [0]     side 0      ; -2 clear IRQ4, for state machine syncing (expected 16ns, -8ns for MOSFET delay)
line_sig_loop:
    set pins, 0b01110       [1]     side 0      ; 0  S0 - HHHHL(L), RS high CP low (16ns)
                                                ; 1
    set pins, 0b10110       [1]     side 0      ; 2  S1 - HLHHL(L), RS low CP high (16ns)
                                                ; 3
    set pins, 0b00110       [1]     side 0      ; 4  S2 - LLHHL(L), RS/CP low, ready for refence sample (16ns)
                                                ; 5
    set pins, 0b00001       [3]     side 0      ; 6  S3 - LLLLH(L), CLK2B low, enable signal output (32ns) 
                                                ; 7
                                                ; 8  
                                                ; 9
    jmp x-- line_sig_loop   [1]     side 0      ; 10 S4 - LLLLH(L), jump to sig_out_loop_clock, sample pixel NOW (16ns)
                                                ; 11
.wrap
```

Program of SM0 could be segmented into 2 parts: line preparation stage and pixel loop stage. In the line preparation stage, we just basically followed timing chart of TCD1708D, but serval small workarounds:

- The high period of `SH` pin is controlled by low 16 bits data in each word of SM0's fifo, called `Exposure ticks` in our code. We read the value to register `x` each line, so we may control the exposure time with this, in a unit of 12 clock cycles. Program will then loop with the `jmp` command to the same line until enough delay. The minimal value of `Exposure ticks` is 10, which is (10+1)\*12\*8=1056ns, just above the minimal value of `SH` high.
- We later read high 16 bits data in each word of SM0's fifo into register `x`, which controls the number of loops in later pixel clock stage. The number is fixed as 3796 (3797-1), see TCD1708D datasheet.

The later pixel loop stage is just some regular IO sets, and another `jmp` command determine whether to continue pixel clocks or go back to line preparation. For each pixel loop, state 0, 1, 2, and 4 has a 1 clock delay after 1 clock for each execution, and state 3 has 3, which takes us 12 SM clocks each pixel in sum.

![TCD1708D Timing Sequence](../../Resources/TCD1708D_Timing_Sequence.png)

#### SM1
```asm
.program cds_line_generate
.side_set 3
.wrap_target
cds_line_init:
    set pins 0b1000         [0] side 0b000      ;       Exposure Sync
    out x, 32               [0] side 0b000      ;       Load counter value=3796
    irq wait 4              [3] side 0b000      ;       State machine syncing. Set IRQ4 and wait for clear, then wait 1 cycle for loop t=0
                                                ; 0     Detects IRQ clear, exit stall state
                                                ; 1   
                                                ; 2     
cds_pixel_loop:                                 ;       GPIO 14-12/18-16
    set pins 0b0100         [1] side 0b100      ; 3     S0 - HLL, CLK low, CDSCLK2 low, CDSCLK1 high, LSB out
                                                ; 4
    set pins 0b0000         [1] side 0b000      ; 5     S1 - LLL, CLK low, CDSCLK2 low, CDSCLK1 low, sampleing reference in 2ns
                                                ; 6
    set pins 0b0010         [1] side 0b010      ; 7     S2 - LHL, CLK low, CDSCLK2 high, CDSCLK1 low
                                                ; 8
    set pins 0b0011         [1] side 0b011      ; 9     S3 - LHH, CLK high, CDSCLK2 high, CDSCLK1 low, MSB out
                                                ; 10
    set pins 0b0001         [1] side 0b001      ; 11    S4 - LLH, CLK high, CDSCLK2 low, CDSCLK1 high, sampleing signal in 2ns
                                                ; 0
    jmp x-- cds_pixel_loop  [1] side 0b001      ; 1     S5 - LLH, Check whether to continue pixel loop
                                                ; 2
.wrap
```

SM1 controls colck signal of both ADC, which have been configured in 1-channel CDS mode after power up. Similar to SM0, the program here is just basically an implementation of timing chart below. We also read a value from fifo each row into `x`, so we can control the number of pixel loops. The value is fixed same as SM0's (3796=3797-1).

![AD9826 Timing Sequence](../../Resources/AD9826_Timing_Sequence.png)

#### SM2
```asm
.program fifo_line_generate_sync
.side_set 3
.wrap_target
fifo_line_sync_init:
    out y, 16                   [0] side 0b101   ;          SLWR high, PKTEND high
    irq wait 4                  [3] side 0b111   ;          CLK Generation in idle                  CLK LHHH
                                                 ; 0
                                                 ; 1
                                                 ; 2
    out x, 16                   [2] side 0b100   ; 3        Load counter value=3797*2-1=7593, LLH SLWR low, prepare for bus sample     CLK LLL
                                                 ; 4
                                                 ; 5
fifo_line_sync_loop:                             ;       GPIO 19(20)21
    nop                         [2] side 0b110   ; 6    0   S0 - HHL CLK rising, sample bus data    CLK HHH
                                                 ; 7    1       
                                                 ; 8    2     
    jmp x-- fifo_line_sync_loop [2] side 0b100   ; 9    3   S1 - HLL CLK falling                    CLK LLL
                                                 ; 10   4     
                                                 ; 11   5     
fifo_line_sync_end:
    jmp !y fifo_line_sync_init  [2] side 0b110   ;                                                  CLK HHH
    nop                         [3] side 0b001   ;          PKTEND low CLK rising, execute flush    CLK LLLH
.wrap
```

Program of SM2 reads 2x16 bits data into register `y` and `x` seperately in each row. Register `x` works as pixel loop counter like SM0 and SM1, but its value is 7593(3797*2-1) instead of 3796, since our ADCs are sampling CCD outputs into 16-bit values. Register `y` is an indicator of manual packet send. The USB buffer had been configured to send a package each time it got 512 bytes, however, our ADCs gives 15190 bytes (7595 pixels, 7594 row pixels + extra 2 bytes when exiting pixel loop, we don't have enough time to pull `SLWR` high), which could not be divided by 512. Thus, we need this indicator to send the extra bytes everytime we want to end a transaction.

You may have also noticed, SM2 is assigned pin 19 & 21 for `SLWR` and `PKTEND` signal, but we are using a 3-bit value in side-set, in which bit-0 is `SLWR` and bit-2 is `PKTEND`. This is due to PIO SMs could only be assigned with continous pin ranges, and we have to put pin 20 here and ignore it. In fact, this is a mistake, pin 19 and pin 20, `SLWR` and `IFCLK` shuold have changed their position in curcit design, but later we found the board is still running, and we do need another SM for a freely running `IFCLK`, so it's kept here.

![CY7C68013A Timing Sequence](../../Resources/CY7C68013A_Timing_Sequence.png)

#### SM3
```asm
.program ifclk_generate
.side_set 1
.wrap_target
    irq clear 5             [2] side 1  ; Sync with SMs
    nop                     [2] side 0
.wrap
```

SM3 is loaded with a simple program chich toggles pin20 to generate `IFCLK`.


### In Syncing the State Machines

You may have noticed, there are `irq clear` or `irq wait` instructions in all SMs, and it is these instructions that keep all the SMs in sync. This is so far the hardest part in imaging subsystem development, and it took weeks to get it working.

First, a short explain of these two instructions:
- `irq clear`: Clear an irq flag. Same as other instructions, takes 1 SM clock to clear.
- `irq wait`: Set an irq flag, and wait until the flag is cleared. After the execution of this instruction, the waiting SM will check whether given irq flag is cleared or not. If cleared, the SM continue to next instruction in next SM clock after the detection of irq clear.

The following table shows how these two instructions works (assume they work in same frequency):

| SM Clock | SM0                      | SM1             |
| -------- | ------------------------ | --------------- |
| 0        | **irq wait 4**           | something else… |
| 1        | waiting…                 |                 |
| 2        |                          | **irq clear 4** |
| 3        | ***irq clear detected*** | something else… |
| 4        | continue to instruction… |                 |
| 5        |                          |                 |

In the example above, SM0 sets irq flag 4 and enters waiting since clock #0, and then SM1 clears irq flag 4 in clock #2. SM0 detects the flag was cleared until clock #3, and the execution of instructions is not resumed until clock #4.

We start with SM0 first, since it is the base of our clocks. In every line preparation period, SM0 enters waiting through `irq wait 5`, and wait to be cleared by SM3. Once SM3 cleared the flag, SM0 continues to delay 14 clocks of `irq wait 5` in 2 clocks.

| Absolute CLK# | Pixel CLK# | Relative CLK# | SM0              | SM1 | SM2 | SM3                 |
| ------------- | ---------- | ------------- | ---------------- | --- | --- | ------------------- |
| -18           |            |               | (irq wait 5)     |     |     | irq clear 5, side 1 |
| -17           |            |               | Exit Stall State |     |     | 1                   |
| -16           |            |               | delay 1/14       |     |     | 1                   |
| -15           |            |               | delay 2/14       |     |     | 0                   |
| -14           |            |               | delay 3/14       |     |     | 0                   |

Then, after the 14 clocks delay, SM0 runs `irq clear 4` to wake up SM1 and SM2. SM1 and SM2 then start to delay the clocks set in `irq wait 4` instruction.

| Absolute CLK# | Pixel CLK# | Relative CLK# | SM0           | SM1              | SM2                 | SM3 |
| ------------- | ---------- | ------------- | ------------- | ---------------- | ------------------- | --- |
| -3            |            |               | delay 14/14   |                  |                     | 0   |
| -2            |            |               | irq clear 4   | (irq wait 4)     | (irq wait 4)        |     |
| -1            |            |               | (MOS delay)   | Exit Stall State | Exit Stall State    |     |
| 0             | 0          | 0             | S0            | delay 1/3        | delay 1/3           | 1   |
| 1             |            | 1             | delay 1       | delay 2/3        | delay 2/3           |     |
| 2             |            | 2             | S1            | delay 3/3        | delay 3/3           |     |
| 3             |            | 3             | delay 1       | S0               | out x               | 0   |
| 4             |            | 4             | S2 (ref. out) | delay 1          | delay 1/2           |     |
| 5             |            | 5             | delay 1       | S1 (ref. sample) | delay 2/2           |     |
| 6             |            | 6             | S3            | delay 1          | S0 (ADC bus sample) | 1   |
| 7             |            | 7             | delay 1/3     | S2               | delay 1/2           |     |
| 8             |            | 8             | delay 2/3     | delay 1          | delay 1/2           |     |
| 9             |            | 9             | delay 3/3     | S3               | S1                  | 0   |
| 10            |            | 10            | S4 (sig. out) | delay 1          | delay 1/2           |     |
| 11            |            | 11            | delay 1       | S4 (sig. sample) | delay 2/2           |     |
| 12            | 1          | 0             | S0            | delay 1          | S0 (ADC bus sample) | 1   |
| 13            |            | 1             | delay 1       | S5               | delay 1/2           |     |
| 14            |            | 2             | S1            | delay 1          | delay 2/2           |     |

Here SM0 has a extra `MOS delay`, which is not a actual instruction. It is a ~8ns delay of UCC27524 MOS driver, and we put it here since it could help syncing the SMs.

And then our SMs are synced, our CCD clocks are generated. ADC clocks are 1 clock delayed than CCD clocks, in order to get stable voltages. Bus sample of USB buffer happens 3 clocks after ADCCLK changes, making sure we are not getting wrong data.

For the rest of pixel clocks, they are exactly identical to CLK#3-12. After that, the clocks enters preparation period again. Since the preparation of SM0 is way longer than SM1 and SM2, we are ignoreing their states after the pixel clock ends until they're wake up again. 

| Absolute CLK# | Pixel CLK# | Relative CLK# | SM0            | SM1              | SM2                 | SM3 |
| ------------- | ---------- | ------------- | -------------- | ---------------- | ------------------- | --- |
| 45561         | 3796       | 10            | S4 (sig. out)  | delay 1          | delay 1/2           | 0   |
| 45562         |            | 11            | delay 1        | S4 (sig. sample) | delay 2/2           |     |
| 45563         | X          |               | set pins       | delay 1          | S0 (ADC bus sample) | 1   |
| 45564         |            |               | delay 1        | S5               | delay 1/2           |     |
| 45565         |            |               | set pins       | delay 1          | delay 2/2           |     |
| 45566         |            |               | delay 1        | delay 1          | S1                  | 0   |
| 45567         |            |               | set pins       | S5               | delay 1/2           |     |
| 45568         |            |               | delay 1/15     | delay 1          | delay 2/2           |     |
| 45569         |            |               | delay 2/15     | ...              | ...                 | 1   |
| ...           |            |               | ...            |                  |                     | ... |
| 45583         |            |               | delay 15/15    |                  |                     | 1   |
| 45584         |            |               | out x          |                  |                     |     |
| 45585         |            |               | delay 1/15     |                  |                     | 0   |
| ...           |            |               | ...            |                  |                     | ... |
| 45600         |            |               | delay 12*(x+1) |                  |                     | 1   |

Here we suppose `Exposure ticks` is 11, and `SH` will be set high for 1152ns. The reason why the delay here is degisned as 12*(x+1) is, 12 clock cycles could make SM3 run 2 entire cycles. We have aligned SM0's execution and SM3's using the tons of delay avaliable in line preparation, and it will make aligning following instructions much more easier.

| Absolute CLK# | Pixel CLK# | Relative CLK# | SM0              | SM1              | SM2              | SM3                 |
| ------------- | ---------- | ------------- | ---------------- | ---------------- | ---------------- | ------------------- |
| 45600         |            |               | delay 12*(x+1)   |                  |                  | 1                   |
| ...           |            |               | ...              |                  |                  | ...                 |
| 45743         |            |               | delay 12/12      |                  |                  | 0                   |
| 45744         |            |               | delay 1/15       |                  |                  | 1                   |
| ...           |            |               | ...              |                  |                  | ...                 |
| 45758         |            |               | delay 15/15      |                  |                  | 1                   |
| 45799         |            |               | irq wait 5       |                  |                  | 0                   |
| 45760         |            |               | Stalled          |                  |                  |                     |
| 45761         |            |               | Stalled          |                  |                  |                     |
| 45762         |            |               | Stalled          |                  |                  | irq clear 5, side 1 |
| 45763(-17)    |            |               | Exit Stall State |                  |                  |                     |
| 45764(-16)    |            |               | delay 1/14       |                  |                  |                     |
| ...           |            |               | ...              |                  |                  | ...                 |
| 45777(-3)     |            |               | delay 14/14      |                  |                  | 0                   |
| 45778(-2)     |            |               | irq clear 4      | (irq wait 4)     | (irq wait 4)     |                     |
| 45779(-1)     |            |               | (MOS delay)      | Exit Stall State | Exit Stall State |                     |
| 45780(0)      | 0          | 0             | S0               | delay 1/3        | delay 1/3        | 1                   |

After the `SH` delay, we continue to the 15-clocks delay of `jmp` command. After that, we encounter `irq wait 5`, set irq 5 and wait for SM3 to clear. The timing of this command just locate serval clock before next `irq clear 5` of SM3, and SM0 will not wait for too long. As SM0 wake up, the state of all SMs goes back to the exact same as we discussed at first place. Then we may conclude that, for each line of scan, it takes 45780 clock cycles to run, as `Exposure ticks`=11. Excluding the `SH` delay, it will be 45636 clocks.

For a full timing sequence, please check [State Machine Sequence.xlsx](<State Machine Sequence.xlsx>).

### Exposure Time Control

Following the timing table above, in current settings, we need 45636 clocks each scan row (exclude expsorure ticks).

These timing numbers assume the default `125MHz` RP2040 system clock. If you store a different `prism.sys_clock_khz` value through the control interface, the firmware reapplies that frequency on boot and the real-time exposure/readout durations scale with the new clock.

And here is a quick lookup table if you want to change line exposure time.

| exposure ticks | row clock cycles | time per frame (ms) | shutter speed   | data rate   |
| -------------- | ---------------- | ------------------- | --------------- | ----------- |
| ~~0~~          | ~~45648~~        | ~~0.365184~~        | ~~1/2738.3456~~ | N/A         |
| 11             | 45780            | 0.36624             | 1/2730.45       | ~40.5MiB/s  |
| 363            | 50004            | 0.400032            | 1/2499.8        | ~36.65MiB/s |
| 1404           | 62496            | 0.499968            | 1/2000.1280     | ~29.62MiB/s |
| 2706           | 78120            | 0.62496             | 1/1600.1024     | ~23.62MiB/s |
| 4529           | 99996            | 0.799968            | 1/1250.05       | ~18.38MiB/s |
| 6613           | 125004           | 1.000032            | 1/999.968       | ~14.68MiB/s |

### Overclocking

The RP2040 could be easily overclocked, and no extra modifications required as clock speed <= 270Mhz. However, pushing the clock speed too hard will damage our timing design, and ADC may not able to sample correct voltage. This section is just for a record, it is not recommended to overclock in actual use.

| clock speed | exposure ticks | row clock cycles | time per frame (ms) | shutter speed | data rate   |
| ----------- | -------------- | ---------------- | ------------------- | ------------- | ----------- |
| 125Mhz      | 10             | 45768            | 0.36614             | 1/2730.45     | ~40.59MiB/s |
| 133MHz      | 11             | 45780            | 0.34421             | 1/2905.20     | ~43.66MiB/s |
| 135MHz      | 11             | 45780            | 0.33911             | 1/2948.89     | ~44.38MiB/s |

## Conclusion

Thanks for reading, and I hope this article could help you in your development. 

The PIO in RP2040/RP2350 is really a powerful peripheral, and it makes many projects with pricise timing controls now possible. I hope to see more MCUs with similar feature avaliable on market in the future.
