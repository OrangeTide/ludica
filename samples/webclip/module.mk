EXECUTABLES += webclip
webclip_DIR  := $(dir $(lastword $(MAKEFILE_LIST)))
webclip_SRCS  = webclip.c
webclip_LIBS = ludica
