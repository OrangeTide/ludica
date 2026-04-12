LIBRARIES += initgl
initgl_DIR  := $(dir $(lastword $(MAKEFILE_LIST)))
initgl_SRCS  = *.c
initgl_CFLAGS = -Wall -W -O2 -g
initgl_CPPFLAGS = -I$(initgl_DIR)include
initgl_EXPORTED_CPPFLAGS = -I$(initgl_DIR)include -I$(initgl_DIR)

### Linux
initgl_SRCS.Linux = unix/*.c linux/*.c
initgl_CPPFLAGS.Linux = -I$(initgl_DIR)unix/include
initgl_CFLAGS.Linux = -flto=auto
initgl_EXPORTED_CPPFLAGS.Linux = -I$(initgl_DIR)unix/include
initgl_EXPORTED_LDLIBS.Linux = -lm -ldl -lEGL -lGLESv2 -lX11
initgl_EXPORTED_LDFLAGS.Linux = -flto=auto

### Windows (MSYS2/Cygwin uname -s reports something containing Windows_NT)
initgl_SRCS.Windows_NT = win32/*.c
initgl_CPPFLAGS.Windows_NT = -I$(initgl_DIR)win32/include
initgl_CFLAGS.Windows_NT = -mwin32
initgl_EXPORTED_CPPFLAGS.Windows_NT = -I$(initgl_DIR)win32/include
initgl_EXPORTED_LDLIBS.Windows_NT = -lgdi32 -lwinmm -lEGL -lGLESv2 -lXinput
initgl_EXPORTED_LDFLAGS.Windows_NT = -L$(initgl_DIR)win32libs
