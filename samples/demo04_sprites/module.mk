EXECUTABLES += demo04_sprites
demo04_sprites_DIR  := $(dir $(lastword $(MAKEFILE_LIST)))
demo04_sprites_SRCS  = demo04_sprites.c
demo04_sprites_LIBS = ludica
demo04_sprites_LDLIBS.Emscripten = -sEVAL_CTORS
demo04_sprites_LDFLAGS.Emscripten = --preload-file $(demo04_sprites_DIR)assets@samples/demo04_sprites/assets
