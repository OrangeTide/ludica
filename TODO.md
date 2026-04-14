# TODO

Usage:
- Check items off as they are completed and move them to # DONE.
- Commit this file with the related changes.
- Agents don't need to scan past the DONE section. can read this file with this command: `sed '/^# DONE/q' TODO.md`

## Next-term

## Future

## Research Items

## See Also

There are some sub-project TODO.md in this project too.

  * lilpc ̣– [samples/lilpc/TODO.md]

# DONE

- [x] ludica should offer a loading screen and progress meter as games initialize. hero's assets already create a few seconds of delay on start. -- resolved: new lud_draw_progress() does the job.
- [x] move all non-ludica components out of src and into samples. this will make it easier to vendor ludica if only the real library's files are in src/ and directories like samples/ and assets/ can be ignored. -- resolved: hero, demos, ansiview moved to samples/
- [x] clean up unused code and directories. (tiny/, src/attic/) -- resolved: src/attic/ and src/demo4/ deleted
- [X] why tools and scripts? -- resolved: tools = build tools, scripts = project maintenance
