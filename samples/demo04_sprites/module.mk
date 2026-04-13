EXECUTABLES += demo04_sprites
demo04_sprites_DIR  := $(dir $(lastword $(MAKEFILE_LIST)))
demo04_sprites_SRCS  = demo04_sprites.c
demo04_sprites_LIBS = ludica
