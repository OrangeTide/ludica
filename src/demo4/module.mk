EXECUTABLES += demo4
demo4_DIR  := $(dir $(lastword $(MAKEFILE_LIST)))
demo4_SRCS  = demo4.c
demo4_CFLAGS = -Wall -W -O2 -g
demo4_LIBS = initgl
