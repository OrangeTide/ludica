EXECUTABLES += demo02_multiscroll
demo02_multiscroll_DIR  := $(dir $(lastword $(MAKEFILE_LIST)))
demo02_multiscroll_SRCS  = demo02_multiscroll.c
demo02_multiscroll_LIBS = ludica
demo02_multiscroll_LDLIBS.Emscripten = -sEVAL_CTORS
demo02_multiscroll_LDFLAGS.Emscripten = --preload-file $(demo02_multiscroll_DIR)assets@samples/demo02_multiscroll/assets
