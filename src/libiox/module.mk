# src/libiox/module.mk -- poll()-based I/O multiplexer
# Skip on emscripten; libiox uses poll/signals/self-pipe which don't
# make sense in a WebAssembly single-threaded main loop.
ifeq ($(findstring emscripten,$(TARGET_TRIPLET)),)

iox_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
iox_SRCS = iox_loop.c iox_fd.c iox_signal.c iox_timer.c
iox_EXPORTED_CPPFLAGS = -I$(iox_DIR)
LIBRARIES += iox

endif
