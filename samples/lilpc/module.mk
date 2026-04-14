EXECUTABLES += lilpc
lilpc_DIR  := $(dir $(lastword $(MAKEFILE_LIST)))
lilpc_SRCS  = lilpc.c cpu286.c bus.c chipset.c video.c kbd.c fdc.c serial.c parallel.c speaker.c
lilpc_LIBS = ludica
