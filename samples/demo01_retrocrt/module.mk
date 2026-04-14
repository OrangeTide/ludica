EXECUTABLES += demo01_retrocrt
demo01_retrocrt_DIR  := $(dir $(lastword $(MAKEFILE_LIST)))
demo01_retrocrt_SRCS  = demo01_retrocrt.c
demo01_retrocrt_LIBS = ludica
demo01_retrocrt_LDLIBS.Emscripten = -sEVAL_CTORS
