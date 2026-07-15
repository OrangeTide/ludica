EXECUTABLES += dragtest
dragtest_DIR  := $(dir $(lastword $(MAKEFILE_LIST)))
dragtest_SRCS  = dragtest.c
dragtest_CPPFLAGS = -Isrc/include
dragtest_LIBS = ludica
dragtest_LDLIBS = -lX11

# XDND drag-source self-test; drives a synthetic drop target over a second X
# connection, so it needs an X11 display (xvfb on headless).
define dragtest_TESTCMD
$(dragtest_EXEC) --selftest
endef
TEST_TARGETS += dragtest
