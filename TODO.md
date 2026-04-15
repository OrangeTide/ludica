# TODO

Usage:
- Check items off as they are completed and move them to # DONE.
- Commit this file with the related changes.
- Agents don't need to scan past the DONE section. can read this file with this command: `sed '/^# DONE/q' TODO.md`

## Next-term

- [ ] gamepad support on all architectures. improved to work with our binding system. also bind joystick axis
- [ ] in game GUI. (TBD: cimgui or custom immediate mode)
- [ ] a small top-down game. collect lots of items
- [ ] improve ludica-mcp
  - better skill information. I think some of the commands for --auto-port are not written correctly.
  - add a kill command so the agent doesn't need to keep asking me to run bash commands to kill processes.
  - a common pattern is to --pause then skip N frames. might be easier to have a built-in skip on the command-line.
  - set SO_REUSEADDR on the listen address. having to fight dead sessions blocking us has wasted too much time.

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
