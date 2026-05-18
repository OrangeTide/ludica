# lilpc : the little 286 PC

Introduction ***TBD***

## CPU

***TODO: write the spec***

## Graphics / Display

CGA (Color Graphics Adapter) emulation with MC6845 CRTC.

### Implemented
- 80x25 and 40x25 text modes (16 colors, 8x8 CGA ROM font)
- 320x200 4-color graphics (mode 4/5)
- 640x200 2-color graphics (mode 6)
- 160x100 16-color pseudo-graphics (CRTC reprogrammed text mode)
- Color Select Register (3D9h): border/overscan, background, palette select
- Mode Control Register (3D8h): blink disable for 16 background colors
- MC6845 CRTC register programming: row/column counts, sync position,
  start address, cursor shape, interlaced mode
- CGA snow emulation (single-ported RAM contention artifact)
- Status register (3DAh): horizontal and vertical retrace bits
- VRAM at B8000h-BBFFFh (16KB)
- CRT post-processing shader

### CGA Compatibility Testing
Tested with CGACompatibilityTester (trixter@oldskool.org). Results
in `/cga-compat-test.txt`. All 46 tests ran to completion (4 batch,
42 interactive) with no crashes or sync losses. Key areas verified:
- Color Select Register: border cycling, background, foreground, palettes
- Text mode: 40-col, blink disable, cursor manipulation, snow, font
- MC6845 CRTC: retrace detection, 80x100 and 90x30 reprogramming,
  interlaced mode, sync position, start address register
- Monitor/capture calibration plates (22 total)
- VRAM speed benchmarks (4 tests)

### Not Yet Implemented
- Composite video artifact color rendering
- Plantronics ColorPlus extended modes (dual-plane 320x200x16)
- MCGA 256-color mode

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
