#!/bin/bash
set -xe

# IMGUI_VERSION=1.66.2
# CIMGUI_VERSION=1.66.2
IMGUI_VERSION=1.90.8-docking
CIMGUI_VERSION=1.90.8dock

# ImGUI
mkdir -p imgui ; cd imgui
curl \
	-O https://raw.githubusercontent.com/ocornut/imgui/v${IMGUI_VERSION}/imgui.cpp \
	-O https://raw.githubusercontent.com/ocornut/imgui/v${IMGUI_VERSION}/imgui_demo.cpp \
	-O https://raw.githubusercontent.com/ocornut/imgui/v${IMGUI_VERSION}/imgui_draw.cpp \
	-O https://raw.githubusercontent.com/ocornut/imgui/v${IMGUI_VERSION}/imgui_tables.cpp \
	-O https://raw.githubusercontent.com/ocornut/imgui/v${IMGUI_VERSION}/imgui_widgets.cpp \
	-O https://raw.githubusercontent.com/ocornut/imgui/v${IMGUI_VERSION}/imconfig.h \
	-O https://raw.githubusercontent.com/ocornut/imgui/v${IMGUI_VERSION}/imgui.h \
	-O https://raw.githubusercontent.com/ocornut/imgui/v${IMGUI_VERSION}/imgui_internal.h \
	-O https://raw.githubusercontent.com/ocornut/imgui/v${IMGUI_VERSION}/imstb_rectpack.h \
	-O https://raw.githubusercontent.com/ocornut/imgui/v${IMGUI_VERSION}/imstb_textedit.h \
	-O https://raw.githubusercontent.com/ocornut/imgui/v${IMGUI_VERSION}/imstb_truetype.h

mkdir -p backends ; cd backends
curl \
	-O https://raw.githubusercontent.com/ocornut/imgui/v${IMGUI_VERSION}/backends/imgui_impl_allegro5.cpp \
	-O https://raw.githubusercontent.com/ocornut/imgui/v${IMGUI_VERSION}/backends/imgui_impl_android.cpp \
	-O https://raw.githubusercontent.com/ocornut/imgui/v${IMGUI_VERSION}/backends/imgui_impl_dx10.cpp \
	-O https://raw.githubusercontent.com/ocornut/imgui/v${IMGUI_VERSION}/backends/imgui_impl_dx11.cpp \
	-O https://raw.githubusercontent.com/ocornut/imgui/v${IMGUI_VERSION}/backends/imgui_impl_dx12.cpp \
	-O https://raw.githubusercontent.com/ocornut/imgui/v${IMGUI_VERSION}/backends/imgui_impl_dx9.cpp \
	-O https://raw.githubusercontent.com/ocornut/imgui/v${IMGUI_VERSION}/backends/imgui_impl_glfw.cpp \
	-O https://raw.githubusercontent.com/ocornut/imgui/v${IMGUI_VERSION}/backends/imgui_impl_glut.cpp \
	-O https://raw.githubusercontent.com/ocornut/imgui/v${IMGUI_VERSION}/backends/imgui_impl_opengl2.cpp \
	-O https://raw.githubusercontent.com/ocornut/imgui/v${IMGUI_VERSION}/backends/imgui_impl_opengl3.cpp \
	-O https://raw.githubusercontent.com/ocornut/imgui/v${IMGUI_VERSION}/backends/imgui_impl_sdl2.cpp \
	-O https://raw.githubusercontent.com/ocornut/imgui/v${IMGUI_VERSION}/backends/imgui_impl_sdl3.cpp \
	-O https://raw.githubusercontent.com/ocornut/imgui/v${IMGUI_VERSION}/backends/imgui_impl_sdlrenderer2.cpp \
	-O https://raw.githubusercontent.com/ocornut/imgui/v${IMGUI_VERSION}/backends/imgui_impl_sdlrenderer3.cpp \
	-O https://raw.githubusercontent.com/ocornut/imgui/v${IMGUI_VERSION}/backends/imgui_impl_vulkan.cpp \
	-O https://raw.githubusercontent.com/ocornut/imgui/v${IMGUI_VERSION}/backends/imgui_impl_wgpu.cpp \
	-O https://raw.githubusercontent.com/ocornut/imgui/v${IMGUI_VERSION}/backends/imgui_impl_win32.cpp \
	-O https://raw.githubusercontent.com/ocornut/imgui/v${IMGUI_VERSION}/backends/imgui_impl_allegro5.h \
	-O https://raw.githubusercontent.com/ocornut/imgui/v${IMGUI_VERSION}/backends/imgui_impl_android.h \
	-O https://raw.githubusercontent.com/ocornut/imgui/v${IMGUI_VERSION}/backends/imgui_impl_dx10.h \
	-O https://raw.githubusercontent.com/ocornut/imgui/v${IMGUI_VERSION}/backends/imgui_impl_dx11.h \
	-O https://raw.githubusercontent.com/ocornut/imgui/v${IMGUI_VERSION}/backends/imgui_impl_dx12.h \
	-O https://raw.githubusercontent.com/ocornut/imgui/v${IMGUI_VERSION}/backends/imgui_impl_dx9.h \
	-O https://raw.githubusercontent.com/ocornut/imgui/v${IMGUI_VERSION}/backends/imgui_impl_glfw.h \
	-O https://raw.githubusercontent.com/ocornut/imgui/v${IMGUI_VERSION}/backends/imgui_impl_glut.h \
	-O https://raw.githubusercontent.com/ocornut/imgui/v${IMGUI_VERSION}/backends/imgui_impl_metal.h \
	-O https://raw.githubusercontent.com/ocornut/imgui/v${IMGUI_VERSION}/backends/imgui_impl_opengl2.h \
	-O https://raw.githubusercontent.com/ocornut/imgui/v${IMGUI_VERSION}/backends/imgui_impl_opengl3.h \
	-O https://raw.githubusercontent.com/ocornut/imgui/v${IMGUI_VERSION}/backends/imgui_impl_opengl3_loader.h \
	-O https://raw.githubusercontent.com/ocornut/imgui/v${IMGUI_VERSION}/backends/imgui_impl_osx.h \
	-O https://raw.githubusercontent.com/ocornut/imgui/v${IMGUI_VERSION}/backends/imgui_impl_sdl2.h \
	-O https://raw.githubusercontent.com/ocornut/imgui/v${IMGUI_VERSION}/backends/imgui_impl_sdl3.h \
	-O https://raw.githubusercontent.com/ocornut/imgui/v${IMGUI_VERSION}/backends/imgui_impl_sdlrenderer2.h \
	-O https://raw.githubusercontent.com/ocornut/imgui/v${IMGUI_VERSION}/backends/imgui_impl_sdlrenderer3.h \
	-O https://raw.githubusercontent.com/ocornut/imgui/v${IMGUI_VERSION}/backends/imgui_impl_vulkan.h \
	-O https://raw.githubusercontent.com/ocornut/imgui/v${IMGUI_VERSION}/backends/imgui_impl_wgpu.h \
	-O https://raw.githubusercontent.com/ocornut/imgui/v${IMGUI_VERSION}/backends/imgui_impl_win32.h

cd ..

sed -i -e 's@^//#define IMGUI_DISABLE_DEMO_WINDOWS@#define IMGUI_DISABLE_DEMO_WINDOWS@' imconfig.h
touch imconfig.h

cd ..

# cimgui
mkdir -p cimgui ; cd cimgui
curl -O https://raw.githubusercontent.com/cimgui/cimgui/${CIMGUI_VERSION}/cimgui.cpp \
	-O https://raw.githubusercontent.com/cimgui/cimgui/${CIMGUI_VERSION}/cimgui.h
cd ..
