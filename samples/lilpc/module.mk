EXECUTABLES += lilpc
lilpc_DIR  := $(dir $(lastword $(MAKEFILE_LIST)))
lilpc_SRCS  = lilpc.c cpu286.c bus.c chipset.c video.c kbd.c fdc.c serial.c parallel.c speaker.c
lilpc_LIBS = ludica
lilpc_LDLIBS.Emscripten = -sEVAL_CTORS
lilpc_LDFLAGS.Emscripten = --preload-file $(lilpc_DIR)bios/pcxtbios.bin@bios/pcxtbios.bin --preload-file $(lilpc_DIR)disk/demodisk.img@disk/demodisk.img --pre-js $(lilpc_DIR)emscripten_args.js
