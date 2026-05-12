# ATI Graphics Solution — Register Map & Emulation Notes

Research for lilpc 286 XT emulator. The ATI Graphics Solution (Rev 2/3,
CW16800-A chip, 1986) and Graphics Solution Plus (CW16800-B, 1987) are
functionally identical. The card is a CGA + MDA + Hercules + Plantronics
ColorPlus superset with ATI-specific extensions. 64KB DRAM, UM6845E CRTC.

The card can operate on RGB color, TTL monochrome, or composite monitors.
DIP switches select default mode and monitor type. The MultiSwitch
utility (MS.COM) allows software mode switching without touching hardware.

No existing emulator (86Box, PCem, DOSBox-X) emulates the ATI Graphics
Solution specifically, though 86Box supports Plantronics ColorPlus as a
CGA extension.

## I/O Ports — Monochrome Side (0x3Bx)

| Port   | R/W | Standard | ATI Extension |
|--------|-----|----------|---------------|
| 0x3B4  | W   | 6845 CRTC index | — |
| 0x3B5  | R/W | 6845 CRTC data | — |
| 0x3B8  | W   | HGC mode control (see below) | — |
| 0x3BA  | R   | Status: bit 0=hsync, bit 3=video, bit 7=vsync | — |
| 0x3BA  | W   | — | Extended mode (see 0x3DF, same bits) |
| 0x3BF  | W   | HGC control: bit 0=enable graphics, bit 1=enable page bit | — |

### Port 0x3B8 — Hercules Mode Control (write)

| Bit | Function |
|-----|----------|
| 1   | 0=text, 1=Hercules graphics |
| 3   | 0=disable video, 1=enable |
| 5   | 0=disable blink, 1=enable blink |
| 7   | 0=graphics page 0, 1=page 1 |

## I/O Ports — Color Side (0x3Dx)

| Port   | R/W | Standard | ATI Extension |
|--------|-----|----------|---------------|
| 0x3D4  | W   | 6845 CRTC index | — |
| 0x3D5  | R/W | 6845 CRTC data | — |
| 0x3D8  | W   | CGA mode select (see below) | — |
| 0x3D9  | W   | CGA color/palette (see below) | — |
| 0x3DA  | R   | Status (see below) | — |
| 0x3DB  | W   | Clear light pen strobe | — |
| 0x3DC  | W   | Set light pen strobe | — |
| 0x3DD  | W   | — | Plantronics/ATI mode (see below) |
| 0x3DF  | W   | — | ATI extended mode (see below) |

### Port 0x3D8 — CGA Mode Select (write)

| Bit | Function |
|-----|----------|
| 0   | 0=40 columns, 1=80 columns |
| 1   | 0=text, 1=graphics |
| 2   | 0=color, 1=B&W |
| 3   | 0=disable video, 1=enable |
| 4   | 0=non-640x200, 1=640x200 B&W |
| 5   | 0=background intensity, 1=enable blink |

### Port 0x3D9 — CGA Color/Palette (write)

| Bit | Function |
|-----|----------|
| 0-3 | Color select (Blue, Green, Red, Intensity) |
| 4   | Foreground intensity in 4-color graphics modes |

### Port 0x3DA — Status Register (read)

| Bit | Function |
|-----|----------|
| 0   | Display enable |
| 1   | Light pen trigger set |
| 2   | Light pen switch |
| 3   | Vertical sync |

### Port 0x3DD — Plantronics/ATI Mode Control (write)

| Bit | Function | Origin |
|-----|----------|--------|
| 0-3 | Unused | — |
| 4   | 320x200 16 colors | Plantronics |
| 5   | 640x200 4 colors | Plantronics |
| 6   | Plane swap (0=normal, 1=swapped) | Plantronics |
| 7   | 640x200 16 colors | **ATI only** |

When both bits 4 and 5 are set, 320x200 takes precedence.

### Port 0x3DF / 0x3BA(write) — ATI Extended Mode (write)

Same register accessible from both mono and color port ranges.

| Bit | Function |
|-----|----------|
| 0   | Reserved |
| 1-2 | Unused |
| 3   | 132-column text for monochrome |
| 4   | 132-column text for color (bit 0 of 0x3D8 must be 1) |
| 5   | Set emulation mode (in lieu of DIP switch) |
| 6   | Set mono mode (in lieu of DIP switch) |
| 7   | Set color mode (in lieu of DIP switch) |

## Memory Map

| Mode | Address Range | Size |
|------|--------------|------|
| CGA 40x25 text | 0xB8000-0xBBFFF | 16KB, 8 pages |
| CGA 80x25 text | 0xB8000-0xBBFFF | 16KB, 4 pages |
| CGA 320x200 4-color | 0xB8000-0xBBFFF | 16KB, 2-bank interlace |
| CGA 640x200 B&W | 0xB8000-0xBBFFF | 16KB, 2-bank interlace |
| Plantronics 320x200x16 | 0xB8000-0xBFFFF | 32KB, dual-plane |
| Plantronics 640x200x4 | 0xB8000-0xBFFFF | 32KB, dual-plane |
| ATI 640x200x16 | 0xB0000-0xBFFFF | 64KB, dual-plane, 4-bank |
| MDA 80x25 text | 0xB0000-0xB3FFF | 16KB, 4 pages |
| Hercules graphics (1pg) | 0xB0000-0xB7FFF | 32KB |
| Hercules graphics (2pg) | 0xB0000-0xBFFFF | 64KB |
| 132x25 color text | 0xB8000-0xB9FFF | 8KB |
| 132x25 mono text | 0xB0000-0xB1FFF | 8KB |
| 132x44 mono text | 0xB0000-0xB3FFF | 16KB |

### Plantronics Dual-Plane Layout

Plane 0 at 0xB8000-0xBBFFF (16KB), Plane 1 at 0xBC000-0xBFFFF (16KB).
Each plane uses standard CGA 2-bank interlace:
- Even scan lines (0,2,4,...,198) at offset +0x0000
- Odd scan lines (1,3,5,...,199) at offset +0x2000

Bit 6 of port 0x3DD swaps plane mapping so Plane 1 appears at 0xB8000.

Pixel encoding for 320x200x16:
- Plane 0 byte: `c1 c0 c1 c0 c1 c0 c1 c0` (4 pixels, 2 bits each)
- Plane 1 byte: `c3 c2 c3 c2 c3 c2 c3 c2`
- Color = c3:c2:c1:c0 = standard CGA IRGB palette

Pixel encoding for 640x200x4:
- Plane 0 byte: 8 pixels, 1 bit each (R)
- Plane 1 byte: 8 pixels, 1 bit each (G)
- Color = G:R (2-bit)

### ATI 640x200x16 Layout

Plane 0 at 0xB0000-0xB7FFF (32KB), Plane 1 at 0xB8000-0xBFFFF (32KB).
Each plane uses 4-bank interlace (8KB per bank):
- Bank 0 (+0x0000): scan lines 0, 4, 8, ..., 196
- Bank 1 (+0x2000): scan lines 1, 5, 9, ..., 197
- Bank 2 (+0x4000): scan lines 2, 6, 10, ..., 198
- Bank 3 (+0x6000): scan lines 3, 7, 11, ..., 199

Pixel format is the same as Plantronics 320x200x16.

## CRTC Register Values (R0-R13, hex)

| Mode | R0 | R1 | R2 | R3 | R4 | R5 | R6 | R7 | R8 | R9 | R10 | R11 | R12 | R13 |
|------|----|----|----|----|----|----|----|----|----|----|-----|-----|-----|-----|
| 40x25 color text | 38 | 28 | 2D | 0A | 1F | 06 | 19 | 1C | 02 | 07 | 06 | 07 | 00 | 00 |
| 80x25 color text | 71 | 50 | 5A | 0A | 1F | 06 | 19 | 1C | 02 | 07 | 06 | 07 | 00 | 00 |
| 320x200/640x200x4 | 38 | 28 | 2D | 0A | 7F | 06 | 64 | 70 | 02 | 01 | 06 | 07 | 00 | 00 |
| 640x200x16 color | 70 | 50 | 58 | 0A | 40 | 06 | 32 | 38 | 02 | 03 | 06 | 07 | 00 | 00 |
| 640x200x16 emulation | 61 | 50 | 52 | 08 | 32 | 06 | 32 | 32 | 02 | 07 | 06 | 07 | 00 | 00 |
| 80x25 mono text | 61 | 50 | 52 | 0F | 19 | 06 | 19 | 19 | 02 | 0D | 0B | 0C | 00 | 00 |
| Hercules graphics | 36 | 2D | 2F | 07 | 5B | 00 | 57 | 57 | 02 | 03 | 00 | 00 | 00 | 00 |
| 132x44 mono | 9F | 84 | 89 | 0F | 2D | 02 | 2C | 2C | 02 | 07 | 06 | 07 | 00 | 00 |

## MultiSwitch Modes

| Key | Keyword | Description |
|-----|---------|-------------|
| A   | MT      | Monochrome Text 80x25 (pure MDA, no graphics) |
| B   | MG1     | Monochrome Graphics 1 page (Hercules HALF) |
| C   | MG2     | Monochrome Graphics 2 pages (Hercules FULL) |
| D   | C80     | Color Text 80x25 (CGA + Plantronics + ATI 640x200x16 auto-sense) |
| E   | L25     | 132x25 Columns |
| F   | L44     | 132x44 Columns (mono only) |

In C80 mode, the card auto-detects whether software is using standard
CGA, Plantronics ColorPlus, or ATI 640x200x16 modes.

## Emulation Strategy

Layered approach, each level adds to the previous:

1. **Plantronics ColorPlus only** — add port 0x3DD (bits 4-6), extend
   video RAM to 32KB, render dual-plane modes. Small effort, broadens
   game compatibility slightly.

2. **Full ATI modes** — add 0x3DD bit 7, port 0x3DF, 0x3BA write,
   extend to 64KB. Adds 640x200x16 and 132-column text. The 4-bank
   interlace and dual port ranges add moderate complexity.

3. **Unified CGA/MDA/HGC card** — support MDA and Hercules modes on
   the same card with software mode switching. Most complex since the
   card operates across both 0x3Bx and 0x3Dx port ranges simultaneously.

## Sync Frequencies

| Monitor | Horizontal | Vertical |
|---------|-----------|----------|
| RGB color | 15.75 kHz | 60 Hz |
| Monochrome | 18.432 kHz | 50 Hz |

## DB9 Connector Pinout

Color/Graphics mode uses standard CGA pinout.
Monochrome/Emulation mode uses standard MDA pinout.
Same physical connector, different signal assignments per mode.

## Included Software

| File | Purpose |
|------|---------|
| MS.COM | MultiSwitch mode switching utility |
| ALLTEST.COM | Diagnostics |
| IOX.ASM | 640x200x16 example (assembler) |
| GSDEMO1.C | 640x200x16 example (C) |
| GSDEMO1.COM | Compiled demo |
| GSACAD.EXE | AutoCAD 640x200 16-color driver |
| PROGINFO.DOC | Advanced programming information |

## References

- [ATI Graphics Solution User Manual (PDF)](ati-graphics-solution-user-manual.pdf) — local copy
- [Commodore PC 20 Manual — ManualsLib](https://www.manualslib.com/manual/1305868/Commodore-Pc-20.html?page=51) — pages 51-64 cover the adapter
- [Seasip — Plantronics ColorPlus Notes](https://www.seasip.info/VintagePC/plantronics.html)
- [VOGONS — ATI Graphics Solution](https://www.vogons.org/viewtopic.php?t=67224)
- [VOGONS — ATI Small Wonder technical info](https://www.vogons.org/viewtopic.php?t=83860)
- [VOGONS — History of ATi Graphics cards Vol. 1](https://www.vogons.org/viewtopic.php?t=77481)
- [VGA Museum — CW16800-A](https://vgamuseum.info/index.php/cpu/item/979-ati-cw16800-a)
- [VGA Museum — CW16800-B](https://vgamuseum.info/index.php/cpu/item/65-ati-cw16800-b-graphics-solution-plus)
- [TheRetroWeb — Rev 2 and Rev 3](https://theretroweb.com/expansioncards/s/ati-graphics-solution-rev-2-and-rev-3)
- [TheRetroWeb — Plus](https://theretroweb.com/expansioncards/s/ati-graphics-solution-plus)
- [CGA on MDA Monitors (blog)](https://swarmik.tumblr.com/post/178909439269/full-cga-graphics-on-mda-monitors-a-few-weeks-ago)
