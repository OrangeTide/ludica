# Vector Fonts

Ludica provides resolution-independent vector font rendering through the
`ludica_vfont.h` API. The system has two backends — **Slug** (GLES3) and
**SDF** (GLES2) — selected automatically based on the active GLES version.
Games use one API regardless of backend.

## Quick Start

```c
#include <ludica_vfont.h>

static lud_vfont_t font;

void init(void) {
    font = lud_load_vfont("assets/fonts/dejavu-sans");
}

void frame(float dt) {
    lud_vfont_begin(0, 0, 800, 600);
    lud_vfont_draw(font, 10, 40, 24.0f, 1, 1, 1, 1, "Hello, world!");
    lud_vfont_end();
}

void cleanup(void) {
    lud_destroy_vfont(font);
}
```

`lud_load_vfont()` accepts a base path without extension. It tries the
preferred backend's extension first (`.slugfont` on GLES3, `.msdffont`
on GLES2), then falls back to the other. Explicit extensions also work:
`lud_load_vfont("assets/fonts/dejavu-sans.msdffont")`.

## Converting Fonts

Two command-line tools convert TTF/OTF files to ludica's binary formats:

```sh
# Slug (GLES3) — GPU Bezier curve evaluation
font2slug DejaVuSans.ttf -o assets/fonts/dejavu-sans.slugfont

# SDF (GLES2) — signed distance field atlas
font2msdf DejaVuSans.ttf -o assets/fonts/dejavu-sans.msdffont
```

For a game that ships on both GLES2 and GLES3, convert the same TTF
with both tools and place both files alongside each other:

    assets/fonts/dejavu-sans.slugfont
    assets/fonts/dejavu-sans.msdffont

The loader picks the right one at runtime.

### font2msdf Options

    font2msdf input.ttf [-o output.msdffont] [--range LO-HI]
                         [--size N] [--pxrange N] [-v]

| Option         | Default  | Description                           |
|----------------|----------|---------------------------------------|
| `--range LO-HI`| 32-255  | Codepoint range (Latin-1 by default)  |
| `--size N`     | 48       | SDF rasterize height in pixels        |
| `--pxrange N`  | 4        | SDF distance range in pixels          |
| `-v`           |          | Verbose output                        |

Larger `--size` produces a sharper atlas at the cost of file size.
A size of 48 with pxrange 4 is a good default for body text rendered
at 12-48px on screen.

### font2slug Options

    font2slug input.ttf [-o output.slugfont] [--range LO-HI]
                         [--bands N] [-v]

| Option         | Default  | Description                           |
|----------------|----------|---------------------------------------|
| `--range LO-HI`| 32-255  | Codepoint range                       |
| `--bands N`    | 8        | Band subdivision count (1-64)         |
| `-v`           |          | Verbose output                        |

## Using Multiple Fonts

Each `lud_load_vfont()` call returns an independent handle. Load as
many as you need:

```c
static lud_vfont_t font_ui;
static lud_vfont_t font_title;
static lud_vfont_t font_mono;

void init(void) {
    font_ui    = lud_load_vfont("assets/fonts/dejavu-sans");
    font_title = lud_load_vfont("assets/fonts/dejavu-serif");
    font_mono  = lud_load_vfont("assets/fonts/hack-regular");
}
```

All fonts work within the same begin/end block:

```c
lud_vfont_begin(0, 0, width, height);
lud_vfont_draw(font_title, 10, 50,  36, 1,1,1,1, "Chapter 1");
lud_vfont_draw(font_ui,    10, 100, 18, 1,1,1,1, body_text);
lud_vfont_draw(font_mono,  10, 400, 14, 0.7,0.7,0.7,1, code_text);
lud_vfont_end();
```

The dispatch layer handles backend switching transparently — you can
mix Slug and SDF fonts in the same frame if both file types are present.

## Adding Font Variants (Bold, Italic)

Ludica treats each variant as a separate font file. To support bold
and italic, convert each variant separately:

```sh
font2msdf DejaVuSans.ttf        -o assets/fonts/dejavu-sans.msdffont
font2msdf DejaVuSans-Bold.ttf   -o assets/fonts/dejavu-sans-bold.msdffont
font2msdf DejaVuSans-Oblique.ttf -o assets/fonts/dejavu-sans-italic.msdffont
```

Then load them as separate handles in your game:

```c
lud_vfont_t font_regular = lud_load_vfont("assets/fonts/dejavu-sans");
lud_vfont_t font_bold    = lud_load_vfont("assets/fonts/dejavu-sans-bold");
lud_vfont_t font_italic  = lud_load_vfont("assets/fonts/dejavu-sans-italic");
```

There is no built-in font style or family management — your game
selects which handle to pass to `lud_vfont_draw()`. This keeps the
API simple and gives full control over which variants to ship. A game
targeting small .wasm bundles can ship just Regular; a game with rich
dialog can add Bold and Italic.

## Measuring Text

Use `lud_vfont_text_width()` for layout calculations:

```c
float w = lud_vfont_text_width(font, 18.0f, "Score: 12345");
/* Right-align: */
lud_vfont_draw(font, screen_w - w - 10, y, 18, 1,1,1,1, "Score: 12345");
```

## Backend Override

The `LUD_VFONT_BACKEND` environment variable forces a specific backend,
useful for testing:

```sh
LUD_VFONT_BACKEND=msdf ./mygame   # force SDF even on GLES3
LUD_VFONT_BACKEND=slug ./mygame   # force Slug even if SDF files exist
```

The corresponding font file must exist or loading will fail.

## Why Pre-generated Font Files (Not Runtime TTF)

Ludica pre-converts fonts to `.msdffont` / `.slugfont` rather than
loading TTF files and generating atlases at init. The reasons:

- **Load speed.** A pre-built font file is fread + glTexImage2D —
  sub-millisecond. Single-channel SDF generation via stb_truetype for
  ~200 Latin glyphs at 48px takes 20-50ms on desktop and 200-500ms on
  Pi Zero. Proper multi-channel MSDF (edge coloring, per-channel
  distance computation) is 3-5x slower still — that's why msdfgen and
  msdf-atlas-gen treat it as an offline step.

- **No runtime rasterization dependency.** The game binary doesn't need
  stb_truetype or msdfgen linked in. Less code, fewer moving parts.

- **Deterministic output.** The same atlas is loaded every time — no
  platform-dependent rasterization differences.

- **Embedded targets.** On Pi Zero the CPU cost of SDF generation at
  init is noticeable; MSDF generation is prohibitive.

The tradeoff is a fixed glyph set baked at build time plus an extra
tooling step. If a game ever needs dynamic glyph coverage (CJK, user
input in arbitrary scripts), a hybrid approach works: pre-baked base
atlas for the common set, lazy runtime generation for overflow glyphs.
That is not implemented today because Latin-1 covers current needs.

## Recommended Fonts

Ludica bundles **DejaVu Sans LGC Regular** as its default GUI font.
For additional fonts, any TTF/OTF file works with the converter tools.
Some good free options:

| Font             | Style      | License        | Notes                    |
|------------------|------------|----------------|--------------------------|
| DejaVu Sans LGC  | Sans-serif | Bitstream Vera | Default, good Latin-1    |
| DejaVu Serif LGC | Serif      | Bitstream Vera | For body/dialog text     |
| Liberation Sans  | Sans-serif | SIL OFL        | Arial-compatible metrics |
| Hack             | Monospace  | MIT            | Good for code display    |

For monospace terminal output and box-drawing characters, consider
loading a bitmap font (unscii .HEX format) through the bitmap font
path instead of the SDF pipeline.
