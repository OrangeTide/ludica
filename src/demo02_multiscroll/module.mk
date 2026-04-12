EXECUTABLES += demo02_multiscroll
demo02_multiscroll_DIR  := $(dir $(lastword $(MAKEFILE_LIST)))
demo02_multiscroll_SRCS  = demo02_multiscroll.c
demo02_multiscroll_CFLAGS = -Wall -W -O2 -g
demo02_multiscroll_LIBS = initgl
