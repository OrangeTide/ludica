LIBRARIES += ludica
ludica_DIR  := $(dir $(lastword $(MAKEFILE_LIST)))
ludica_SRCS  = app.c args.c event.c timing.c log.c input.c action.c gamepad.c shader.c mesh.c texture.c framebuffer.c image.c sprite.c font.c progress.c anim.c
ludica_GENERATED_SRCS = shaders/framebuffer.c shaders/sprite.c
ludica_CPPFLAGS = -I$(ludica_DIR)include -I$(ludica_DIR)../thirdparty -DLUD_VERSION='"$(shell cat $(ludica_DIR)../../VERSION 2>/dev/null || echo unknown)"'
ludica_EXPORTED_CPPFLAGS = -I$(ludica_DIR)include -I$(ludica_DIR)

### Linux
ludica_SRCS.Linux = platform_x11.c linux/gamepad-linux.c
ludica_EXPORTED_LDLIBS.Linux = -lm -ldl -lEGL -lGLESv2 -lX11

### Windows (MSYS2/Cygwin uname -s reports something containing Windows_NT)
ludica_SRCS.Windows_NT = win32/window.c win32/gamepad-windows.c
ludica_CPPFLAGS.Windows_NT = -I$(ludica_DIR)win32/include
ludica_CFLAGS.Windows_NT = -mwin32
ludica_EXPORTED_CPPFLAGS.Windows_NT = -I$(ludica_DIR)win32/include
ludica_EXPORTED_LDLIBS.Windows_NT = -lgdi32 -lwinmm -lEGL -lGLESv2 -lXinput
ludica_EXPORTED_LDFLAGS.Windows_NT = -L$(ludica_DIR)win32libs
