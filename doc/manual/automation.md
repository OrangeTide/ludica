# Automation & MCP Manual

Ludica includes a TCP automation server and an MCP (Model Context Protocol)
bridge that let AI agents and scripts observe and control running games.

## Quick Start

1. Launch a game with automation enabled:

```sh
_out/x86_64-linux-gnu/bin/hero --auto-port 4000 --paused --fixed-dt
```

2. Connect Claude Code (or any MCP client) via the MCP server:

```json
{
  "mcpServers": {
    "ludica": {
      "command": "_out/x86_64-linux-gnu/bin/ludica-mcp",
      "args": ["--port", "4000"]
    }
  }
}
```

The game starts frozen. The agent advances frames with `step`, observes the
screen with `screenshot`, and sends input with `do_action` or `send_keys`.

The game auto-terminates when the first TCP client disconnects. Send `NOKILL`
early in the session to keep it running for multiple connections.

## Command-Line Flags

These flags are parsed by `lud_run()` and apply to any ludica program:

| Flag | Description |
|------|-------------|
| `--auto-port PORT` | Enable TCP automation on PORT (default: off) |
| `--auto-file FILE` | Replay commands from a text file |
| `--capture-dir DIR` | Output directory for screenshots and audio captures |
| `--width W --height H` | Force window size |
| `--paused` | Start frozen, wait for first STEP command |
| `--fixed-dt` | Use constant dt (1/60s) instead of wall clock |

For AI agent use, always pass `--paused --fixed-dt` so the agent has
deterministic control over timing.

## TCP Text Protocol

The automation server accepts one TCP connection at a time on the specified
port. Protocol: one command per line (`\n` terminated), one response per
command. All responses are `OK [data]` or `ERR <message>`.

### Help

```
HELP                    -> OK STEP:Advance N frames; RESUME:Unpause; ...
HELP <command>          -> OK STEP — Detailed description of the command...
```

List all commands with one-line summaries, or get a detailed paragraph
about a specific command.

### Frame Control

```
STEP [N]                -> OK <frame_number>
```

Advance N frames (default 1). The game stays frozen between steps, giving
the controller full pacing control. This is the primary command for agents.

```
FRAMEDELAY <count>      -> OK
```

Pause command reading for N frames. For scripted replay files alongside
real-time playback.

```
SEED <n>                -> OK seed set to <n>
```

Set the RNG seed for deterministic runs.

### Actions (High-Level Input)

Games register named actions via `lud_make_action()` / `lud_bind_key()`.
Agents should prefer actions over raw keys -- they work at the game's
semantic level without knowing key bindings.

```
LISTACTIONS             -> OK move_left=Left move_right=Right jump=Space ...
ACTION <name>           -> OK    (press and auto-release next frame)
ACTION <name> HOLD      -> OK    (press and hold)
ACTION <name> RELEASE   -> OK    (release held action)
```

### Low-Level Input

Fallback for when actions aren't defined. Key names are case-insensitive.

```
KEYDOWN <keyname>               -> OK
KEYUP <keyname>                 -> OK
MOUSEMOVE <x> <y>              -> OK
MOUSEDOWN <button> <x> <y>     -> OK
MOUSEUP <button> <x> <y>       -> OK
SCROLL <dx> <dy>               -> OK
```

### Key Names

Single letters (`A`-`Z`) and digits (`0`-`9`) are their own names. Other
key names: `Space`, `Escape`, `Enter`, `Return`, `Tab`, `Backspace`,
`Insert`, `Delete`, `Left`, `Right`, `Up`, `Down`, `PageUp`, `PageDown`,
`Home`, `End`, `F1`-`F12`, `LeftShift`, `RightShift`, `LeftControl`,
`RightControl`, `LeftAlt`, `RightAlt`, `LeftSuper`, `RightSuper`,
`CapsLock`, `Minus`, `Equal`, `LeftBracket`, `RightBracket`, `Backslash`,
`Semicolon`, `Apostrophe`, `GraveAccent`, `Comma`, `Period`, `Slash`,
`Menu`, `KP0`-`KP9`, `KPDecimal`, `KPDivide`, `KPMultiply`, `KPSubtract`,
`KPAdd`, `KPEnter`, `KPEqual`.

### Screen Capture

```
CAPSCREEN [filename]            -> OK <path>
CAPSCREEN --base64              -> OK base64:<png_data>
CAPRECT <x> <y> <w> <h> [file] -> OK <path>
CAPRECT <x> <y> <w> <h> --base64 -> OK base64:<png_data>
READPIXEL <x> <y>              -> OK <r> <g> <b>
```

Without `--base64`, captures are written as PNG files to `--capture-dir`
(or current directory). With `--base64`, the PNG is returned inline in
the response. Coordinates are in screen pixels, origin top-left.

A `SIGUSR1` signal (Linux only) also triggers a full-screen capture without
TCP.

### Audio Capture

```
CAPAUDIO START                  -> OK
CAPAUDIO STOP [filename]        -> OK <path>
```

START begins recording the mixed audio output. STOP writes a WAV file
(44100 Hz, 16-bit stereo) and returns the path. If no filename is given,
one is generated from the frame count.

### Lifecycle

```
QUIT                            -> OK
NOKILL                          -> OK auto-terminate disabled
RESTART                         -> OK restarting
RESUME                          -> OK game resumed
```

By default, the game auto-terminates when the first TCP client disconnects.
Send `NOKILL` to disable this, allowing the game to accept new connections.

`RESTART` re-execs the game process via `execv()`, picking up any newly
built binary. The listen socket is preserved so the same port stays active.
Not available on Windows.

### State Queries

```
QUERY FRAME                     -> OK <number>
QUERY SIZE                      -> OK <w> <h>
QUERY FPS                       -> OK <fps>
QUERY VAR <name>                -> OK <value>
LISTVAR                         -> OK name1 name2 ...
```

Games register queryable variables with:

```c
lud_auto_register_int("score", &score);
lud_auto_register_str("scene", &scene_name);
```

## MCP Server

`ludica-mcp` is a standalone bridge program that reads MCP JSON-RPC on
stdin and translates tool calls into TCP automation commands. It does not
link against ludica -- it connects over TCP.

```
ludica-mcp [--port PORT]
```

Default port is 4000. The server implements MCP protocol version
`2025-03-26` with stdio transport (newline-delimited JSON).

### MCP Tools

| Tool | Description | Key Arguments |
|------|-------------|---------------|
| `list_actions` | Discover game actions and key bindings | -- |
| `do_action` | Trigger a named action | `name`, `mode` (press/hold/release) |
| `step` | Advance N frames | `count` (default 1) |
| `screenshot` | Capture screen as base64 PNG or file | `x`, `y`, `width`, `height`, `file` |
| `read_pixel` | Read RGB at a coordinate | `x`, `y` |
| `query` | Read game state | `what` (frame/size/fps/var), `name` |
| `list_vars` | List registered state variables | -- |
| `send_keys` | Low-level key injection | `key`, `action` (press/down/up) |
| `mouse_click` | Click at position | `x`, `y`, `button` |
| `audio_capture` | Start/stop audio recording | `action` (start/stop), `file` |
| `quit` | Request game exit | -- |
| `help` | List commands or get detailed help | `command` (optional) |
| `restart` | Re-exec with fresh binary | -- |
| `nokill` | Disable auto-terminate on disconnect | -- |

### Tool Details

**`list_actions`** -- Call this first to discover what the game supports.
Returns space-separated `name=Key` pairs. Use `do_action` with these names
rather than `send_keys`.

**`do_action`** -- Default mode is `press` (activate for one frame). Use
`hold` to keep it active across multiple steps, then `release` when done.

**`step`** -- Advances the game by `count` frames (default 1). Returns the
resulting frame number. The game is frozen between steps when launched with
`--paused`. Always step after sending input to see the effect.

**`screenshot`** -- With no arguments, captures the full screen and returns
a base64-encoded PNG image. To capture a sub-region, provide `x`, `y`,
`width`, `height`. To save to disk instead of inline, provide `file`.

**`read_pixel`** -- Returns `R G B` values (0-255) at the given coordinate.
Useful for checking specific screen locations without a full screenshot.

**`query`** -- `what=frame` returns the current frame number. `what=size`
returns `W H` in pixels. `what=fps` returns the frame rate. `what=var`
with a `name` returns a game-registered variable's value.

**`send_keys`** -- Fallback when the game has no actions defined. Key names
are case-insensitive (see Key Names section). Default action is `press`
(down then up). Use `down`/`up` for held keys.

**`audio_capture`** -- `start` begins recording all audio output. `stop`
writes a WAV file and returns the path. Optionally provide `file` as the
output filename.

## Agent Workflow

A typical agent session:

1. Launch the game with `--auto-port 4000 --paused --fixed-dt`
2. Configure MCP client to use `ludica-mcp --port 4000`
3. Call `list_actions` to discover available inputs
4. Call `list_vars` to discover observable state
5. Loop:
   a. `screenshot` to observe the screen
   b. Decide on input (use `do_action` when possible)
   c. `step` to advance one or more frames
   d. `query` to check game state as needed

## Game-Side Setup

To make a game automation-friendly, register actions and variables:

```c
/* In init(): */
lud_action_t a_left  = lud_make_action("left");
lud_action_t a_right = lud_make_action("right");
lud_action_t a_jump  = lud_make_action("jump");
lud_bind_key(a_left,  LUD_KEY_A);
lud_bind_key(a_left,  LUD_KEY_LEFT);
lud_bind_key(a_right, LUD_KEY_D);
lud_bind_key(a_right, LUD_KEY_RIGHT);
lud_bind_key(a_jump,  LUD_KEY_SPACE);

lud_auto_register_int("score", &score);
lud_auto_register_int("level", &level);
lud_auto_register_str("scene", &scene_name);
```

The automation system is a no-op when `--auto-port` is not passed, so
registering actions and variables has zero overhead in normal play.
