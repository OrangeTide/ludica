# Ludica Automation & MCP Plan

Automation and introspection layer for ludica, exposing game state and
input control via a TCP text protocol and an MCP server.

## Phase 1 — CLI argument parsing

Add optional `argc`/`argv` fields to `lud_desc_t`. Parse `--flags` in
`lud_run()` before calling `init()`. Initially supports:

- `--auto-port PORT` — enable TCP automation listener
- `--auto-file FILE` — replay commands from a file
- `--capture-dir DIR` — output directory for captures
- `--width W --height H` — force window size
- `--paused` — start with game loop frozen, waiting for first STEP
- `--fixed-dt` — use constant dt (1/60) instead of wall clock

Files:
- `src/ludica/args.c`
- Changes to `src/ludica/app.c` and `ludica_internal.h`
- Add fields to `lud_desc_t` in `src/ludica/include/ludica.h`

## Phase 2 — TCP automation core

Non-blocking TCP server that accepts one connection at a time. Hooks
into the main loop via `lud__auto_poll()` (after platform event poll)
and `lud__auto_post_frame()` (after swap). No-ops when disabled.

Text protocol, one command per line (`\n` terminated), one response per
command:

### Input injection (low-level)

```
KEYDOWN <keyname>               -> OK
KEYUP <keyname>                 -> OK
MOUSEMOVE <x> <y>              -> OK
MOUSEDOWN <button> <x> <y>     -> OK
MOUSEUP <button> <x> <y>       -> OK
SCROLL <dx> <dy>               -> OK
```

Key names reuse `lud_key_from_name()`. Errors return `ERR <message>`.
File replay uses the same parser (read lines from fd instead of socket).

### Action system (high-level, preferred for agents)

Games register named actions via the existing `lud_make_action()` /
`lud_bind_key()` system. Automation exposes these semantically:

```
LISTACTIONS                     -> OK move_left=Left move_right=Right hard_drop=Space ...
ACTION <name>                   -> OK    (press and auto-release next frame)
ACTION <name> HOLD              -> OK    (press and hold)
ACTION <name> RELEASE           -> OK    (release held action)
```

Agents discover available actions via LISTACTIONS, then operate at the
game's semantic level without knowing key bindings. This is the
preferred input method for AI agents.

### Frame control

```
STEP [N]                        -> OK <frame_number>  (advance N frames, default 1)
FRAMEDELAY <count>              -> OK                  (pause command reading for N frames)
WAITEVENT <type>                -> OK <detail>         (block until matching real event)
WAITEVENT FRAME                 -> OK <number>         (block until next frame completes)
SEED <n>                        -> OK                  (set RNG seed)
```

STEP is the primary mode for agents: the game stays frozen between
steps, giving the agent full control over pacing. FRAMEDELAY is for
scripted replay files where commands run alongside real-time playback.

### Capture

```
CAPSCREEN [filename]            -> OK <path>
CAPRECT <x> <y> <w> <h> [file] -> OK <path>
READPIXEL <x> <y>              -> OK <r> <g> <b>
CAPAUDIO START                  -> OK
CAPAUDIO STOP [filename]        -> OK <path>
```

### State queries

```
QUERY FRAME                     -> OK <number>
QUERY SIZE                      -> OK <w> <h>
QUERY FPS                       -> OK <fps>
QUERY VAR <name>                -> OK <value>
LISTVAR                         -> OK name1 name2 ...
LISTACTIONS                     -> OK action1=Key1 action2=Key2 ...
QUIT                            -> OK
```

Apps register queryable state via:

```c
lud_auto_register_int("score", &score);
lud_auto_register_int("level", &level);
lud_auto_register_str("scene", scene_name_ptr);
```

### Response format

All responses are a single line: `OK [data]` or `ERR <message>`.
When `--base64` is used with capture commands, image data is returned
inline: `OK base64:<data>`.

Files:
- `src/ludica/automation.c`
- `src/ludica/include/ludica_auto.h`
- Hook calls added to `src/ludica/app.c` `frame_tick()`

## Phase 3 — Screen capture

`CAPSCREEN` triggers `glReadPixels()` after buffer swap, flips rows,
writes PNG via `stb_image_write.h`. Auto-named `frame_%06d.png` using
internal frame counter, or caller-supplied name. Output goes to
`--capture-dir` or current directory.

`CAPRECT` captures a sub-rectangle of the framebuffer. `READPIXEL`
returns the RGB value at a single coordinate.

Support `--base64` flag on capture commands to return image data inline
in the response instead of writing to disk.

Also support `SIGUSR1` on Linux to trigger a capture without TCP.

Files:
- Screen capture logic in `src/ludica/automation.c`
- `src/thirdparty/stb_image_write.h`
- Signal handler in `src/ludica/automation.c` (Linux only, `#ifdef`)

## Phase 4 — Audio mixer

Adapt triton's 16-channel PCM mixer into ludica. Strip the register-file
hardware emulation layer. Replace with a direct C struct API:

```c
lud_audio_init()
lud_audio_play(ch, desc)
lud_audio_stop(ch)
lud_audio_mix(out, nframes)     /* called from audio device callback */
```

16 channels, PCM16/PCM8/IMA ADPCM formats, per-channel stereo volume,
8.8 fixed-point pitch, looping. Master volume. `lud_audio_mix()` is the
single call site for device backends (miniaudio, etc.).

Source: adapted from `/home/jon/research/triton-audio/demo/triton_audio.c`
(MIT-0 / Public Domain).

Files:
- `src/ludica/audio.c`
- `src/ludica/include/ludica_audio.h`

## Phase 5 — Audio capture

Tee the output of `lud_audio_mix()` into a growing sample buffer when
capture is active. `CAPAUDIO STOP` writes a WAV file (44100 Hz, 16-bit,
stereo). Pre-mix per-channel capture is possible since the mixer loop is
explicit — defer this until there's a use case.

WAV writer adapted from triton's `wav_save()`.

Files:
- Capture accumulation + WAV writer in `src/ludica/automation.c`
- Hook in `src/ludica/audio.c` to call capture tap

## Phase 6 — MCP server

Standalone C program using jsmn.h for JSON-RPC parsing. Reads MCP
protocol on stdin, translates tool calls to TCP commands against the
ludica automation port, formats JSON responses to stdout. No HTTP — MCP
transport is stdio.

Tools exposed:
- `list_actions` — discover available game actions and their key bindings
- `do_action` — trigger a named action (press, hold, release)
- `step` — advance N frames, returns frame number
- `screenshot` — capture screen or region, return as base64 or file path
- `read_pixel` — probe RGB at a coordinate
- `query` — read game state (frame, size, fps, registered variables)
- `list_vars` — enumerate app-registered state variables
- `send_keys` — low-level key injection (fallback when actions aren't defined)
- `mouse_click` — click at position
- `audio_capture` — start/stop WAV recording
- `quit` — request exit

Files:
- `tools/ludica-mcp.c`
- `src/thirdparty/jsmn.h` (or `tools/jsmn.h`)
