#pragma NOIV // Do not generate interrupt vectors
//-----------------------------------------------------------------------------
//   File:       Project_PRISM_CY7C68013A.c
//   Contents:   USB FIFO implementation.
//
// Copyright (c) 2026, mr258876
// SPDX-License-Identifier: MIT
//-----------------------------------------------------------------------------
#include "fx2.h"
#include "fx2regs.h"
#include "fx2sdly.h" // SYNCDELAY macro

extern BOOL GotSUD; // Received setup data flag
extern BOOL Sleep;
extern BOOL Rwuen;
extern BOOL Selfpwr;

enum
{
	Alt0_BulkIN = 0,
	Alt1_BulkOUT,
	Alt2_BulkINOUT,
	Alt3_IsocIN,
	Alt4_IsocOUT,
	Alt5_IsocIN,
	Alt6_IsocINOUT
};

enum
{
	Full_Alt0_BulkIN = 0,
	Full_Alt1_BulkOUT,
	Full_Alt2_IsocIN,
	Full_Alt3_IsocOUT
};

BYTE xdata Digit[] = {0xc0, 0xf9, 0xa4, 0xb0, 0x99, 0x92, 0x82, 0xf8, 0x80, 0x98, 0x88, 0x83, 0xc6, 0xa1, 0x86, 0x8e};

BYTE Configuration;					 // Current configuration
BYTE AlternateSetting = Alt0_BulkIN; // Alternate settings

//-----------------------------------------------------------------------------
// Task Dispatcher hooks
//   The following hooks are called by the task dispatcher.
//-----------------------------------------------------------------------------

void TD_Init(void) // Called once at startup
{
	// set the CPU clock to 48MHz
	CPUCS = ((CPUCS & ~bmCLKSPD) | bmCLKSPD1);
	SYNCDELAY;

	PINFLAGSAB = 0x08; // FLAGA - EP2EF
	SYNCDELAY;
	PINFLAGSCD = 0xE0; // FLAGD - EP6FF
	SYNCDELAY;
	PORTACFG |= 0x80;
	SYNCDELAY;

	EZUSB_Delay(100);	// Wait for external clock generation
	// Internal clock, 48 MHz, Asynchronous Mode, Slave FIFO interface
	// IFCONFIG = 0xCB; // 0b11001011
	// External clock, 48 MHz, Synchronous Mode, Slave FIFO interface
	IFCONFIG = 0x43; // 0b01000011
	SYNCDELAY;

	// Default interface uses endpoint 2, zero the valid bit on all others
	// Just using endpoint 2, zero the valid bit on all others
	EP1OUTCFG = (EP1OUTCFG & 0x7F);
	SYNCDELAY;
	EP1INCFG = (EP1INCFG & 0x7F);
	SYNCDELAY;
	EP2CFG = 0xE0; // EP2 is DIR=IN, TYPE=BULK, SIZE=512, BUF=4x
	SYNCDELAY;
	EP4CFG = (EP4CFG & 0x7F);; // Clear valid bit
	SYNCDELAY;
	EP6CFG = (EP6CFG & 0x7F);
	SYNCDELAY;
	EP8CFG = (EP8CFG & 0x7F);
	SYNCDELAY;

	// Reset FIFO Buffers
	FIFORESET = 0x80; // activate NAK-ALL to avoid race conditions
	SYNCDELAY;		  // see TRM section 15.14
	FIFORESET = 0x02; // reset, FIFO 2
	SYNCDELAY;		  //
	FIFORESET = 0x04; // reset, FIFO 4
	SYNCDELAY;		  //
	FIFORESET = 0x06; // reset, FIFO 6
	SYNCDELAY;		  //
	FIFORESET = 0x08; // reset, FIFO 8
	SYNCDELAY;		  //
	FIFORESET = 0x00; // deactivate NAK-ALL
	SYNCDELAY;		  //

	// Set the FIFO configuration for the used endpoints
	EP2FIFOCFG = 0x0D; // AUTOIN=1, ZEROLENIN=1, WORDWIDE=1
	SYNCDELAY;

	Rwuen = TRUE; // Enable remote-wakeup
}

void TD_Poll(void) // Called repeatedly while the device is idle
{
	SYNCDELAY;	// We don't need the CPU to do anything
}

BOOL TD_Suspend(void) // Called before the device goes into suspend mode
{
	return (TRUE);
}

BOOL TD_Resume(void) // Called after the device resumes
{
	return (TRUE);
}

//-----------------------------------------------------------------------------
// Device Request hooks
//   The following hooks are called by the end point 0 device request parser.
//-----------------------------------------------------------------------------

BOOL DR_GetDescriptor(void)
{
	return (TRUE);
}

BOOL DR_GetWcidDescriptor(void)
{
	return (TRUE);
}

BOOL DR_SetConfiguration(void) // Called when a Set Configuration command is received
{
	if (EZUSB_HIGHSPEED())
	{						  // ...FX2 in high speed mode
		EP2AUTOINLENH = 0x02; // set core AUTO commit len = 512 bytes
		SYNCDELAY;
		EP2AUTOINLENL = 0x00;
		SYNCDELAY;
	}
	else
	{						  // ...FX2 in full speed mode
		EP2AUTOINLENH = 0x00; // set core AUTO commit len = 64 bytes
		SYNCDELAY;
		EP2AUTOINLENL = 0x40;
		SYNCDELAY;
	}

	Configuration = SETUPDAT[2];
	return (TRUE); // Handled by user code
}

BOOL DR_GetConfiguration(void) // Called when a Get Configuration command is received
{
	EP0BUF[0] = Configuration;
	EP0BCH = 0;
	EP0BCL = 1;
	return (TRUE); // Handled by user code
}

BOOL DR_SetInterface(void) // Called when a Set Interface command is received
{
	AlternateSetting = SETUPDAT[2];
	return (TRUE); // Handled by user code
}

BOOL DR_GetInterface(void) // Called when a Set Interface command is received
{
	EP0BUF[0] = AlternateSetting;
	EP0BCH = 0;
	EP0BCL = 1;
	return (TRUE); // Handled by user code
}

BOOL DR_GetStatus(void)
{
	return (TRUE);
}

BOOL DR_ClearFeature(void)
{
	return (TRUE);
}

BOOL DR_SetFeature(void)
{
	return (TRUE);
}

BOOL DR_VendorCmnd(void)
{
	return (TRUE);
}

//-----------------------------------------------------------------------------
// USB Interrupt Handlers
//   The following functions are called by the USB interrupt jump table.
//-----------------------------------------------------------------------------

// Setup Data Available Interrupt Handler
void ISR_Sudav(void) interrupt 0
{
	GotSUD = TRUE; // Set flag
	EZUSB_IRQ_CLEAR();
	USBIRQ = bmSUDAV; // Clear SUDAV IRQ
}

// Setup Token Interrupt Handler
void ISR_Sutok(void) interrupt 0
{
	EZUSB_IRQ_CLEAR();
	USBIRQ = bmSUTOK; // Clear SUTOK IRQ
}

void ISR_Sof(void) interrupt 0
{

	EZUSB_IRQ_CLEAR();
	USBIRQ = bmSOF; // Clear SOF IRQ
}

void ISR_Ures(void) interrupt 0
{
	// Whenever we get a USB Reset, we should revert to full speed mode
	pConfigDscr = pFullSpeedConfigDscr;
	((CONFIGDSCR xdata *)pConfigDscr)->type = CONFIG_DSCR;
	pOtherConfigDscr = pHighSpeedConfigDscr;
	((CONFIGDSCR xdata *)pOtherConfigDscr)->type = OTHERSPEED_DSCR;

	EZUSB_IRQ_CLEAR();
	USBIRQ = bmURES; // Clear URES IRQ
}

void ISR_Susp(void) interrupt 0
{
	Sleep = TRUE;
	EZUSB_IRQ_CLEAR();
	USBIRQ = bmSUSP;
}

void ISR_Highspeed(void) interrupt 0
{
	if (EZUSB_HIGHSPEED())
	{
		pConfigDscr = pHighSpeedConfigDscr;
		((CONFIGDSCR xdata *)pConfigDscr)->type = CONFIG_DSCR;
		pOtherConfigDscr = pFullSpeedConfigDscr;
		((CONFIGDSCR xdata *)pOtherConfigDscr)->type = OTHERSPEED_DSCR;

		// This register sets the number of Isoc packets to send per
		// uFrame.  This register is only valid in high speed.
		EP2ISOINPKTS = 0x03;
	}
	else
	{
		pConfigDscr = pFullSpeedConfigDscr;
		pOtherConfigDscr = pHighSpeedConfigDscr;
	}

	EZUSB_IRQ_CLEAR();
	USBIRQ = bmHSGRANT;
}
void ISR_Ep0ack(void) interrupt 0
{
}
void ISR_Stub(void) interrupt 0
{
}
void ISR_Ep0in(void) interrupt 0
{
}
void ISR_Ep0out(void) interrupt 0
{
}
void ISR_Ep1in(void) interrupt 0
{
}
void ISR_Ep1out(void) interrupt 0
{
}

void ISR_Ep2inout(void) interrupt 0
{
}
void ISR_Ep4inout(void) interrupt 0
{
}
void ISR_Ep6inout(void) interrupt 0
{
}
void ISR_Ep8inout(void) interrupt 0
{
}
void ISR_Ibn(void) interrupt 0
{
}
void ISR_Ep0pingnak(void) interrupt 0
{
}
void ISR_Ep1pingnak(void) interrupt 0
{
}
void ISR_Ep2pingnak(void) interrupt 0
{
}
void ISR_Ep4pingnak(void) interrupt 0
{
}
void ISR_Ep6pingnak(void) interrupt 0
{
}
void ISR_Ep8pingnak(void) interrupt 0
{
}
void ISR_Errorlimit(void) interrupt 0
{
}
void ISR_Ep2piderror(void) interrupt 0
{
}
void ISR_Ep4piderror(void) interrupt 0
{
}
void ISR_Ep6piderror(void) interrupt 0
{
}
void ISR_Ep8piderror(void) interrupt 0
{
}
void ISR_Ep2pflag(void) interrupt 0
{
}
void ISR_Ep4pflag(void) interrupt 0
{
}
void ISR_Ep6pflag(void) interrupt 0
{
}
void ISR_Ep8pflag(void) interrupt 0
{
}
void ISR_Ep2eflag(void) interrupt 0
{
}
void ISR_Ep4eflag(void) interrupt 0
{
}
void ISR_Ep6eflag(void) interrupt 0
{
}
void ISR_Ep8eflag(void) interrupt 0
{
}
void ISR_Ep2fflag(void) interrupt 0
{
}
void ISR_Ep4fflag(void) interrupt 0
{
}
void ISR_Ep6fflag(void) interrupt 0
{
}
void ISR_Ep8fflag(void) interrupt 0
{
}
void ISR_GpifComplete(void) interrupt 0
{
}
void ISR_GpifWaveform(void) interrupt 0
{
}
