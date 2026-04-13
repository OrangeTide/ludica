LIBRARIES += lithos
lithos_DIR  := $(dir $(lastword $(MAKEFILE_LIST)))
lithos_SRCS  = app.c event.c timing.c log.c input.c gamepad.c shader.c mesh.c texture.c framebuffer.c image.c sprite.c font.c
lithos_CPPFLAGS = -I$(lithos_DIR)include -I$(lithos_DIR)../thirdparty
lithos_EXPORTED_CPPFLAGS = -I$(lithos_DIR)include -I$(lithos_DIR)

### Linux
lithos_SRCS.Linux = platform_x11.c linux/gamepad-linux.c
lithos_EXPORTED_LDLIBS.Linux = -lm -ldl -lEGL -lGLESv2 -lX11

### Windows (MSYS2/Cygwin uname -s reports something containing Windows_NT)
lithos_SRCS.Windows_NT = win32/window.c win32/gamepad-windows.c
lithos_CPPFLAGS.Windows_NT = -I$(lithos_DIR)win32/include
lithos_CFLAGS.Windows_NT = -mwin32
lithos_EXPORTED_CPPFLAGS.Windows_NT = -I$(lithos_DIR)win32/include
lithos_EXPORTED_LDLIBS.Windows_NT = -lgdi32 -lwinmm -lEGL -lGLESv2 -lXinput
lithos_EXPORTED_LDFLAGS.Windows_NT = -L$(lithos_DIR)win32libs
