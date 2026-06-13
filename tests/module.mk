EXECUTABLES += renderstate_test
renderstate_test_DIR  := $(dir $(lastword $(MAKEFILE_LIST)))
renderstate_test_SRCS  = renderstate_test.c
renderstate_test_LIBS  = ludica

# Needs an X11 display; run under xvfb-run on a headless box.
define renderstate_test_TESTCMD
$(renderstate_test_EXEC)
endef
TEST_TARGETS += renderstate_test
