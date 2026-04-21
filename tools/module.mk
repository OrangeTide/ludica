# ludica-mcp uses TCP sockets -- skip on Emscripten
ifeq ($(findstring emscripten,$(TARGET_TRIPLET)),)
EXECUTABLES += ludica-mcp
ludica-mcp_DIR  := $(dir $(lastword $(MAKEFILE_LIST)))
ludica-mcp_SRCS  = ludica-mcp.c
ludica-mcp_CPPFLAGS = -I$(ludica-mcp_DIR)../src/thirdparty
endif

# ludica-launcher -- TCP daemon for game process management (skip on Emscripten)
ifeq ($(findstring emscripten,$(TARGET_TRIPLET)),)
EXECUTABLES += ludica-launcher
ludica-launcher_DIR  := $(dir $(lastword $(MAKEFILE_LIST)))
ludica-launcher_SRCS  = ludica-launcher.c
ludica-launcher_LIBS  = iox
endif

SUBDIRS += ludica-mcp-bridge

# font2slug -- offline TTF/OTF to .slugfont converter (skip on Emscripten)
ifeq ($(findstring emscripten,$(TARGET_TRIPLET)),)
EXECUTABLES += font2slug
font2slug_DIR  := $(dir $(lastword $(MAKEFILE_LIST)))
font2slug_SRCS  = font2slug.c
font2slug_CPPFLAGS = -I$(font2slug_DIR)../src/thirdparty -I$(font2slug_DIR)../src/ludica
font2slug_LDLIBS = -lm
endif

# font2msdf -- offline TTF/OTF to .msdffont converter (skip on Emscripten)
ifeq ($(findstring emscripten,$(TARGET_TRIPLET)),)
EXECUTABLES += font2msdf
font2msdf_DIR  := $(dir $(lastword $(MAKEFILE_LIST)))
font2msdf_SRCS  = font2msdf.c
font2msdf_CPPFLAGS = -I$(font2msdf_DIR)../src/thirdparty -I$(font2msdf_DIR)../src/ludica
font2msdf_LDLIBS = -lm
endif
