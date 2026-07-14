EXECUTABLES += dndtest
dndtest_DIR  := $(dir $(lastword $(MAKEFILE_LIST)))
dndtest_SRCS  = dndtest.c
dndtest_CPPFLAGS = -Isrc/include
dndtest_LIBS = ludica
dndtest_LDLIBS = -lX11

# XDND drop self-test; drives a synthetic drag source over a second X
# connection, so it needs an X11 display (xvfb on headless).
define dndtest_TESTCMD
$(dndtest_EXEC) --selftest
endef
TEST_TARGETS += dndtest
