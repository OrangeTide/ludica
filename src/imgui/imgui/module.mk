LIBRARIES += imgui
imgui_DIR  := $(dir $(lastword $(MAKEFILE_LIST)))
imgui_SRCS  = *.cpp
imgui_CXXFLAGS = -Wall -W -O2 -g
imgui_CPPFLAGS = -I$(imgui_DIR) -I$(imgui_DIR)backends
imgui_EXPORTED_CPPFLAGS = -I$(imgui_DIR) -I$(imgui_DIR)backends

### Linux
imgui_SRCS.Linux = backends/imgui_impl_sdl2.cpp backends/imgui_impl_opengl3.cpp
imgui_CXXFLAGS.Linux = $(shell sdl2-config --cflags) $(shell pkg-config --cflags gl) -flto=auto
imgui_EXPORTED_LDLIBS.Linux = $(shell sdl2-config --libs) $(shell pkg-config --libs gl) -lm
imgui_EXPORTED_LDFLAGS.Linux = -flto=auto

### Windows
imgui_SRCS.Windows_NT = backends/imgui_impl_win32.cpp backends/imgui_impl_dx10.cpp
imgui_CXXFLAGS.Windows_NT = -mwin32
imgui_EXPORTED_LDLIBS.Windows_NT = -lgdi32 -lwinmm -lEGL -lGLESv2 -lXinput
