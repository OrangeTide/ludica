# ludica-mcp-bridge -- stdio MCP JSON-RPC to TCP launcher translator
ifeq ($(findstring emscripten,$(TARGET_TRIPLET)),)
EXECUTABLES += ludica-mcp-bridge
ludica-mcp-bridge_DIR  := $(dir $(lastword $(MAKEFILE_LIST)))
ludica-mcp-bridge_SRCS  = bridge.c
ludica-mcp-bridge_CPPFLAGS = -I$(ludica-mcp-bridge_DIR)../../src/thirdparty
endif
