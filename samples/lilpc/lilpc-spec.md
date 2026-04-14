# lilpc : the little 286 PC

Introduction ***TBD***

## CPU

***TODO: write the spec***

## Graphics / Display

***TODO: write the spec***

## Sound

### PC Speaker

***NOT IMPLEMENTED***

### Disney Sound Source and Covox Speech Thing

***NOT IMPLEMENTED***

## User Peripherials

### Keyboard

***TODO: write the spec***

### Mouse

***NOT IMPLEMENTED***

### Joystick

***NOT IMPLEMENTED***

Gravis PC GamePad would have been available in 1991 and commonly found in 1992. Not unusual to have as an upgrade to an old computer.

## Storage / Media

### Floppy

***TODO: write the spec***

### SCSI Bus - NCR 53C80 (5380)

***NOT IMPLEMENTED***

Computer uses an NCR 5380-compatible SCSI controller for mass storage. The 5380 was used in the Macintosh Plus/SE/Classic/II, Amiga A3000, and many others.

The 5380 is an 8-bit SCSI-1 controller. It has only 8 registers, and its
DRQ/DACK signals should be wired to the ISA DMA for bulk data transfers:

| Register | Offset | Description |
|---|---|---|
| Current SCSI Data | 0x00 | Data bus value (active data during transfer) |
| Initiator Command | 0x01 | Assert SCSI bus signals (SEL, ATN, BSY, ACK, etc.) |
| Mode | 0x02 | Enable target mode, DMA mode, parity, etc. |
| Target Command | 0x03 | Assert I/O, C/D, MSG signals |
| Current SCSI Bus Status | 0x04 | Read SCSI bus signal state |
| Bus and Status | 0x05 | Phase match, DMA request, parity error, IRQ active |
| Input Data | 0x06 | Latched input data (valid during handshake) |
| Reset Parity/Interrupt | 0x07 | Clear parity error and IRQ (read to clear) |

**SCSI IDs:**
- ID 7: Host System (initiator, highest priority)
- ID 0: Internal hard drive (40 MB)
- ID 1: Internal CD-ROM drive
- IDs 2–6: Available for expansion

### Internal Hard Drive - 40 MB SCSI

- 40 MB capacity (plausible for 1980's -- 100+ MB drives were upper range by 1989)
- SCSI-1 commands: INQUIRY, READ(6)/READ(10), WRITE(6)/WRITE(10), TEST UNIT READY, REQUEST SENSE
- For emulation: backed by a flat file (raw disk image or per-partition files)

### Internal CD-ROM Drive - 650 MB

- not likely to be on a 1980's 286. but perhaps easy for us to support in emulation.
- Read-only, 650 MB (standard CD-ROM / ISO 9660)
- 2× speed (~300 KB/s) - era-appropriate for a budget console
- SCSI-1 commands: INQUIRY, READ(10), READ TOC, TEST UNIT READY, REQUEST SENSE
- Supports CD-DA audio tracks (red book audio playback)
- For emulation: ISO image file or BIN/CUE with audio tracks

## Networking

***NOT IMPLEMENTED***
