;;-----------------------------------------------------------------------------
;;   File:      dscr.a51
;;   Contents:  USB descriptor of fifo buffer.  
;;
;;   Copyright (c) 2026, mr258876
;;-----------------------------------------------------------------------------

   
DSCR_DEVICE   equ   1   ;; Descriptor type: Device
DSCR_CONFIG   equ   2   ;; Descriptor type: Configuration
DSCR_STRING   equ   3   ;; Descriptor type: String
DSCR_INTRFC   equ   4   ;; Descriptor type: Interface
DSCR_ENDPNT   equ   5   ;; Descriptor type: Endpoint
DSCR_DEVQUAL  equ   6   ;; Descriptor type: Device Qualifier

DSCR_DEVICE_LEN   equ   18
DSCR_CONFIG_LEN   equ    9
DSCR_INTRFC_LEN   equ    9
DSCR_ENDPNT_LEN   equ    7
DSCR_DEVQUAL_LEN  equ   10

ET_CONTROL   equ   0   ;; Endpoint type: Control
ET_ISO       equ   1   ;; Endpoint type: Isochronous
ET_BULK      equ   2   ;; Endpoint type: Bulk
ET_INT       equ   3   ;; Endpoint type: Interrupt

public      DeviceDscr, DeviceQualDscr, HighSpeedConfigDscr, FullSpeedConfigDscr, WcidFeatureDscr, WcidExtendedFeatureDscr, StringDscr, UserDscr

;DSCR   SEGMENT   CODE

;;-----------------------------------------------------------------------------
;; Global Variables
;;-----------------------------------------------------------------------------
;      rseg DSCR     ;; locate the descriptor table in on-part memory.

CSEG   AT 100H


DeviceDscr:   
      db   DSCR_DEVICE_LEN    ;; Descriptor length
      db   DSCR_DEVICE        ;; Decriptor type
      dw   0002H              ;; Specification Version (BCD)
      db   00H                ;; Device class
      db   00H                ;; Device sub-class
      db   00H                ;; Device sub-sub-class
      db   64                 ;; Maximum packet size
/*      
      dw   0B404H             ;; Vendor ID (Sample Device)
      dw   0F100H             ;; Product ID (Sample Device)
*/
      dw   0501DH             ;; Vendor ID 0x1D50
      dw   09C61H             ;; Product ID 0x619C
      dw   0000H              ;; Product version ID
      db   1                  ;; Manufacturer string index
      db   2                  ;; Product string index
      db   0                  ;; Serial number string index
      db   1                  ;; Number of configurations

org (($ / 2) +1) * 2

DeviceQualDscr:
      db   DSCR_DEVQUAL_LEN   ;; Descriptor length
      db   DSCR_DEVQUAL       ;; Decriptor type
      dw   0002H              ;; Specification Version (BCD)
      db   00H                ;; Device class
      db   00H                ;; Device sub-class
      db   00H                ;; Device sub-sub-class
      db   64                 ;; Maximum packet size
      db   1                  ;; Number of configurations
      db   0                  ;; Reserved

org (($ / 2) +1) * 2

HighSpeedConfigDscr:   
      db   DSCR_CONFIG_LEN      ;; Descriptor length
      db   DSCR_CONFIG          ;; Descriptor type
      db   (HighSpeedConfigDscrEnd-HighSpeedConfigDscr) mod 256 ;; Total Length (LSB)
      db   (HighSpeedConfigDscrEnd-HighSpeedConfigDscr)  /  256 ;; Total Length (MSB)
      db   1                    ;; Number of interfaces
      db   1                    ;; Configuration number
      db   0                    ;; Configuration string
      db   10100000b            ;; Attributes (b7 - buspwr, b6 - selfpwr, b5 - rwu)
      db   50                   ;; Power requirement (div 2 ma)

;; Interface Descriptor
      db   DSCR_INTRFC_LEN      ;; Descriptor length
      db   DSCR_INTRFC          ;; Descriptor type
      db   0                    ;; Zero-based index of this interface
      db   0                    ;; Alternate setting
      db   1                    ;; Number of end points 
      db   0ffH                 ;; Interface class
      db   00H                  ;; Interface sub class
      db   00H                  ;; Interface sub sub class
      db   0                    ;; Interface descriptor string index
      
;; Endpoint Descriptor
      db   DSCR_ENDPNT_LEN      ;; Descriptor length
      db   DSCR_ENDPNT          ;; Descriptor type
      db   82H                  ;; Endpoint number, and direction
      db   ET_BULK              ;; Endpoint type
      db   00H                  ;; Maximum packet size (LSB)
      db   02H                  ;; Maximum packet size (MSB)
      db   00H                  ;; Polling interval

HighSpeedConfigDscrEnd:   

org (($ / 2) +1) * 2

FullSpeedConfigDscr:   
      db   DSCR_CONFIG_LEN      ;; Descriptor length
      db   DSCR_CONFIG          ;; Descriptor type
      db   (FullSpeedConfigDscrEnd-FullSpeedConfigDscr) mod 256 ;; Total Length (LSB)
      db   (FullSpeedConfigDscrEnd-FullSpeedConfigDscr)  /  256 ;; Total Length (MSB)
      db   1                    ;; Number of interfaces
      db   1                    ;; Configuration number
      db   0                    ;; Configuration string
      db   10100000b            ;; Attributes (b7 - buspwr, b6 - selfpwr, b5 - rwu)
      db   50                   ;; Power requirement (div 2 ma)

;; Interface Descriptor
      db   DSCR_INTRFC_LEN      ;; Descriptor length
      db   DSCR_INTRFC          ;; Descriptor type
      db   0                    ;; Zero-based index of this interface
      db   0                    ;; Alternate setting
      db   1                    ;; Number of end points 
      db   0ffH                 ;; Interface class
      db   00H                  ;; Interface sub class
      db   00H                  ;; Interface sub sub class
      db   0                    ;; Interface descriptor string index
      
;; Endpoint Descriptor
      db   DSCR_ENDPNT_LEN      ;; Descriptor length
      db   DSCR_ENDPNT          ;; Descriptor type
      db   82H                  ;; Endpoint number, and direction
      db   ET_BULK              ;; Endpoint type
      db   40H                  ;; Maximum packet size (LSB)
      db   00H                  ;; Maximum packet size (MSB)
      db   00H                  ;; Polling interval

FullSpeedConfigDscrEnd:   

org (($ / 2) +1) * 2

WcidFeatureDscr:
      dw    2800H,0000H                   ;; Descriptor length
      dw    0001H                         ;; Specification Version (BCD)
      dw    0400H                         ;; Descriptor Index
      db    1                             ;; Number of interfaces
      db    0,0,0,0,0,0,0                 ;; Reserved
;; WCID Interface Descriptor
      db    0                             ;; Zero-based index of this interface
      db    1                             ;; Reserved
      db    'W','I','N','U','S','B',0,0   ;; CID
      db    0,0,0,0,0,0,0,0               ;; Sub CID
      db    0,0,0,0,0,0                   ;; Reserved

WcidFeatureDscrEnd:

org (($ / 2) +1) * 2

WcidExtendedFeatureDscr:
      dw    8E00H,0000H                   ;; Descriptor length
      dw    0001H                         ;; Specification Version (BCD)
      dw    0500H                         ;; Descriptor Index
      dw    0100H                         ;; Number of interfaces
;; WCID Property Field
      dw    8400H,0000H                   ;; Size of property section
      dw    0100H,0000H                   ;; Property data type (1=Unicode REG_SZ)
      dw    2800H                         ;; Property name length
      db    'D',00                        ;; Property name "DeviceInterfaceGUID"
      db    'e',00
      db    'v',00
      db    'i',00
      db    'c',00
      db    'e',00
      db    'I',00
      db    'n',00
      db    't',00
      db    'e',00
      db    'r',00
      db    'f',00
      db    'a',00
      db    'c',00
      db    'e',00
      db    'G',00
      db    'U',00
      db    'I',00
      db    'D',00
      db    00,00 
      dw    4E00H,0000H                   ;; Property data length
      db    '{',00                        ;; Property name "{89511353-e6b2-3bcd-2a11-df7b0bcfe810}"  MD5 of "Project PRISM FIFO Buffer"
      db    '8',00
      db    '9',00
      db    '5',00
      db    '1',00
      db    '1',00
      db    '3',00
      db    '5',00
      db    '3',00
      db    '-',00
      db    'e',00
      db    '6',00
      db    'b',00
      db    '2',00
      db    '-',00
      db    '3',00
      db    'b',00
      db    'c',00
      db    'd',00
      db    '-',00
      db    '2',00
      db    'a',00
      db    '1',00
      db    '1',00
      db    '-',00
      db    'd',00
      db    'f',00
      db    '7',00
      db    'b',00
      db    '0',00
      db    'b',00
      db    'c',00
      db    'f',00
      db    'e',00
      db    '8',00
      db    '1',00
      db    '0',00
      db    '}',00
      db    00,00

WcidExtendedFeatureDscrEnd:

org (($ / 2) +1) * 2

StringDscr:

StringDscr0:                              ;; Language Code
      db   StringDscr0End-StringDscr0     ;; String descriptor length
      db   DSCR_STRING
      db   09H,04H
StringDscr0End:

StringDscr1:                              ;; Manufacturer
      db   StringDscr1End-StringDscr1     ;; String descriptor length
      db   DSCR_STRING
      db   'g',00
      db   'i',00
      db   't',00
      db   'h',00
      db   'u',00
      db   'b',00
      db   '.',00
      db   'c',00
      db   'o',00
      db   'm',00
      db   '/',00
      db   'm',00
      db   'r',00
      db   '2',00
      db   '5',00
      db   '8',00
      db   '8',00
      db   '7',00
      db   '6',00
StringDscr1End:

StringDscr2:                              ;; Product Name
      db   StringDscr2End-StringDscr2     ;; Descriptor length
      db   DSCR_STRING
      db   'P',00
      db   'r',00
      db   'o',00
      db   'j',00
      db   'e',00
      db   'c',00
      db   't',00
      db   ' ',00
      db   'P',00
      db   'R',00
      db   'I',00
      db   'S',00
      db   'M',00
      db   ' ',00
      db   'F',00
      db   'I',00
      db   'F',00
      db   'O',00
      db   ' ',00
      db   'B',00
      db   'u',00
      db   'f',00
      db   'f',00
      db   'e',00
      db   'r',00
StringDscr2End:

StringDscr3:                              ;; Serial Number
      db   StringDscr3End-StringDscr3     ;; Descriptor length
      db   DSCR_STRING
      db   'B',00
      db   'u',00
      db   'l',00
      db   'k',00
      db   '-',00
      db   'I',00
      db   'N',00
StringDscr3End:

StringDscr4:                              ;; For WCID 1.0 Enumeration
      db   StringDscr4End-StringDscr4     ;; Descriptor length
      db   DSCR_STRING
      db   'M',00
      db   'S',00
      db   'F',00
      db   'T',00
      db   '1',00
      db   '0',00
      db   '0',00
      db   17H,00
StringDscr4End:

UserDscr:      
      dw   0000H
      end
      
