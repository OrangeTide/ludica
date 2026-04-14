EXECUTABLES += demo05_audio
demo05_audio_DIR  := $(dir $(lastword $(MAKEFILE_LIST)))
demo05_audio_SRCS  = demo05_audio.c
demo05_audio_LIBS = ludica
demo05_audio_LDLIBS.Emscripten = -sEVAL_CTORS
