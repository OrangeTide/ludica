EXECUTABLES += demo03_text_dialogs
demo03_text_dialogs_DIR  := $(dir $(lastword $(MAKEFILE_LIST)))
demo03_text_dialogs_SRCS  = demo03_text_dialogs.c
demo03_text_dialogs_LIBS = ludica
demo03_text_dialogs_LDLIBS.Emscripten = -sEVAL_CTORS
