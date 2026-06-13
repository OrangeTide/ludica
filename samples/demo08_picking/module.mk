EXECUTABLES += demo08_picking
demo08_picking_DIR  := $(dir $(lastword $(MAKEFILE_LIST)))
demo08_picking_SRCS  = picking.c
demo08_picking_CPPFLAGS = -Isrc/include
demo08_picking_LIBS = ludica

# Color-id picking self-check; needs an X11 display (xvfb on headless).
define demo08_picking_TESTCMD
$(demo08_picking_EXEC) --selftest
endef
TEST_TARGETS += demo08_picking
