EXECUTABLES += ansiview
ansiview_DIR  := $(dir $(lastword $(MAKEFILE_LIST)))
ansiview_SRCS  = ansiview.c
ansiview_LIBS = ludica
ansiview_LDLIBS.Emscripten = -sEVAL_CTORS
ansiview_LDFLAGS.Emscripten = --preload-file $(ansiview_DIR)assets@samples/ansiview/assets
