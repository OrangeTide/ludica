---
name: ludica-mcp
description: Observe, control, and debug ludica games through the ludica-mcp launcher. Use when the user asks you to play a game, test gameplay, reproduce a bug, inspect a crash, automate a UI, or step through frames.
---

# Ludica MCP — Agent Guide

You drive ludica games through `ludica-mcp-bridge`, a stdio MCP server
wired up in `.claude/settings.json`. The bridge talks over TCP to
`ludica-mcp` — a long-lived launcher the user starts once at the
beginning of the work session. The launcher owns the game process,
captures its stdout/stderr, proxies a control fd for input/frame-step,
and collects crash cores. The full protocol reference is
`doc/manual/ludica-mcp.md`; this skill is the how-to-drive version.

## Before you start

- The launcher must already be running. If tool calls fail with
  something like `not_connected`, ask the user to run `ludica-mcp &`.
  Don't try to spawn it yourself.
- `LUDICA_MCP_ALLOWEXEC` on the launcher gates what binaries you can
  spawn. Only aliases in that list work. Typical setup:
  `LUDICA_MCP_ALLOWEXEC=$(echo _out/*/bin/* | tr ' ' ':')` in `.env`.
- Every bridge process is its own session. When your connection closes
  the game is killed — call `session_nokill` early if you need it to
  survive a reconnect.

## Core flow

```
ping            -> OK pong           (is the launcher there?)
spawn <alias>   -> OK pid=<n>        (start a game; clears prior logs)
list_actions    -> jump=Space ...    (what input names the game exposes)
list_vars       -> score health ...  (what state you can query)
pause  / step   -> advance by frames (deterministic observe/act loop)
screenshot      -> base64 PNG        (or file=PATH to save)
query what=var  -> numeric state     (cheaper than screenshot)
kill            -> OK killed         (stop the game)
```

Every command replies `OK [data]` or `ERR <reason>`. Multi-line bodies
are framed; the bridge handles framing, you just read the text.

## Tool reference

### Lifecycle
- **`spawn(alias, args?)`** — fork/exec a game. `alias` is a bare
  filename or full path in `LUDICA_MCP_ALLOWEXEC`. `args` is a string
  array, passed through verbatim. A second `spawn` kills the prior game
  and clears its log buffer.
- **`kill(signal?)`** — send signal (default TERM); `KILL`, `INT`, etc.
- **`status()`** — `running pid=N` | `exited code=N` | `signaled sig=N`
  | `never`. Check this after an unresponsive game to see if it crashed.
- **`env(key, value?)`** / **`unsetenv(key)`** — modify the launcher's
  env for the *next* spawn. Not the running game.

### Control (game must be spawned with a control fd — ludica games always are)
- **`list_actions()`** — returns `name=Key ...`. Always call first.
- **`list_vars()`** — returns registered `lud_auto_register_*` names.
- **`action(name, mode?)`** — `mode`: `press` (default, one frame),
  `hold`, `release`. Prefer this over any key-level API.
- **`step(n?)`** — advance N frames (default 1). Requires the game to
  be paused; pair with `pause` first.
- **`pause()`** / **`resume()`** — toggle the run state.
- **`seed(n)`** — set the deterministic RNG seed before a repro run.
- **`screenshot(x?, y?, width?, height?, file?)`** — full screen by
  default. `file=PATH` writes to disk and returns the path; otherwise
  returns base64 PNG inline.
- **`read_pixel(x, y)`** — `R G B`. Much cheaper than a screenshot when
  you only care about one spot.
- **`query(what, name?)`** — `what` is `frame`, `size`, `fps`, or `var`
  (pass `name` for `var`).

### Logs (survive game exit; cleared on next `spawn`)
- **`log_tail(n, streams?)`** / **`log_head(n, streams?)`** —
  last/first N lines. `streams` is `["stdout"]`, `["stderr"]`, or
  `["stdout","stderr"]` (default both, lines prefixed).
- **`log_range(a, b, streams?)`** — 1-based inclusive slice.
- **`log_grep(pattern, ctx?, streams?)`** — POSIX extended regex.
- **`log_where(predicates, streams?)`** — structural filter over JSON
  log lines. Predicates are `key=value` or `key~regex`.
- **`log_jq(expr, streams?)`** — pipe through `jq`.
- **`log_clear(streams?)`** — drop buffered lines.

### Crash forensics (work whether or not the game is currently running)
- **`gdb_hint()`** — pid + a `gdb -p` command for the user to paste.
- **`gdb_core_find()`** — path of the most recent core for the last
  spawned binary. `ERR no_core` with diagnostics if none.
- **`gdb_core_list()`** — all available cores for that binary.
- **`gdb_core_summary(core?)`** — one-liner: `file:line in func: SIGNAME`.
- **`gdb_core_backtrace(core?, limit?)`** — full trace.
- **`gdb_core_frame(frame, core?)`** — detail for one frame.
- **`gdb_core_locals(frame?, core?)`** — locals at a frame.

### Sessions (usually you don't need these)
- **`session_info()`** — id, name, attached state.
- **`session_name(name)`** — give the session a stable handle so you
  can reconnect after a rebuild/restart.
- **`session_nokill()`** — keep the game alive after this bridge
  disconnects.
- **`session_list()`** — see what other agents are doing on the same
  launcher.
- **`session_kill(name)`** — force-destroy another session. Careful.

### Meta
- **`help()`** — list every launcher command with a one-line summary.
  Start here if you're unsure what's available.
- **`help(command="spawn")`** — detailed help for one command, as a
  ground-truth source for syntax.
- **`version()`**, **`ping()`** — launcher sanity checks.

## Pre-canned recipes

### Sanity check the launcher
```
ping                         -> OK pong
version                      -> OK <semver>
session_info                 -> OK id=<n> ...
```

### Start a deterministic run, paused
```
spawn(alias="hero", args=["--paused", "--fixed-dt"])
list_actions
list_vars
query(what="size")           -> W H
screenshot                   -> title/intro frame
```
`--paused --fixed-dt` are ludica flags (not launcher flags); they tell
the game to start frozen and use fixed 1/60s timesteps. The launcher
passes them through `args` untouched.

### Observe/act loop
```
action(name="move_right", mode="hold")
step(n=30)                   -> frame=30
action(name="move_right", mode="release")
screenshot                   -> see the result
query(what="var", name="score")
```

### Find errors in logs
```
log_tail(n=50, streams=["stderr"])
log_where(predicates=["lvl=error"])           # structured lines
log_grep(pattern="assert|SIGSEGV", ctx=3)
log_jq(expr='select(.lvl=="warn") | .msg')
```

### Triage a crash
```
status                       -> signaled sig=11 ...
gdb_core_summary             -> src/hero/hero.c:214 in draw_x: SIGSEGV
gdb_core_backtrace(limit=20)
gdb_core_frame(frame=3)
gdb_core_locals(frame=3)
log_tail(n=40, streams=["stderr"])   # last output before the crash
```

### Survive a rebuild
```
session_name(name="worktree-x")
session_nokill
# user rebuilds and restarts their own game manually, or:
kill                         # stop old build
spawn(alias="hero", ...)     # start fresh build, logs cleared
```

### Discover an unknown command
```
help                         -> full command list with one-liners
help(command="log_where")    -> detailed syntax for one command
```

## Tips

- **Start with `help` or `list_actions` when in doubt.** Both are cheap
  and confirm the launcher is alive and the game is controllable.
- **Prefer `query`/`read_pixel` over `screenshot`.** Screenshots are
  large; a number or a pixel RGB is often enough to check game state.
- **`--fixed-dt` + `pause`/`step` is how you get deterministic
  repros.** Same seed + same inputs + same steps = same frames.
- **Logs persist across game exit.** After a crash, `log_tail` still
  shows the final output. Combined with `gdb_core_summary` that's
  usually enough to locate a bug.
- **`spawn` clears the log buffer.** If you need to preserve logs
  across a run, save them first with `log_tail n=10000`.
- **Multi-line tool results come back as a single `text` content
  block** with `\n`-joined lines. The bridge unescapes framed bodies
  automatically.
- **Error prefixes** you'll see: `ERR exec:` (allowlist/path),
  `ERR no_process` (no spawn yet), `ERR no_control` (game doesn't have
  a control fd), `ERR no_core` (no crash dump found),
  `ERR usage:` (bad args), `ERR jq:` (jq error).

## Making a game automatable

For a game to expose meaningful `list_actions`/`list_vars`, its source
needs to register them:

```c
lud_action_t jump = lud_make_action("jump");
lud_bind_key(jump, LUD_KEY_SPACE);

lud_auto_register_int("score", &score);
lud_auto_register_str("level_name", &level_name);
```

No extra setup beyond that — `lud_run` and the control-fd handshake
are automatic when the launcher passes `---controlfd=N`.
