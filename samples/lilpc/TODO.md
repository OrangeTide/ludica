# TODO

Usage:
- Check items off as they are completed and move them to # DONE.
- Commit this file with the related changes.
- Agents don't need to scan past the DONE section. can read this file with this command: `sed '/^# DONE/q' TODO.md`

## Next-term

- [ ] implement and use [Bochs 0xE9 port hack](https://bochs.sourceforge.io/doc/docbook/user/bochsrc.html#AEN2509) to report status in our exerciser.
- [ ] 286 Instruction exerciser.
  - assembler file(s) that tests every instruction. writes results to I/O port (e9 hack)
  - used in place of ROM BIOS during development.
  - [ ] prioritize 8086/80186 instructions first.
  - [ ] work on 286 protected-mode exerciser at a later phase
- [ ] more accurate CGA rendering.
  - [ ] snow emulation. real snow might be a little difficult because it relates to multiple components competing for the bus access. see https://www.reenigne.org/blog/the-cga-wait-states/  ... I'm thinking a scoreboard or time-oriented bit map that indicates if memory bus was accessed by the CPU during a particular CGA clock. we could fake it reasonably by passing the scoreboard a frame late to the shader and I don't think anyone will notice that we're a frame behind with our snow.

## Future

- [ ] MCGA 256-color mode
- [ ] Tandy sound chip (MAYBE)
- [ ] CGA composite-mode shader. a complete cga.glsl shader that has parameters related to CGA's control modes. by default start as if on a composite (not RGB) monitor. and properly handle color burst mode on/off and [artifact colors](https://en.wikipedia.org/wiki/Composite_artifact_colors#PC_compatibles_with_CGA_graphic_cards)
- [ ] Ethernet IPX networking over virtual IPX switch server that people can share.
  - [ ] easy model, use UDP-based daemon at a known location that bridges IPX tunnels together like (RFC 1234](https://datatracker.ietf.org/doc/html/rfc1234)
- [ ] Covox Speech Thing / Disney Sound Source. with DSS being a bit less finicky but fixed and very limited sample rate. Covox could theoretically offer a very high sampling rate, but in practice it tended to be a CPU hog
## Research Items

- [ ] can we use option ROM to hold emulator specific hardware and extensions?
  - [ ] mouse driver
  - [ ] cd-rom driver MSCDEX
  - [ ] host file system driver (appears like a cd-rom driver or networked file system). mounted on boot or perhaps with `NET USE` type utility?
- [ ] network card. pick one of these that is period correct, few HW bugs, and very easy to emulate. 3c503, NE1000, NE2000, AMD LANCE, are places we can start. NE1000 came out in 1987. At that time a 286 AT would have still been a mid-range PC option, with the 286 XT falling behind AT clones due to a similar price with less compatibility and performance.
  - [ ] then we can try out mTCP on our system. https://www.brutman.com/mTCP/mTCP.htm -- we should pick a network card it supports.

# DONE
