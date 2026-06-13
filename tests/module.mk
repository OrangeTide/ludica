EXECUTABLES += renderstate_test
renderstate_test_DIR  := $(dir $(lastword $(MAKEFILE_LIST)))
renderstate_test_SRCS  = renderstate_test.c
renderstate_test_LIBS  = ludica

# Needs an X11 display; run under xvfb-run on a headless box.
define renderstate_test_TESTCMD
$(renderstate_test_EXEC)
endef
TEST_TARGETS += renderstate_test

EXECUTABLES += meshupdate_test
meshupdate_test_DIR  := $(dir $(lastword $(MAKEFILE_LIST)))
meshupdate_test_SRCS  = meshupdate_test.c
meshupdate_test_LIBS  = ludica

# Needs an X11 display; run under xvfb-run on a headless box.
define meshupdate_test_TESTCMD
$(meshupdate_test_EXEC)
endef
TEST_TARGETS += meshupdate_test

# Pure CPU test; no display needed.
EXECUTABLES += arena_test
arena_test_DIR  := $(dir $(lastword $(MAKEFILE_LIST)))
arena_test_SRCS  = arena_test.c
arena_test_LIBS  = ludica

define arena_test_TESTCMD
$(arena_test_EXEC)
endef
TEST_TARGETS += arena_test

EXECUTABLES += instanced_test
instanced_test_DIR  := $(dir $(lastword $(MAKEFILE_LIST)))
instanced_test_SRCS  = instanced_test.c
instanced_test_LIBS  = ludica

# Needs an X11 display; run under xvfb-run on a headless box.
define instanced_test_TESTCMD
$(instanced_test_EXEC)
endef
TEST_TARGETS += instanced_test
