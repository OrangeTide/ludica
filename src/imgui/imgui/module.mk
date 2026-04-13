LIBRARIES += imgui
imgui_DIR  := $(dir $(lastword $(MAKEFILE_LIST)))
imgui_SRCS  = *.cpp
imgui_CPPFLAGS = -I$(imgui_DIR) -I$(imgui_DIR)backends
imgui_EXPORTED_CPPFLAGS = -I$(imgui_DIR) -I$(imgui_DIR)backends

### Linux
imgui_SRCS.Linux = backends/imgui_impl_opengl3.cpp
imgui_CPPFLAGS.Linux = -DIMGUI_IMPL_OPENGL_ES2
imgui_EXPORTED_LDLIBS.Linux = -lm

### Windows
imgui_SRCS.Windows_NT = backends/imgui_impl_win32.cpp backends/imgui_impl_dx10.cpp
imgui_CXXFLAGS.Windows_NT = -mwin32
imgui_EXPORTED_LDLIBS.Windows_NT = -lgdi32 -lwinmm -lEGL -lGLESv2 -lXinput
