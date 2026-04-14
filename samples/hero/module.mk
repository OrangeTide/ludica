EXECUTABLES += hero
hero_DIR  := $(dir $(lastword $(MAKEFILE_LIST)))
hero_SRCS  = hero.c
hero_GENERATED_SRCS = shaders/portal.c
hero_CPPFLAGS = -Isrc/include
hero_LIBS = ludica
hero_LDLIBS.Emscripten = -sASYNCIFY
