LIBRARIES += initgl
initgl_DIR  := $(dir $(lastword $(MAKEFILE_LIST)))
initgl_SRCS  = app.c event.c timing.c log.c input.c gamepad.c shader.c mesh.c texture.c framebuffer.c image.c sprite.c font.c
initgl_CPPFLAGS = -I$(initgl_DIR)include -I$(initgl_DIR)../thirdparty
initgl_EXPORTED_CPPFLAGS = -I$(initgl_DIR)include -I$(initgl_DIR)

### Linux
initgl_SRCS.Linux = platform_x11.c linux/gamepad-linux.c
initgl_EXPORTED_LDLIBS.Linux = -lm -ldl -lEGL -lGLESv2 -lX11

### Windows (MSYS2/Cygwin uname -s reports something containing Windows_NT)
initgl_SRCS.Windows_NT = win32/window.c win32/gamepad-windows.c
initgl_CPPFLAGS.Windows_NT = -I$(initgl_DIR)win32/include
initgl_CFLAGS.Windows_NT = -mwin32
initgl_EXPORTED_CPPFLAGS.Windows_NT = -I$(initgl_DIR)win32/include
initgl_EXPORTED_LDLIBS.Windows_NT = -lgdi32 -lwinmm -lEGL -lGLESv2 -lXinput
initgl_EXPORTED_LDFLAGS.Windows_NT = -L$(initgl_DIR)win32libs
