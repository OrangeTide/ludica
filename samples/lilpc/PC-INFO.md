# PC Hardware & Software Reference for lilpc

## BIOS

### pcxtbios (Super PC/Turbo XT BIOS)
- **Source:** https://github.com/virtualxt/pcxtbios (vendored in `vendor/pcxtbios/`)
- **License:** Public domain
- **Type:** 8KB ROM, maps at FE000-FFFFF
- **Target:** 8088/8086/V20 XT-class machines, 83-key XT keyboard
- **Features:** Floppy boot, hard disk via expansion ROM, turbo toggle
- **Build:** `make_linux.sh` uses Open Watcom WASM

### GLaBIOS
- **Source:** https://github.com/640-KB/GLaBIOS
- **Website:** https://glabios.org/
- **License:** GPL v3
- **Target:** XT/5160 class PCs (8088-286)
- **Companion ROMs:** GLaDISK (HD floppy, XT/AT/286/386+), GLaTICK (RTC)
- **Releases:** https://github.com/640-KB/GLaBIOS/releases

### BIOS ROM Mapping
| ROM Size | Base Address | Range |
|----------|-------------|-------|
| 8 KB | FE000h | FE000-FFFFF |
| 32 KB | F8000h | F8000-FFFFF |
| 64 KB | F0000h | F0000-FFFFF |

CPU starts execution at FFFF:0000 (physical F_FFF0h) after reset.

## Video Adapters

### CGA (Color Graphics Adapter)
- **Reference:** https://www.seasip.info/VintagePC/cga.html
- **VRAM:** 16KB at B8000h-BBFFFh
- **Font ROM:** 9264 character ROM (non-standard pinout, not 27xx)
  - Contains 3 fonts: one 14-row (matching MDA), two 8x8 (thick/thin uprights)
  - Thin font selectable via P3 header jumper
  - Character set is CP437
- **Clock:** Derived from ISA bus 14.318 MHz (4x NTSC subcarrier)
- **Text modes:** 40x25 and 80x25, 8x8 character cells, 16 colors
- **Graphics modes:** 320x200x4, 640x200x2
- **Snow:** Single-ported RAM causes snow during active display. Check 03DAh bit 0.
- **Undocumented modes:**
  - 160x200 composite color
  - 160x100 "text" mode (80x25 reprogrammed to 2-pixel char height)

#### CGA I/O Ports
| Port | R/W | Description |
|------|-----|-------------|
| 3D4h | W | CRTC index register |
| 3D5h | R/W | CRTC data register |
| 3D8h | W | Mode control register |
| 3D9h | W | Color select register |
| 3DAh | R | Status register |
| 3DBh | R/W | Clear light pen strobe |
| 3DCh | R/W | Set light pen strobe |

#### CGA Mode Control (3D8h)
| Bit | Function |
|-----|----------|
| 0 | 80x25 text (set) vs 40x25 (clear) |
| 1 | Graphics mode (set) vs text (clear) |
| 2 | B&W mode (set) vs color (clear) |
| 3 | Video enable |
| 4 | 640x200 graphics (set) vs 320x200 (clear) |
| 5 | Blink enable (set) vs background intensity (clear) |

#### CGA Status Register (3DAh)
| Bit | Function |
|-----|----------|
| 0 | Display enable (H/V retrace active, safe to access VRAM) |
| 1 | Light pen trigger |
| 2 | Light pen switch |
| 3 | Vertical retrace active (safe for ~1.25 ms) |

#### CGA Color Select (3D9h)
- Text modes: bits 0-3 = border color, bit 4 = background intensity
- 320x200: bits 0-3 = background, bit 4 = alt intensity, bit 5 = palette select
  - Palette 0: green, red, brown
  - Palette 1: cyan, magenta, white
- 640x200: bits 0-3 = foreground color

#### CGA Video Modes
| Mode | Type | Cols | Rows | Colors | Resolution |
|------|------|------|------|--------|------------|
| 00h | Text | 40 | 25 | 16 | 320x200 |
| 01h | Text | 40 | 25 | 16 | 320x200 |
| 02h | Text | 80 | 25 | 16 | 640x200 |
| 03h | Text | 80 | 25 | 16 | 640x200 |
| 04h | Gfx | 320 | 200 | 4 | 320x200 |
| 05h | Gfx | 320 | 200 | 4 | 320x200 |
| 06h | Gfx | 640 | 200 | 2 | 640x200 |

#### CGA 16-Color RGBI Palette
| Index | Color | RGB |
|-------|-------|-----|
| 0 | Black | 000000 |
| 1 | Blue | 0000AA |
| 2 | Green | 00AA00 |
| 3 | Cyan | 00AAAA |
| 4 | Red | AA0000 |
| 5 | Magenta | AA00AA |
| 6 | Brown | AA5500 |
| 7 | Light Gray | AAAAAA |
| 8 | Dark Gray | 555555 |
| 9 | Light Blue | 5555FF |
| 10 | Light Green | 55FF55 |
| 11 | Light Cyan | 55FFFF |
| 12 | Light Red | FF5555 |
| 13 | Light Magenta | FF55FF |
| 14 | Yellow | FFFF55 |
| 15 | White | FFFFFF |

### MDA (Monochrome Display Adapter)
- **Reference:** https://www.seasip.info/VintagePC/mda.html
- **VRAM:** 4KB at B0000h (repeats through B0000-B7FFF)
- **Font ROM:** 9264 character ROM (same as CGA, shared design)
  - 14-row font used for display (9x14 pixels, 9th column duplicates 8th for C0-DF)
  - Two 8x8 fonts also present but unused by MDA
  - Character set: CP437
- **Text:** 80x25 only, 9x14 character cells = 720x350 display
- **No graphics mode**

#### MDA I/O Ports
| Port | R/W | Description |
|------|-----|-------------|
| 3B4h | W | CRTC index register |
| 3B5h | R/W | CRTC data register |
| 3B8h | W | Mode control (bit 5=blink, bit 3=video enable) |
| 3BAh | R | Status (bit 3=video, bit 0=horizontal retrace) |

#### MDA Attributes
- Bits 0-2: foreground (underline if 001)
- Bit 3: high intensity
- Bits 4-6: background
- Bit 7: blink (if enabled)
- Special combinations: 00h,08h,80h,88h = black; 70h,F0h = inverse video

### Hercules Graphics Card
- **VRAM:** 64KB at B0000h-BFFFFh (2 graphics pages)
- **Text:** 80x25, 9x14 characters = 720x350
- **Graphics:** 720x348 monochrome, 2 pages

#### Hercules I/O Ports
| Port | R/W | Description |
|------|-----|-------------|
| 3B4h | W | CRTC index (also 3B0h, 3B2h) |
| 3B5h | R/W | CRTC data (also 3B1h, 3B3h) |
| 3B8h | W | Mode control |
| 3BAh | R | Status register |
| 3BFh | W | Configuration register |

#### Hercules Mode Control (3B8h)
| Bit | Function |
|-----|----------|
| 1 | Graphics mode (set) vs text (clear) |
| 3 | Video enable |
| 5 | Blink enable |
| 7 | Graphics page 1 (set) vs page 0 (clear) |

#### Hercules Configuration (3BFh)
| Bit | Function |
|-----|----------|
| 0 | Allow setting graphics mode bit in 3B8h |
| 1 | Allow setting graphics page bit in 3B8h |

#### Hercules Status (3BAh)
| Bit | Function |
|-----|----------|
| 0 | Horizontal sync |
| 3 | Video signal |
| 7 | Vertical sync |

#### Hercules Detection
Read 3BAh repeatedly; if bit 7 changes, it's Hercules (MDA never changes bit 7).
Then check bits 4-6: 50h=InColor, 10h=Graphics Plus, other=standard HGC.

### Hercules Graphics Card Plus (HGC+)
- **Reference:** https://www.seasip.info/VintagePC/hercplus.html
- **Font ROM:** AMI C15994 FONTROM, 14-pixel font, also has two 8-pixel fonts
- **RAMfont:** Distinguishing feature from standard Hercules
  - Bitmaps at B4000h, 16 bytes/character
  - 4K RAMfont: single font in RAM
  - 48K RAMfont: up to 12 fonts (B4000-B7FFF), low 4 attribute bits select font
- **90-column mode:** 8-pixel wide characters
- **Extra CRTC registers:**
  - R14h (xMode): font source, column mode, RAMfont size
  - R15h (Underline): underline position for 48k mode
  - R16h (Strikethrough): strikethrough position

## MC6845 CRTC Registers (shared by CGA, MDA, Hercules)

| Reg | Name | Description |
|-----|------|-------------|
| 00h | Horiz Total | Chars per scanline including retrace (-1) |
| 01h | Horiz Displayed | Chars displayed per scanline (-1) |
| 02h | Horiz Sync Pos | Char position where hsync starts |
| 03h | Horiz Sync Width | Char clocks during hsync |
| 04h | Vert Total | Character rows per frame |
| 05h | Vert Adjust | Scanlines added to vertical total |
| 06h | Vert Displayed | Character rows displayed |
| 07h | Vert Sync Pos | Row where vsync starts |
| 08h | Interlace Mode | |
| 09h | Max Scan Line | Scanlines per character row (-1) |
| 0Ah | Cursor Start | Scanline where cursor starts (bits 5-6: blink mode) |
| 0Bh | Cursor End | Scanline where cursor ends |
| 0Ch | Start Addr High | Upper 6 bits of display start address |
| 0Dh | Start Addr Low | Lower 8 bits of display start address |
| 0Eh | Cursor Loc High | Upper 6 bits of cursor address |
| 0Fh | Cursor Loc Low | Lower 8 bits of cursor address |
| 10h | Light Pen High | Upper 6 bits of light pen address |
| 11h | Light Pen Low | Lower 8 bits of light pen address |

### CRTC Default Values
**CGA 80x25:** 71,80,82,10, 31,6,25,28, 2,7, 6,7, 0,0
**CGA 40x25:** 38,40,42,10, 31,6,25,28, 2,7, 6,7, 0,0
**MDA 80x25:** 97,80,82,15, 25,6,25,25, 2,13, 11,12, 0,0
**Hercules Gfx:** 53,45,46,7, 91,2,87,87, 2,3, 0,0, 0,0

## XT Keyboard

### References
- Layout and scan codes: https://www.seasip.info/VintagePC/ibm_1501105.html
- 83-key keyboard (IBM Model F), XT protocol
- Internal 12x8 matrix (supports up to 96 keys, 83 used)
- Uses Scan Code Set 1 (make code on press, make+80h on release)

### XT Scan Code Set 1

| Code | Key | Code | Key | Code | Key |
|------|-----|------|-----|------|-----|
| 01 | Esc | 02 | 1 ! | 03 | 2 @ |
| 04 | 3 # | 05 | 4 $ | 06 | 5 % |
| 07 | 6 ^ | 08 | 7 & | 09 | 8 * |
| 0A | 9 ( | 0B | 0 ) | 0C | - _ |
| 0D | = + | 0E | Backspace | 0F | Tab |
| 10 | Q | 11 | W | 12 | E |
| 13 | R | 14 | T | 15 | Y |
| 16 | U | 17 | I | 18 | O |
| 19 | P | 1A | [ { | 1B | ] } |
| 1C | Enter | 1D | Ctrl | 1E | A |
| 1F | S | 20 | D | 21 | F |
| 22 | G | 23 | H | 24 | J |
| 25 | K | 26 | L | 27 | ; : |
| 28 | ' " | 29 | ` ~ | 2A | LShift |
| 2B | \ \| | 2C | Z | 2D | X |
| 2E | C | 2F | V | 30 | B |
| 31 | N | 32 | M | 33 | , < |
| 34 | . > | 35 | / ? | 36 | RShift |
| 37 | KP * (PrtSc) | 38 | Alt | 39 | Space |
| 3A | CapsLock | 3B | F1 | 3C | F2 |
| 3D | F3 | 3E | F4 | 3F | F5 |
| 40 | F6 | 41 | F7 | 42 | F8 |
| 43 | F9 | 44 | F10 | 45 | NumLock |
| 46 | ScrollLock | 47 | KP 7 Home | 48 | KP 8 Up |
| 49 | KP 9 PgUp | 4A | KP - | 4B | KP 4 Left |
| 4C | KP 5 | 4D | KP 6 Right | 4E | KP + |
| 4F | KP 1 End | 50 | KP 2 Down | 51 | KP 3 PgDn |
| 52 | KP 0 Ins | 53 | KP . Del | | |

Break code = make code OR 80h (e.g., Esc release = 81h).

### XT Keyboard Protocol
- Active-low clock and data lines
- Keyboard sends 1 start bit + 8 data bits (LSB first)
- XT has no bidirectional communication (unlike AT keyboard)
- Port 60h: read scan code data
- Port 61h bit 7: toggle to acknowledge keystroke (clear keyboard)
- IRQ1 generated on each scan code

## Chipset

### 8259A PIC (Programmable Interrupt Controller)
- XT uses single PIC, 8 IRQ lines (IRQ0-7)
- I/O ports: 20h-21h
- Vector base typically 08h (IRQ0 = INT 08h)

| IRQ | Device |
|-----|--------|
| 0 | Timer (PIT ch0) |
| 1 | Keyboard |
| 2 | (cascade on AT) / EGA retrace |
| 3 | COM2 serial |
| 4 | COM1 serial |
| 5 | Hard disk (XT) / LPT2 |
| 6 | Floppy disk |
| 7 | LPT1 |

### 8254 PIT (Programmable Interval Timer)
- I/O ports: 40h-43h
- Input clock: 1.193182 MHz (14.31818 MHz / 12)
- Channel 0: system timer → IRQ0 (default: 65536 = ~18.2 Hz)
- Channel 1: DRAM refresh (not critical for emulation)
- Channel 2: PC speaker

### 8237 DMA Controller
- XT uses single 8237 (4 channels, 8-bit transfers)
- I/O ports: 00h-0Fh (controller), 81h-83h (page registers)
- Channel 0: DRAM refresh
- Channel 2: floppy disk
- Channel 3: hard disk (XT)

### DMA Page Registers
| Port | Channel |
|------|---------|
| 81h | Channel 2 (floppy) |
| 82h | Channel 3 (hard disk) |
| 83h | Channel 1 |
| 87h | Channel 0 (refresh) |

## Serial Port (8250 UART)
- COM1: base 3F8h, IRQ4
- COM2: base 2F8h, IRQ3
- Registers at base+0 through base+7
- RTS/CTS hardware flow control via MCR/MSR

## Parallel Port (LPT)
- LPT1: base 378h, IRQ7
- LPT2: base 278h, IRQ5

### Disney Sound Source / Convox SpeechThing
- Connects to parallel port
- 8-bit unsigned PCM, ~7 kHz
- Data written to LPT data register, BUSY line used for flow control
- Simple FIFO - write sample, wait for BUSY to clear

## 80286 CPU

### LOADALL (Undocumented)
- **Reference:** https://www.rcollins.org/articles/loadall/loadall.html
- Opcode: 0Fh 05h
- Loads entire CPU state from memory at 800h-866h
- Used by EMM286 and other memory managers
- Allows entering protected mode segments while in real mode

### Reset Vector
- CPU starts at FFFF:0000 (physical FFFF0h)
- Typically a far JMP to BIOS POST code

## BIOS Data Area (0040:0000 = physical 400h)

| Offset | Size | Description |
|--------|------|-------------|
| 0000h | 4W | COM port base addresses (COM1-4) |
| 0008h | 3W | LPT port base addresses (LPT1-3) |
| 0010h | W | Equipment word |
| 0013h | W | Memory size in KB |
| 0017h | B | Keyboard shift flags 1 |
| 0018h | B | Keyboard shift flags 2 |
| 001Ah | W | Keyboard buffer head pointer |
| 001Ch | W | Keyboard buffer tail pointer |
| 001Eh | 32B | Keyboard buffer (16 words) |
| 0049h | B | Current video mode |
| 004Ah | W | Columns per screen |
| 004Eh | W | Video page offset |
| 0050h | 16B | Cursor positions (8 pages) |
| 0060h | W | Cursor shape (start/end scan lines) |
| 0062h | B | Current display page |
| 0063h | W | CRTC base port (3B4h or 3D4h) |
| 006Ch | DW | Timer tick count (incremented ~18.2x/sec) |
| 0072h | W | Reset flag (1234h = warm boot) |
| 0080h | W | Keyboard buffer start offset |
| 0082h | W | Keyboard buffer end offset |
