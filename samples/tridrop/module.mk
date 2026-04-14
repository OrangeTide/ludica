EXECUTABLES += tridrop
tridrop_DIR  := $(dir $(lastword $(MAKEFILE_LIST)))
tridrop_SRCS  = tridrop.c
tridrop_LIBS = ludica
tridrop_LDLIBS.Emscripten = -sEVAL_CTORS
