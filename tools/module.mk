EXECUTABLES += ludica-mcp
ludica-mcp_DIR  := $(dir $(lastword $(MAKEFILE_LIST)))
ludica-mcp_SRCS  = ludica-mcp.c
ludica-mcp_CPPFLAGS = -I$(ludica-mcp_DIR)../src/thirdparty
