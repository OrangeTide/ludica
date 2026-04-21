EXECUTABLES += demo07_vfont
demo07_vfont_DIR  := $(dir $(lastword $(MAKEFILE_LIST)))
demo07_vfont_SRCS  = demo07_vfont.c
demo07_vfont_LIBS = ludica
