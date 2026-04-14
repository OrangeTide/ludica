EXECUTABLES += lilpc
lilpc_DIR  := $(dir $(lastword $(MAKEFILE_LIST)))
lilpc_SRCS  = lilpc.c cpu286.c bus.c chipset.c video.c kbd.c fdc.c serial.c parallel.c speaker.c debugmon.c
lilpc_LIBS = ludica
lilpc_LDLIBS.Emscripten = -sEVAL_CTORS -sEXPORTED_RUNTIME_METHODS=ccall -sEXPORTED_FUNCTIONS=_main,_malloc,_free
lilpc_LDFLAGS.Emscripten = --preload-file $(lilpc_DIR)bios/pcxtbios.bin@bios/pcxtbios.bin --pre-js $(lilpc_DIR)emscripten_args.js
