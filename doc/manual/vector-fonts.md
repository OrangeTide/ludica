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
    lud_pen_t pen = { 10, 40 };
    lud_vfont_draw(font, &pen, 24.0f, 1, 1, 1, 1, "Hello, world!");
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

## Pen Tracking

The pen (`lud_pen_t`) is a two-float struct that tracks the current
drawing position. It is caller-owned and passed by pointer:

```c
lud_pen_t pen = { 10, 40 };
lud_vfont_draw(font_regular, &pen, 18, 1,1,1,1, "Hello, ");
lud_vfont_draw(font_bold,    &pen, 18, 1,1,1,1, "world!");
```

`lud_vfont_draw()` advances `pen->x` past the rendered text; `pen->y`
is unchanged. This makes inline style changes (bold, italic, color)
automatic — the second draw starts where the first left off.

For inline graphics, read and adjust the pen between draw calls:

```c
lud_vfont_draw(font, &pen, 18, 1,1,1,1, "Score: ");
draw_star_icon(pen.x, pen.y - 14);  /* inline sprite */
pen.x += 20;                         /* advance past icon */
lud_vfont_draw(font, &pen, 18, 1,1,1,1, "12345");
```

Different text regions use independent pens. There is no global cursor.

## Font Metrics

Font metrics are stored in both `.slugfont` and `.msdffont` files and
scaled to view units for a given font size:

```c
float asc = lud_vfont_ascender(font, 18);   /* baseline to top (positive) */
float desc = lud_vfont_descender(font, 18);  /* baseline to bottom (negative) */
float gap = lud_vfont_line_gap(font, 18);    /* extra inter-line spacing */
float lh = lud_vfont_line_height(font, 18);  /* asc - desc + gap */
```

Use `lud_vfont_newline()` to advance to the next line:

```c
lud_pen_t pen = { 10, 40 };
lud_vfont_draw(font, &pen, 18, 1,1,1,1, "First line");
float lh = lud_vfont_line_height(font, 18);
lud_vfont_newline(&pen, 10, lh);  /* pen.x = 10, pen.y += lh */
lud_vfont_draw(font, &pen, 18, 1,1,1,1, "Second line");
```

For mixed-size lines, collect the max line height across all fonts
used on that line before calling `lud_vfont_newline()`.

## Line Breaking

`lud_vfont_line_break()` finds the byte offset at which to break text
to fit within a given width:

```c
const char *text = "A long paragraph that needs word wrapping...";
float max_w = 400.0f;
float lh = lud_vfont_line_height(font, 18);
lud_pen_t pen = { 10, 40 };

while (*text) {
    int brk = lud_vfont_line_break(font, 18, text, max_w);
    if (brk <= 0) break;
    char line[256];
    memcpy(line, text, brk);
    line[brk] = '\0';
    lud_vfont_draw(font, &pen, 18, 1,1,1,1, line);
    lud_vfont_newline(&pen, 10, lh);
    text += brk;
    while (*text == ' ') text++;
}
```

Breaks at the last space that fits. Returns the full input length if
it fits entirely, or 0 if not even one word fits.

## Clipping

`lud_vfont_set_clip()` restricts rendering to a rectangle in view
coordinates. Text outside the rectangle is not drawn:

```c
lud_vfont_begin(0, 0, 800, 600);

/* Draw into a 300x200 text box at (50,50) */
lud_vfont_set_clip(50, 50, 300, 200);
lud_pen_t pen = { 50, 80 };
lud_vfont_draw(font, &pen, 18, 1,1,1,1, long_text);

lud_vfont_clear_clip();
/* Unclipped drawing continues normally */
lud_vfont_end();
```

Clipping is session state within the current `begin`/`end` block.
It uses GL scissor internally. Setting a new clip replaces the
previous one (no nesting).

## Architecture

The vfont system has three levels of state:

- **Pen** — per-call, caller-owned. Each draw call reads and advances
  the pen. Different text regions use independent pens.

- **Clip** — per-region, session state. Set with `lud_vfont_set_clip()`,
  cleared with `lud_vfont_clear_clip()`, automatically cleared by
  `lud_vfont_end()`.

- **Viewport** — per-frame. Set by `lud_vfont_begin()`, torn down by
  `lud_vfont_end()`.

`begin`/`end` is **not reentrant**. Both backends batch geometry and
flush on `end` or when the dispatch layer switches between backends.
Nesting `begin`/`end` would corrupt the batch. Clip changes trigger an
internal flush-restart cycle, which is why clip is session state rather
than a per-draw parameter.

Multiple fonts can be mixed freely within a single `begin`/`end` block.
The dispatch layer tracks which backend each font belongs to and
switches transparently. A backend switch flushes the current batch and
starts the new backend.

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
lud_pen_t pen = { 10, 50 };
lud_vfont_draw(font_title, &pen, 36, 1,1,1,1, "Chapter 1");
pen = (lud_pen_t){ 10, 100 };
lud_vfont_draw(font_ui, &pen, 18, 1,1,1,1, body_text);
pen = (lud_pen_t){ 10, 400 };
lud_vfont_draw(font_mono, &pen, 14, 0.7,0.7,0.7,1, code_text);
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

Mixing styles inline uses the pen naturally:

```c
lud_pen_t pen = { 10, 40 };
lud_vfont_draw(font_regular, &pen, 18, 1,1,1,1, "This is ");
lud_vfont_draw(font_bold,    &pen, 18, 1,1,1,1, "bold");
lud_vfont_draw(font_regular, &pen, 18, 1,1,1,1, " and ");
lud_vfont_draw(font_italic,  &pen, 18, 1,1,1,1, "italic");
lud_vfont_draw(font_regular, &pen, 18, 1,1,1,1, ".");
```

## Measuring Text

Use `lud_vfont_text_width()` for layout calculations:

```c
float w = lud_vfont_text_width(font, 18.0f, "Score: 12345");
/* Right-align: */
lud_pen_t pen = { screen_w - w - 10, y };
lud_vfont_draw(font, &pen, 18, 1,1,1,1, "Score: 12345");
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
