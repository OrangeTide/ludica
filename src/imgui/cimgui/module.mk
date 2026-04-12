LIBRARIES += cimgui
cimgui_DIR  := $(dir $(lastword $(MAKEFILE_LIST)))
cimgui_SRCS  = *.cpp
cimgui_CXXFLAGS = -Wall -W -O2 -g
cimgui_CPPFLAGS = -I$(cimgui_DIR)../
cimgui_LIBS = imgui
cimgui_EXPORTED_CPPFLAGS = -I$(cimgui_DIR)../ -I$(cimgui_DIR)
