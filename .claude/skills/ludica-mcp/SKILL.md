---
name: ludica-mcp
description: Interact with a running ludica game via the MCP automation server. Use this skill whenever you need to observe, test, play, or control a ludica-based game -- taking screenshots, sending input, stepping frames, reading game state, or recording audio. Activate when the user asks you to play a game, test gameplay, automate a game, or debug visual output.
---

# Ludica MCP -- AI Game Automation

You are connected to a running ludica game through the `ludica-mcp` MCP
server. This gives you tools to observe and control the game in a
frame-by-frame loop.

## Setup

The game must be launched with automation flags:

```sh
_out/x86_64-linux-gnu/bin/<game> --auto-port 4000 --paused --fixed-dt
```

- `--paused` freezes the game until you send STEP commands
- `--fixed-dt` uses constant 1/60s timesteps for determinism
- `--auto-port 4000` opens the TCP automation port

The MCP server connects to this port. If the game isn't running or the
port doesn't match, tool calls return "not connected to game".

## Core Loop

Your primary workflow is observe-decide-act-step:

1. **Observe**: `screenshot` to see the screen, `query` or `read_pixel` for state
2. **Decide**: Analyze what you see and determine the right input
3. **Act**: `do_action` (preferred) or `send_keys` (fallback)
4. **Step**: `step` to advance frames and see the result

The game is frozen between steps. Nothing happens until you call `step`.

## Tools

### Discovery (call these first)

- **`list_actions`** -- Returns available game actions as `name=Key` pairs.
  These are the semantic inputs the game defines (e.g., `jump=Space`,
  `move_left=A`). Always call this first and use `do_action` with these
  names rather than raw keys.

- **`list_vars`** -- Returns registered state variable names. Use
  `query(what="var", name="...")` to read their values.

### Input

- **`do_action(name, mode?)`** -- Preferred input method. Triggers a game
  action by name. Modes:
  - `press` (default): activate for one frame, auto-releases
  - `hold`: keep active across multiple steps
  - `release`: release a held action

  Example: `do_action(name="jump")` or `do_action(name="move_left", mode="hold")`

- **`send_keys(key, action?)`** -- Low-level fallback. Key names are
  case-insensitive. Common names: A-Z, 0-9, Space, Escape, Enter, Tab,
  Left, Right, Up, Down, F1-F12, LeftShift, LeftControl, LeftAlt.
  Actions: `press` (default, down+up), `down`, `up`.

- **`mouse_click(x, y, button?)`** -- Click at screen coordinates.
  Button defaults to 1 (left).

### Observation

- **`screenshot(x?, y?, width?, height?, file?)`** -- Capture the screen.
  With no arguments, returns a full-screen base64 PNG that you can see
  directly. Provide x/y/width/height for a sub-region. Provide `file` to
  save to disk instead of returning inline.

- **`read_pixel(x, y)`** -- Returns `R G B` values (0-255) at a coordinate.
  Faster than a full screenshot when checking a specific spot.

- **`query(what, name?)`** -- Read game state:
  - `what="frame"` -- current frame number
  - `what="size"` -- window size as `W H`
  - `what="fps"` -- current frame rate
  - `what="var"` -- value of a named variable (pass `name`)

### Frame Control

- **`step(count?)`** -- Advance `count` frames (default 1). Returns the
  new frame number. This is how time passes -- the game does nothing
  until you step. After sending input, step at least once to see the
  effect.

### Audio

- **`audio_capture(action, file?)`** -- `action="start"` begins recording
  all game audio. `action="stop"` writes a WAV file (44100 Hz, stereo,
  16-bit) and returns the path.

### Lifecycle

- **`quit`** -- Ask the game to exit cleanly.

## Tips

- Always call `list_actions` at the start. Prefer `do_action` over
  `send_keys` -- it uses the game's own action names so you don't need
  to know key bindings.

- After sending input, call `step(count=1)` to advance one frame, then
  `screenshot` to see the result. If you need to hold a direction for
  movement, use `do_action(name="move_right", mode="hold")`, then step
  multiple frames, then `do_action(name="move_right", mode="release")`.

- For animations or testing over time, step multiple frames:
  `step(count=60)` advances one second of game time.

- Use `query(what="var", name="score")` to check game state numerically
  rather than trying to read numbers from screenshots.

- Screenshots are the most expensive operation. Use `read_pixel` or
  `query` when you only need a specific value.

- If a tool returns "not connected to game", the game isn't running or
  is on a different port. The game must be started separately with
  `--auto-port`.
