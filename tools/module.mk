# ludica-mcp uses TCP sockets -- skip on Emscripten
ifeq ($(findstring emscripten,$(TARGET_TRIPLET)),)
EXECUTABLES += ludica-mcp
ludica-mcp_DIR  := $(dir $(lastword $(MAKEFILE_LIST)))
ludica-mcp_SRCS  = ludica-mcp.c
ludica-mcp_CPPFLAGS = -I$(ludica-mcp_DIR)../src/thirdparty
endif

# font2slug -- offline TTF/OTF to .slugfont converter (skip on Emscripten)
ifeq ($(findstring emscripten,$(TARGET_TRIPLET)),)
EXECUTABLES += font2slug
font2slug_DIR  := $(dir $(lastword $(MAKEFILE_LIST)))
font2slug_SRCS  = font2slug.c
font2slug_CPPFLAGS = -I$(font2slug_DIR)../src/thirdparty -I$(font2slug_DIR)../src/ludica
font2slug_LDLIBS = -lm
endif
