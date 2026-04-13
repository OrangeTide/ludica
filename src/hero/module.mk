EXECUTABLES += hero
hero_DIR  := $(dir $(lastword $(MAKEFILE_LIST)))
hero_SRCS  = hero.c
hero_CPPFLAGS = -Isrc/include
hero_LIBS = ludica
