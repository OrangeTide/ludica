EXECUTABLES += ansiview
ansiview_DIR  := $(dir $(lastword $(MAKEFILE_LIST)))
ansiview_SRCS  = ansiview.c
ansiview_CFLAGS = -Wall -W -O2 -g
ansiview_LIBS = initgl
