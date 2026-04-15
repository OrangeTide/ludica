EXECUTABLES += demo06_slugtext
demo06_slugtext_DIR  := $(dir $(lastword $(MAKEFILE_LIST)))
demo06_slugtext_SRCS  = demo06_slugtext.c
demo06_slugtext_LIBS = ludica
