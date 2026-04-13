EXECUTABLES += ansiview
ansiview_DIR  := $(dir $(lastword $(MAKEFILE_LIST)))
ansiview_SRCS  = ansiview.c
ansiview_LIBS = lithos
