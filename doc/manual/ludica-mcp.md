# Ludica MCP — Design & Architecture

This document describes the redesigned `ludica-mcp` automation system: its
goals, architecture, protocol, and implementation plan. It serves as both
the reference manual for users and the architectural guide for
contributors.

It supersedes the earlier in-process automation design documented in
`automation.md`. That earlier system linked the TCP server directly into
each game binary. This document describes its replacement: a standalone
launcher process that owns game lifecycle, log capture, and a proxied
control channel into the game.

## Why Redesign

The previous `ludica-mcp` was a library linked into every game. It worked
for simple cases but broke on the ones that mattered most:

- **Crashes kill the MCP**. When the game segfaults, the MCP dies with
  it. The agent loses its connection and has no way to read what
  happened.
- **gdb freezes the MCP**. Stopping the game under a debugger also stops
  the MCP thread. The agent cannot observe state while the debugger is
  active.
- **Restart requires reconnect dance**. Rebuilding and relaunching the
  game meant the agent had to tear down and rebuild its connection each
  cycle, and timing was fragile.
- **Agents burn tokens on logs**. The old design had no log facility;
  agents that wanted stderr output had to screenshot the terminal or run
  the game outside MCP entirely.
- **Core dumps disappear into the void**. When a game crashes, the core
  file lands in a distro-specific location (`/var/lib/systemd/coredump`,
  `/var/lib/apport/coredump`, `/var/crash`, CWD, something set by
  `/proc/sys/kernel/core_pattern`). Agents have no reliable way to find
  it, so post-mortem debugging was ad-hoc or skipped.

The common thread is that the MCP's *process lifetime* was tied to the
game's. The fix is to separate them.

## Goals

1. **Resilient** — the MCP survives game crashes, gdb sessions, rebuilds,
   and re-execs. Agent reconnects are cheap.
2. **Token-efficient** — queries return summaries, not dumps. No agent
   should have to read ten thousand lines of log to find an error.
3. **Observable even when uncontrolled** — if the game is paused in gdb
   or has crashed, logs and process state remain readable.
4. **Single SKILL.md covers the surface** — an agent can learn the whole
   interface without trial and error.
5. **Zero overhead when not automated** — the game binary runs
   identically whether launched via MCP or directly from a shell.
6. **Every command acknowledges** — `OK [data]` or `ERR <reason>` on
   every command, including spawn. Agents never guess whether something
   happened.
7. **Multi-session** — the launcher serves several concurrent clients.
   Two Claude sessions in different worktrees, or an Opus parent
   delegating frame-capture and crash-bisect work to Haiku/Sonnet
   sub-agents, all share one launcher without interfering with each
   other's game processes or log buffers.

## Non-Goals

- Remote (non-localhost) MCP access.
- Complete replacement for gdb. The launcher points the agent at gdb and
  steps out of the way.
- Full shell semantics for env var expansion or arg quoting.
- High concurrency. A handful of concurrent sessions is the design
  point; hundreds is not. No threading, no scaling story.
- Shared observation of one game by multiple sessions. Each session
  owns its game.

## Architecture

One long-lived launcher, one bridge per Claude session, one game per
session:

```
  [Claude Opus]           [Claude (other worktree)]    [Haiku sub-agent]
      |                         |                             |
      | stdio MCP               | stdio MCP                   | stdio MCP
      v                         v                             v
  [bridge A]               [bridge B]                     [bridge C]
      |                         |                             |
      '----- TCP ---------------+-----------------------------'
                                |
                                v
                    +-----------------------------+
                    |    ludica-mcp launcher      |
                    |   (single process, libiox)  |
                    |                             |
                    |   session A   session B     |
                    |   session C   ...           |
                    +------|--------|-------|-----+
                           |        |       |
                           v        v       v
                        [game]   [game]   [game]
                           |        |       |
                           | (per-session control fd)
                           v
                   (in-process instrumentation)
```

### Responsibilities

**Launcher (`ludica-mcp`)**

- Accepts many TCP connections on `LUDICA_MCP_PORT`. Each connection is
  one session with independent state.
- Single-threaded event loop built on `libiox`
  (`/home/jon/DEVEL/lumi/src/libiox`): poll-based fd watching, one-shot
  timers, self-pipe signals (notably SIGCHLD for child exit).
- Enforces the allowlist: binaries outside `LUDICA_MCP_ALLOWEXEC` cannot
  be spawned. Allowlist and .env-derived config are global.
- Per session: fork/exec the game, wire stdout and stderr to a
  session-local circular buffer, optionally allocate a control fd
  (socketpair). A session owns at most one game at a time.
- Exposes the TCP command surface: lifecycle, logs, control-fd proxy,
  gdb affordances.
- Survives game exit and crash. Holds each session's most recent logs
  until that session's next spawn (or session teardown).

**Game process**

- Accepts a hidden `---controlfd=N` argument (triple-dash namespace to
  avoid collision with user flags).
- If present, spawns a small in-process handler that speaks the same
  line protocol on that fd: input injection, frame step, screenshot,
  variable query.
- When absent, the game runs normally with zero overhead.

**Bridge (`ludica-mcp-bridge`)**

- JSON-RPC over stdio on the Claude Code side; TCP on the launcher
  side.
- Pure translation. No state. One process per Claude session.
- Re-reads `LUDICA_MCP_PORT` from the environment at startup.

### Shell out; don't reimplement

The launcher already has machinery for spawning, tracking, and
capturing output from child processes. It should reuse that machinery
whenever a well-known Unix utility does the job better than a
hand-rolled version would:

- `jq` for JSON queries on logs and events.
- `gdb -batch` for core summaries, introspection, and multi-thread
  backtraces.
- `coredumpctl` for locating cores on systemd-coredump systems;
  `apport-unpack` for apport; plain `find` / stat for filesystem
  patterns.
- `addr2line`, `nm`, `objdump` for symbol/address lookups when a stack
  trace needs resolution.
- `file` / `readelf` to identify binary types and dynamic deps.

Pre-canned actions are thin wrappers: the launcher supplies the right
command, the right args, the right working directory, and parses or
truncates the output before returning it to the agent. The *value* is
in orchestration — knowing which utility to run and caching results —
not in recreating functionality.

The rule of thumb: if a standard utility exists and the agent would
reach for it in a shell, expose it as a canned action rather than
implementing a bespoke version. Exception: when the action runs on
the hot path of a control loop (per-frame, per-event). Those stay in
process.

### Why three processes, not two

The launcher survives many Claude sessions. The user starts it once at
the beginning of the workday. Each Claude Code invocation spins up its
own short-lived bridge that connects to the already-running launcher.
This is why the bridge is trivial and the launcher does all the real
work.

### Session lifecycle

- **Open**: a bridge opens a TCP connection. A new session is
  auto-created with a fresh log buffer, empty env overrides, and no
  game. The server replies to the first command normally; no explicit
  `HELLO` is required.
- **Identify (optional)**: `session name <name>` gives the session a
  stable handle. Named sessions can be detached and reattached.
- **Detach**: `session detach` disconnects the TCP socket but keeps
  the session (and its game) alive. Intended for `restart`-style
  flows where the bridge needs to reconnect after a rebuild.
- **Attach**: on a fresh connection, `session attach <name>`
  reconnects to a detached session. If the name is unknown it returns
  `ERR no_session`.
- **Close**: bridge disconnects. If the session is attached and not
  detached, the game is killed (unless `session nokill` was set) and
  the session is destroyed. `nokill` keeps the game running and turns
  the session into a detached one implicitly.

Sessions are strictly independent. A command issued on session A
cannot observe or control session B's game or logs. The allowlist,
`.env`-derived config, and the crash-core cache (keyed by binary
path) are the only global state.

## Configuration

### .env

At startup the launcher reads `.env` from the current working directory
(or from `--env-file PATH`). Every key is applied via `setenv(k, v, 1)`
— values in `.env` override the inherited environment. `.env` is
global; all sessions see the same config. Format:

- One `KEY=VALUE` per line.
- Blank lines and lines starting with `#` are ignored.
- Values may be quoted with `"..."`; inside quotes the escapes `\\`,
  `\"`, `\$`, and `` \` `` are recognized.
- No shell interpolation. No `${VAR}` expansion.

Parser is lifted from `sessdir_state.c` in `lumi` (see
`parse_dotenv`/`unescape_quoted`).

### Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `LUDICA_MCP_PORT` | 4000 | TCP listen port |
| `LUDICA_MCP_ALLOWEXEC` | (none) | `:`-separated list of spawnable binary paths |
| `LUDICA_MCP_LOG_BYTES` | 1048576 | Circular buffer size per stream |
| `LUDICA_MCP_CAPTURE_DIR` | `./capture` | Screenshot and audio output directory |
| `LUDICA_MCP_CPU_LIMIT` | (unset) | `RLIMIT_CPU` seconds for child |
| `LUDICA_MCP_AS_LIMIT` | (unset) | `RLIMIT_AS` bytes for child |

`LUDICA_MCP_ALLOWEXEC` is a literal list; no glob expansion in v1. A
convenient way to populate it from the current build:

```sh
echo LUDICA_MCP_ALLOWEXEC=$(echo _out/*/bin/* | tr ' ' ':') > .env
```

If wildcard-at-launch-time becomes common, add a separate
`LUDICA_MCP_ALLOWGLOB` variable later. Don't build it until it's
needed.

## TCP Protocol

Line-delimited text over TCP. The launcher accepts one bridge
connection at a time. Every command produces exactly one response:

```
OK [data]
ERR <reason>
```

Commands that return multiple lines (`log_tail`, `log_grep`, `log_jq`,
`gdb_core_backtrace`, `help` listing, etc.) wrap their body in an
explicit frame so clients don't need to guess where the response ends:

```
OK\n                        <- status line
<body line 1>\n
<body line 2>\n
...
END\n                       <- success terminator
```

If the body fails asynchronously (e.g. `log_jq`'s jq child exits
non-zero), the terminator carries the reason instead:

```
END ERR <reason>\n
```

Body lines that would otherwise be ambiguous are escape-encoded: if a
body line begins with `\` or with the three bytes `END`, the launcher
prepends one extra `\`. Decoders strip one leading `\` from every body
line. With `nc`, the end of a response can be found with
`grep -n '^END\( \|$\)'` and bodies unescaped with `sed 's/^\\\\/\\/'`.

`ERR <reason>\n` single-line responses are **not** wrapped — no `END`
terminator follows.

The launcher may also emit asynchronous event lines:

```
EVENT <name> [data]
```

Events are opt-in: a client subscribes with `subscribe <pattern>` (glob)
and unsubscribes with `unsubscribe`. Clients that do not subscribe never
see events and can use a strict request/response loop.

### Command style

Commands follow a `noun_verb` naming convention (`log_grep`,
`gdb_core_backtrace`, `session_attach`) but are **stateless**: there is
no "current buffer" or "selected object" held between calls. Every
command carries everything it needs to act.

Where a command can target multiple instances of the same kind of
object, the targets appear as **trailing positional arguments**, like
file arguments to shell utilities. `grep PATTERN file1 file2` becomes
`log_grep PATTERN stdout stderr`. Omitting the targets means "all
applicable" — `log_grep PATTERN` searches both streams. Single-instance
targets (the running process, the session's control fd) take no
selector.

Optional named parameters use `--key=value`. Positional arguments are
for required or obvious targets; flags are for tuning (`--limit=N`,
`--core=PATH`, `--base64`).

### Lifecycle commands

```
spawn <alias> [args...]       -> OK pid=<n>             | ERR exec: <reason>
kill [signal]                 -> OK killed pid=<n>      | ERR no_process
status                        -> OK running pid=<n> | exited code=<n> | signaled sig=<n> | never
env <KEY> [VALUE]             -> OK <KEY>=<VALUE>
unsetenv <KEY>                -> OK
```

`spawn` forks, execs, and wires stdout/stderr into the launcher's log
buffer. `<alias>` may be a bare filename matching the last path
component of any entry in `LUDICA_MCP_ALLOWEXEC`, or a full allowlisted
path. Args are passed through verbatim. A second `spawn` while a
process is already running first kills the old one, clears the log
buffer, then spawns the new.

`env` without `VALUE` reads. With `VALUE` it sets the launcher's
environment, which is inherited by the next spawn. It does not affect
a running process.

### Log commands

```
log_tail <n> [streams...]                      -> OK <n lines>
log_head <n> [streams...]                      -> OK <n lines>
log_range <a> <b> [streams...]                 -> OK <lines a..b>
log_grep <regex> [--ctx=<n>] [streams...]      -> OK <matching lines>
log_where <field>=<value>... [streams...]      -> OK <matching JSON lines>
log_jq <jq-expr> [streams...]                  -> OK <jq output>
log_clear [streams...]                         -> OK cleared
```

A `stream` argument is `stdout` or `stderr`. When no streams are given,
the command operates on both, merged in timestamp order. Supplying one
or more stream names restricts the operation — exactly like file
arguments to `grep`, `tail`, `head`.

`log_where` is structural: each line is parsed as JSON and filtered by
field equality (or `field~regex` for pattern match on a field value).
Non-JSON lines are skipped. See "Game-side Log Format" below.

`log_jq` pipes the buffer through `jq` (via fork/exec/pipe) and returns
its output. The agent writes jq expressions directly —
`log_jq 'select(.lvl=="error") | .msg' stderr` — which is far more
expressive than anything we'd reinvent. Errors from `jq`
(expression syntax, missing binary) surface as `ERR jq: <reason>`.
Requires `jq` to be on `PATH`.

Line numbers are 1-based and refer to the current contents of the
circular buffer (earliest retained line is line 1). `log_grep` uses
POSIX extended regex (`regcomp` with `REG_EXTENDED`); upgrade to PCRE
only if real use cases demand it.

Logs persist across game exit. They are cleared on the next `spawn`.

### Control commands

These proxy through the game's control fd. If the game was not spawned
with a control fd (or the fd has been closed), they return
`ERR no_control`.

```
action <name> [hold|release]          -> OK action <name> pressed|held|released
step [n]                              -> OK frame=<n>
pause                                 -> OK paused
resume                                -> OK resumed
seed <n>                              -> OK seed=<n>
screenshot [x y w h] [--base64|--file=PATH]
                                      -> OK <path>                | OK base64:<data>
read_pixel <x> <y>                    -> OK <r> <g> <b>
query <what> [name]                   -> OK <value>
list_actions                          -> OK <name>=<key> ...
list_vars                             -> OK <name> ...
```

`what` is one of `frame`, `size`, `fps`, `var`.

### gdb commands

Live (process running):

```
gdb_hint                              -> OK pid=<n> suggested: gdb -p <n>
gdbserver <port>                      -> OK listening on :<port>
introspect <name>                     -> OK <dump>   | ERR unknown introspection
```

`gdb_hint` returns the PID and a shell command the user can paste.
`gdbserver` spawns the next game with `gdbserver --attach` or
`gdbserver :port` wrapping. `introspect` runs a pre-canned `gdb -batch
-ex "print ..."` against the live process and returns parsed output;
the set of known names comes from a small table in the launcher and
the skill documentation.

Post-mortem (core file):

```
gdb_core_find                                  -> OK path=<path>  | ERR no_core
gdb_core_list                                  -> OK <path1> <path2> ...
gdb_core_summary   [--core=<path>]             -> OK <file:line in func: <signal>>
gdb_core_backtrace [--core=<path>] [--limit=N] -> OK <backtrace>
gdb_core_frame <n> [--core=<path>]             -> OK <frame detail>
gdb_core_locals    [--core=<path>] [--frame=N] -> OK <locals>
```

`gdb_core_find` returns the most recent core matching the last spawned
binary. The other `gdb_core_*` commands default to that same core; pass
`--core=<path>` (from `gdb_core_list`) to target a different one —
targets are explicit, never held as hidden state. `gdb_core_summary`
runs a canned `gdb -batch` that produces a single-line summary pointing
at the first frame outside the crash infrastructure (`raise`, `abort`,
`__assert_fail`, `__GI_*`, and `_start` are skipped).
`gdb_core_backtrace` returns the full trace for deeper analysis.
`gdb_core_frame N` and `gdb_core_locals --frame=N` drill into a
specific frame without asking for the whole trace again.

All `gdb_core_*` commands work whether or not a process is currently
running: they operate on the saved core file, not on the live process.

### Session commands

```
session_info                          -> OK id=<n> name=<name> attached=<yes|no>
session_name <name>                   -> OK
session_detach                        -> OK detached  (connection then closes)
session_attach <name>                 -> OK attached
session_nokill                        -> OK           (game outlives disconnect)
session_list                          -> OK <session1> <session2> ...
session_kill <name>                   -> OK           (force-destroy a named session)
```

`session_list` is global — it lets one agent discover what other
sessions are active. It does not expose per-session log or process
detail; those require an attach. `session_kill` takes an explicit
name rather than a hidden "current session" because sessions are the
one kind of object where selecting the wrong target is most dangerous.

### Meta commands

```
help                                  -> OK <command summaries>
help <command>                        -> OK <command> — <detailed description>
version                               -> OK <version>
ping                                  -> OK pong
subscribe <event-pattern>             -> OK subscribed
unsubscribe                           -> OK
```

### Error categories

Every `ERR` response uses a short kind followed by a human description:

- `ERR exec: ...` — spawn failed (not in allowlist, no such file,
  permission, etc.)
- `ERR no_process` — command requires a running game; none exists
- `ERR no_control` — command needs the control fd; game wasn't spawned
  with one
- `ERR usage: ...` — malformed arguments
- `ERR not_allowed: ...` — allowlist or safety check rejected the
  request
- `ERR internal: ...` — launcher bug; should not happen

## Log Buffer

Each stream (stdout, stderr) has an independent circular byte buffer
sized by `LUDICA_MCP_LOG_BYTES`. Writes come from `read()` on the pipe
connected to the child's fd 1/2. The buffer retains the most recent
bytes; older data is dropped.

A lightweight line index is rebuilt on demand for range/head/tail/grep
queries. No attempt is made to preserve complete lines when truncating:
if a line is partially overwritten, it is reported as truncated with a
leading ellipsis.

Events `EVENT log_stdout_line <line>` and `EVENT log_stderr_line <line>`
are emitted for each complete line received (subscribers only, so the
cost is zero when no one is listening).

## Crash Handling

When a spawned child exits abnormally (killed by signal, typically
SIGSEGV or SIGABRT), the launcher performs automatic post-mortem
collection before returning control to the agent:

1. **Locate the core file.** The launcher knows where to look:
   - `/proc/sys/kernel/core_pattern` — the source of truth. If it
     starts with `|`, the kernel pipes to a helper such as
     `systemd-coredump` or `apport`. Parse the rest of the line to
     know which.
   - `systemd-coredump`: use `coredumpctl --no-legend list <binary>`
     to find the most recent, `coredumpctl dump <pid>` to extract.
   - `apport`: `/var/lib/apport/coredump/core.<binary>.*.<pid>`.
   - Plain pattern (`core`, `core.%p`, `core.%e.%p`, etc.): resolve
     against the child's working directory, then `/var/crash`, then
     `.`.

2. **Compute a crash summary.** Run once, cache the result:
   ```
   gdb -batch -nx \
       -ex "set pagination off" \
       -ex "bt 50" \
       -ex "quit" \
       <binary> <core>
   ```
   Walk the backtrace top-down, skipping frames whose function name
   matches a noise list: `raise`, `abort`, `__assert_fail`,
   `__assert_fail_base`, `__GI_*`, `__libc_*`, `_start`, `start_thread`,
   `__restore_rt`, and ludica's own error-path wrappers (once they
   exist). The first remaining frame is the reported crash site. The
   one-line summary looks like:
   ```
   src/hero/hero.c:214 in draw_sector_recursive: SIGSEGV
   ```

3. **Emit an event.** Subscribers receive
   ```
   EVENT crash pid=<n> sig=<n> core=<path>
   EVENT crash_summary <line>
   ```

4. **Cache for queries.** Subsequent `gdb_core_summary`,
   `gdb_core_backtrace`, etc. return cached data until the next
   `spawn` or `kill`, at which point the core reference is cleared
   (the file itself is not deleted; distros rotate cores their own
   way).

If no core is found, `gdb_core_find` returns `ERR no_core` with a
diagnostic pointing at `ulimit -c`, the current `core_pattern`, and
whether `systemd-coredump` / `apport` are installed. That alone saves
an agent a long search.

Debug builds should run with `ulimit -c unlimited` or a configured
`RLIMIT_CORE`; release builds default to `RLIMIT_CORE=0` unless
overridden by env.

## Game-side Log Format

Ludica will emit structured log lines: one JSON object per line,
newline-terminated, written to stderr. Example:

```json
{"t":1234567,"lvl":"info","msg":"loaded texture","tex":"stone.png","bytes":4096}
{"t":1234720,"lvl":"warn","msg":"portal recursion limit","sector":3,"depth":10}
{"t":1236441,"lvl":"error","msg":"assert failed","file":"hero.c","line":214,"expr":"sector<N"}
```

Required fields: `t` (monotonic ms since start), `lvl` (`debug`, `info`,
`warn`, `error`), `msg` (short human description). All other fields are
per-call. `file`/`line` auto-filled by the logging macro.

### Why not printf-style strings

- **Agent-friendly**: `log_where lvl=error` beats regex on free text.
- **Events get first-class treatment**: crash summaries, frame markers,
  and user-defined trace points are the same shape as log lines.
- **No stdio dependency**: the implementation is a small
  JSON-escaping writer. `printf`'s full grammar (field widths, `%n`,
  floating-point formatting, locale handling) is heavy in WASM and
  largely unneeded. A minimal vararg API that knows strings, ints,
  unsigned, hex, floats, and bools covers the use cases; no field
  widths, no padding.

### Proposed API shape

Exact signatures are an implementation concern, but the interface
should allow structured calls without per-call allocation:

```c
lud_log(LUD_LOG_INFO, "loaded texture",
    "tex",   LUD_STR(name),
    "bytes", LUD_INT(sz),
    NULL);
```

or a macro that captures `__FILE__`/`__LINE__` automatically. The
writer emits a single `{...}\n` line. Escaping rules are strict JSON
(`"`, `\\`, control chars to `\uXXXX`). No pretty-printing.

### MCP-side implications

- `log_grep` still works on the raw JSON text — fine for rough lookups.
- `log_where <field>=<value>` parses each line as JSON and filters
  structurally. Much cheaper in tokens than grep-then-read.
- `log_jq <expr>` pipes the buffer through `jq` for full-power
  transforms (see "Shell out; don't reimplement" above).
- `EVENT` lines from the launcher can reuse the same JSON shape, so
  agent parsers handle both uniformly.
- Malformed lines (non-JSON output from third-party libraries writing
  to stderr directly) are retained verbatim and surface through
  `log_grep` but not `log_where` / `log_jq`.

## Control fd Protocol

The launcher creates a `socketpair(AF_UNIX, SOCK_STREAM)`, passes one
end to the child via `---controlfd=N`, and proxies the other end. On the
game side, `lud_run` checks for `---controlfd=` early in startup and, if
present, dups the fd into a known slot and spawns a reader thread (or
poll-integrates it into the main event loop).

The control fd speaks the same line-oriented text protocol as TCP.
Commands the game understands are the "Control" subset above. Errors
and acknowledgments flow back on the same fd.

This inverts the old design: automation is no longer a TCP server
inside the game. The game exposes a single bidirectional fd. The
launcher is the only TCP listener.

## Resource Limits

On `spawn`, before `execve`, the launcher applies `setrlimit` for any
of `RLIMIT_CPU`, `RLIMIT_AS`, `RLIMIT_CORE` that are configured via
env. Defaults are: `RLIMIT_CORE=0` in release, unlimited with
`--debug`; CPU and AS unlimited. This keeps runaway agent loops from
filling disk or eating RAM.

## Usage

### Starting the launcher

The user starts the launcher manually, once, at the start of a work
session:

```sh
ludica-mcp &
```

`.env` lives in the project root. The launcher reads it from the
working directory at startup.

### Connecting Claude Code

`.claude/settings.json` configures the bridge:

```json
{
  "mcpServers": {
    "ludica": {
      "command": "_out/x86_64-linux-gnu/bin/ludica-mcp-bridge"
    }
  }
}
```

The bridge reads `LUDICA_MCP_PORT` from the environment and connects
to the already-running launcher. If the launcher isn't running, the
bridge reports `ERR not_connected` on every tool call and the agent
asks the user to start it.

### A typical session

1. User: `ludica-mcp &`
2. User: start Claude Code
3. Agent: `list_actions` — returns empty (no game running yet)
4. Agent: `spawn hero --fixed-dt --paused` — `OK pid=12345`
5. Agent: `step 1`, `screenshot`, repeat
6. Game crashes. Agent: `status` → `OK signaled sig=11 core=/var/lib/systemd/coredump/...`
7. Agent: `gdb_core_summary` → `OK src/hero/hero.c:214 in draw_sector_recursive: SIGSEGV`
8. Agent: `log_tail 20 stderr` — reads the last output before the crash
9. If needed: `gdb_core_backtrace 30` for the full trace
10. Agent fixes code, rebuilds, `spawn hero ...` again — the new core
    replaces the old in the cache
9. End of day. User: `kill %1` to stop the launcher.

### Debugging with gdb

1. Agent: `gdb_hint` → returns PID and a suggested command
2. User (in a separate terminal): `gdb -p 12345`
3. Agent continues to use `log_*` and `status` while debugger is
   attached. Control commands return `ERR no_control` (the game is
   paused) until the user continues.

Or, non-interactively:

4. Agent: `introspect player` → canned `gdb -batch -ex "print player"`
   returns the parsed value. Game is not attached permanently; the
   batch gdb session exits immediately.

## Implementation Plan

Six phases. Each phase is independently useful and independently
mergeable.

**Phase 1 — Skeleton.** New `tools/ludica-mcp/` (replaces the existing
library). Vendor `libiox` or add it as a dependency. TCP listen on
`LUDICA_MCP_PORT` with multi-client accept, dotenv loader, session
table, `help`, `ping`, `version`, `session *`. SIGCHLD handler in
place (no children yet, but wiring the signal early avoids retrofits).
No game interaction yet. Prove the shape.

**Phase 2 — Lifecycle and logs.** `spawn`, `kill`, `status`, `env`;
circular log buffer; `log_tail`, `log_head`, `log_range`, `log_grep`,
`log_clear`. Allowlist enforcement. This is enough to replace "tail -f"
for agents. `log_where` (structured filtering) is deferred to phase 3b
because it depends on the ludica-side JSON logger landing first.

**Phase 2b — Game-side JSON logging.** New `lud_log` API in
`src/ludica/` with a minimal JSON-escaping vararg writer (no
stdio/printf dependency). Convert existing `fprintf(stderr, ...)`
call sites. Adds `log_where` structured query on the MCP side once
output is guaranteed-structured.

**Phase 3 — Control fd.** `---controlfd=N` on the game side; launcher
opens socketpair and proxies the Control command subset. Game-side
implementation in `src/ludica/automation.c` (repurposed, mostly
simpler than today).

**Phase 4 — Bridge rewrite.** `tools/ludica-mcp-bridge/` replaces the
old stdio-JSON-RPC-to-TCP tool. Translation layer only; no state.

**Phase 5 — gdb affordances and crash capture.** `gdb_hint`,
`introspect`, `gdbserver` wrapping. Core-file location across distros
(`core_pattern`, `systemd-coredump`, `apport`, CWD). Automatic
`gdb_core_summary` on abnormal exit, with the full `gdb_core_*`
command family for follow-up. Skill doc covers the common `gdb -batch`
invocations.

**Phase 6 — Events and subscriptions.** `EVENT` line protocol,
`subscribe`/`unsubscribe`, `log_stdout_line` / `log_stderr_line` /
`process_exit` / `crash` / `crash_summary` events.

## Concurrency Model

Single-threaded, single-event-loop. `libiox` multiplexes:

- Listening socket (accept new bridges).
- One bridge socket per session (command reads, event writes).
- Two pipe fds per running game (stdout, stderr).
- One control-fd per running game (socketpair to in-process
  instrumentation).
- SIGCHLD via self-pipe (detect child exit, kick off core capture).
- Timers for command timeouts and deferred cleanup.

No mutexes. All state transitions happen in the dispatch loop. gdb
batch invocations (`gdb_core_summary`, `introspect`) run as blocking
subprocess calls with a short timeout. Game-side threads are gdb's
problem, not ours — a pre-canned `gdb_core_threads` action (maps to
`thread apply all bt`) is the right place to handle multi-threaded
backtraces if crashes in threaded code become common.

Sub-agent fan-out (Opus dispatches Haiku/Sonnet to collect frames or
bisect a crash) works naturally: each sub-agent's bridge opens its own
TCP connection and gets its own session. The launcher sees them as
independent clients and fires up independent game processes. Resource
exhaustion (too many concurrent games) is the user's problem to cap via
agent orchestration, not the launcher's.

## Open Questions

- **PCRE**: skip for v1. Revisit if POSIX ERE proves limiting.
- **`LUDICA_MCP_ALLOWGLOB`**: don't build until someone reaches for it.
- **Shared observation of one game**: multiple sessions watching the
  same game process. Unclear if needed; design has no path to it and
  we deliberately didn't try.
- **Structured log events**: if agents start parsing log output for
  event-like patterns, promote them to real `EVENT` lines.
- **`lud_run` integration**: should `---controlfd=` be parsed by
  `lud_run` itself or by a separate `lud__automation_init` called
  early? Lean toward the latter so non-ludica programs can use the
  same control-fd protocol.

## Relation to the Old System

The old `tools/ludica-mcp.c` (MCP JSON-RPC bridge) and
`src/ludica/automation.c` (in-process TCP server) will both be
replaced. A short transition period may keep the old MCP tools present
but marked deprecated in the skill documentation, so existing agent
sessions don't break.

`automation.md` describes the legacy protocol and will be removed once
the new system is in use.
