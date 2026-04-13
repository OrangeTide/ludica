# Game Dev experiments [![CI](https://github.com/OrangeTide/gamedev/workflows/CI/badge.svg)](https://github.com/OrangeTide/gamedev/actions)

Collection of my studies in game development.

## Description

## Getting Started

## Installation & Development

### Dependencies

  * Linux x86-64/ARM64 or Windows x86-64
  * GCC or MSYS2/[MinGW-w64](https://www.mingw-w64.org/) or equivalent
  * GNU Make
  * If building on Windows:
    * MinGW-w64 or [w64devkit](https://github.com/skeeto/w64devkit)
    * ANGLE libaries - see `update-binaries.sh`
    * [DirectX End-User Runtime Web Installer](https://answers.microsoft.com/en-us/windows/forum/all/xinput13dll-is-missing/b6566b88-b8dd-4dd3-abc5-2efac787a93a)

#### Ubuntu 22.04 x86-64 / Raspbian (64-bit aarch64)

  ```sh
  sudo apt-get install -y build-essential git cmake
  sudo apt-get install -y libx11-dev libxext-dev libxfixes-dev libxi-dev \
      libxcursor-dev libgl1-mesa-dev libglfw3-dev libasound2-dev
  ```

#### Windows x86-64

Install MSYS2 and MINGW64.

Install headers and libraries, from MSYS2 shell:

  ```sh
  cd src/ludica
  download-headers.sh
  cd win32libs
  update-binaries.sh
  ```

### Building

  * Run `make`

Cross compile:

  ```sh
  make CONFIG=configs/mingw32_config.mk
  ```

## Running

  * Run `./bin/Linux-aarch64/demo4` or any of the other built programs.

## Status & Known Bugs

Win32 support is incomplete. The following are missing or broken:

  * Empty window. nothing sems to render.
  * key event callbacks are not being called.

## Help

Create a new issue at https://github.com/OrangeTide/gamedev/issues

## License

This project is licensed under the [0BSD License](LICENSE)

## Acknowledgments

  * [Dear ImGui](https://github.com/ocornut/imgui)
  * [cimgui](https://github.com/cimgui/cimgui)
  * [bq_websocket](https://github.com/bqqbarbhg/bq_websocket)
  * [native-activity example](https://github.com/android/ndk-samples/tree/main/native-activity)
  * [stb](https://github.com/nothings/stb)
  * [Chromium browser compiled with the Clang/LLVM compiler](https://github.com/RobRich999/Chromium_Clang)
  * [Using OpenGL ES on windows desktops via EGL](https://www.saschawillems.de/blog/2015/04/19/using-opengl-es-on-windows-desktops-via-egl/)
