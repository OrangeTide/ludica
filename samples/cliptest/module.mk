EXECUTABLES += cliptest
cliptest_DIR  := $(dir $(lastword $(MAKEFILE_LIST)))
cliptest_SRCS  = cliptest.c
cliptest_CPPFLAGS = -Isrc/include
cliptest_LIBS = ludica
cliptest_LDLIBS = -lX11

# Clipboard copy/paste round-trip self-check; needs an X11 display
# (xvfb on headless). Opens a second X connection to act as another app.
define cliptest_TESTCMD
$(cliptest_EXEC) --selftest
endef
TEST_TARGETS += cliptest
