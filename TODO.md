# TODO

Usage:
- Check items off as they are completed and move them to # DONE.
- Commit this file with the related changes.
- Agents don't need to scan past the DONE section. can read this file with this command: `sed '/^# DONE/q' TODO.md`

## Near-term

- [ ] ludica-mcp - the agents struggle to use it. multiple failed attempts to start it. problems with duplicate processes. using manual kill signals. requires a lot of approvals to run netcat (nc) commands. no help command but the agents often request it.
  - [ ] add HELP command to list every command with a 1-line summary. and HELP <command> to give a more detailed info on on a particular command. (1 paragraph is fine)
  - [X] add QUIT command to terminate
  - [ ] add RESTART command that does execv() on itself. so that if a new executable is built it can start again. hidden command-line option ---listenfd=%d preserves the listenfd. (since SOCK_CLOEXEC wasn't used this should work)
  - [ ] automatically terminate program after the first connection closes. (add a NOKILL command to disable this behavior)
  - [ ] use SO_REUSEADDR for listen socket so the most recently ran instance "wins" and duplicates are less of an issue.
  - [ ] can we interface it more easily than calling nc over and over again, how can we streamline an agent's access to this MCP?
    - from one session: ludica-mcp tools weren't available as MCP tools in this session — I had to fall back to raw TCP commands (STEP, CAPSCREEN, QUIT). The MCP server may need a session restart to pick up.
  - [ ] some agents have trouble with the format or status of the SCREENSHOT command. what could be wrong?
  - [ ] add some reporting message to the commands so that success or failure are obvious to the agent. this might help with the screenshot, to know where the screenshot is saved?
  - [ ] update the MCP skill for all of the above. basically we need to make the agents use the MCP and use it effectively.

## Future

- [ ] gamepad support on all architectures. improved to work with our binding system. also bind joystick axis
- [ ] in game GUI. (TBD: cimgui or custom immediate mode)
- [ ] a small top-down game. collect lots of items
- [ ] improve ludica-mcp
  - a common pattern is to --pause then skip N frames. might be easier to have a built-in skip on the command-line.

## 3D Engine Support

Features identified from hero Gen2 analysis. See `doc/notes/ludica-engine-features.md`
for full design rationale and API sketches.

- [ ] job system (`ludica_job.h`) — work queue with deferred completion callbacks.
  workers on native, inline fallback on WASM. app never writes threading code.
  - `lud_job_submit(func, arg, on_complete)` — func on worker, on_complete on main thread
  - job groups for bulk loading with `lud_draw_progress` integration
  - WASM: jobs run inline during `lud_job_poll` with per-frame time budget
- [ ] arena allocator — bump allocator for per-job scratch, per-frame temporaries,
  procgen buffers. bulk reset, no per-object free.
  - reference: `DEVEL/osdev/cmd/jsh/jsh.c:533-647` (hybrid bump/malloc strategy)
- [ ] texture arrays (`GL_TEXTURE_2D_ARRAY`, GLES3) — bind once, index by layer.
  eliminates per-material texture switching.
- [ ] mesh update — `lud_update_mesh()` for partial VBO writes (streaming world geometry)
- [ ] instanced drawing — `lud_draw_instanced()` for repeated kit-piece meshes (GLES3)
- [ ] frustum culling utilities — `lud_frustum_extract()`, `lud_frustum_test_aabb()`
- [ ] deferred resource destruction — queue GL deletes to end of frame for safe streaming
- [ ] collision primitives (`ludica_phys.h`) — swept capsule vs. planes/convex shapes,
  wall sliding. develop in hero first, extract when API stabilizes.

## Future

- [ ] font rendering: SDF, MSDF (multichannel signed distance field), or Slug
- [ ] networking / multiplayer. 
  - I have an very simple IDL compiler for doing binary protocols.
  - native and wasm games should be able to play together
  - open question: how do we connect players together. a lobby server + game server seems sensible. will wasm be able to attach to it?

## Research Items

## See Also

There are some sub-project TODO.md in this project too.

  * lilpc – [samples/lilpc/TODO.md]
  * hero – [samples/hero/TODO.md]

# DONE

- [x] ludica should offer a loading screen and progress meter as games initialize. hero's assets already create a few seconds of delay on start. -- resolved: new lud_draw_progress() does the job.
- [x] move all non-ludica components out of src and into samples. this will make it easier to vendor ludica if only the real library's files are in src/ and directories like samples/ and assets/ can be ignored. -- resolved: hero, demos, ansiview moved to samples/
- [x] clean up unused code and directories. (tiny/, src/attic/) -- resolved: src/attic/ and src/demo4/ deleted
- [X] why tools and scripts? -- resolved: tools = build tools, scripts = project maintenance
- [x] is there anything we should import from /home/jon/jondev.local/pix ? -- resolved: gamepad code already imported and improved in ludica. atlas, sprite, shader, keyboard all superseded by ludica's equivalents.
- [x] is there anything we should import from /home/jon/jondev.local/emu1 ? -- resolved: fantasy console (Z80/6502 + TMS9918A) on Raylib. Different scope from lilpc (x86) and ludica. Nothing to import.
- [x] is there anything we should import from /home/jon/jondev.local/midge ? -- resolved: earlier GL windowing framework (2019-2020), desktop GL + X11 only. Fully superseded by ludica — no gamepad, audio, sprites, fonts, GLES, or Emscripten support.
