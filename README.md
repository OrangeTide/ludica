# Ludica [![CI](https://github.com/OrangeTide/gamedev/workflows/CI/badge.svg)](https://github.com/OrangeTide/gamedev/actions)

A lightweight cross-platform graphics library for games and demos,
built on OpenGL ES 2.0.

Ludica provides a minimal C API for windowing, input, shaders, meshes,
textures, sprites, fonts, and framebuffer effects. Several demo programs
and a portal-based 3D engine (`hero`) are included.

## Programs

| Program | Description |
|---------|-------------|
| `hero` | Portal-based 3D engine with normal-mapped PBR textures |
| `demo01_retrocrt` | Palette framebuffer with CRT post-processing |
| `demo02_multiscroll` | Parallax scrolling and sprite batching |
| `demo03_text_dialogs` | Font rendering and dialog box UI |
| `ansiview` | ANSI art viewer |

## Building

Requires GCC (or MinGW-w64 on Windows) and GNU Make.

```sh
make                # default build
make RELEASE=1      # optimized (-O2, LTO)
make DEBUG=1        # debug symbols
```

Output goes to `_out/<triplet>/bin/` (e.g. `_out/x86_64-linux-gnu/bin/hero`).

Cross compile for Windows:

```sh
make CONFIG=configs/mingw32_config.mk
```

### Linux dependencies

Ubuntu / Debian:

```sh
sudo apt-get install -y build-essential git \
    libx11-dev libxext-dev libxfixes-dev libxi-dev \
    libxcursor-dev libegl1-mesa-dev libgles2-mesa-dev
```

### Windows dependencies

Install [MSYS2](https://www.msys2.org/) with MINGW64, or
[w64devkit](https://github.com/skeeto/w64devkit). Then fetch headers
and ANGLE libraries:

```sh
cd src/ludica
./download-headers.sh
cd win32libs
./update-binaries.sh
```

## Running

```sh
_out/x86_64-linux-gnu/bin/hero
```

## Directory layout

- `src/ludica/` -- Core library (platform, shaders, meshes, textures, sprites, fonts)
- `src/hero/` -- Portal-based 3D engine
- `src/demo01_retrocrt/` -- CRT post-processing demo
- `src/demo02_multiscroll/` -- Parallax scrolling demo
- `src/demo03_text_dialogs/` -- Font and dialog demo
- `src/ansiview/` -- ANSI art viewer
- `src/thirdparty/` -- Header-only dependencies (stb_image, stb_ds, miniaudio)
- `assets/textures/` -- PBR textures (CC0, see [CREDIT.md](CREDIT.md))
- `tools/` -- Build-time code generators

## License

This project is licensed under the [0BSD License](LICENSE).

## Acknowledgments

- [ambientCG](https://ambientcg.com/) -- CC0 PBR textures
- [Dear ImGui](https://github.com/ocornut/imgui) / [cimgui](https://github.com/cimgui/cimgui)
- [stb](https://github.com/nothings/stb) -- stb_image, stb_ds
- [HandmadeMath](https://github.com/HandmadeMath/HandmadeMath) -- single-header math library
- [miniaudio](https://miniaud.io/) -- single-header audio library
- [Using OpenGL ES on Windows via EGL](https://www.saschawillems.de/blog/2015/04/19/using-opengl-es-on-windows-desktops-via-egl/)
