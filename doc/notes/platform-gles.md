# Platform GLES Support

How ludica's GLES2/3 context is obtained on each target OS, and the plan
for filling in the macOS gap.

## Goal

One shader path, one GL call path. Games write `#version 100` or
`#version 300 es` shaders and use GLES2/3 entry points; the platform
layer is responsible for producing a conformant context on the host OS.
`lud_gles_version()` returns 2 or 3 at runtime regardless of backend.

## Current status per OS

### Linux — X11 + EGL (native)

- `src/ludica/platform_x11.c`
- Links `libEGL` + `libGLESv2`. Mesa provides both on every
  mainstream distro; Pi ships vendor EGL; Nvidia ships EGL via its
  driver.
- GLES3 context requested via `EGL_OPENGL_ES3_BIT` with fallback to
  GLES2 when `gles_version` is 2 or unset.
- **Status: done.**

### Windows — EGL (via ANGLE)

- `src/ludica/win32/window.c`
- Uses the same EGL/GLES2 headers as Linux. Runtime `libEGL.dll` +
  `libGLESv2.dll` are ANGLE binaries (Google's GLES-over-D3D11
  translator). Vendored under `src/ludica/win32libs/`.
- Same GLES2/3 selection path as Linux.
- **Status: done.** Native `wgl` + desktop GL is explicitly *not*
  used; ANGLE is the portability contract.

### Web — WebGL via Emscripten

- `src/ludica/platform_emscripten.c`
- Emscripten maps GLES2 calls to WebGL 1, GLES3 calls to WebGL 2.
  Shader `#version 100` / `#version 300 es` pass through unchanged.
- **Status: done.**

### macOS — **not implemented**

Apple deprecated OpenGL at 10.14 (2018), caps desktop GL at 4.1 Core,
and ships no GLES or EGL. This is the only target where the
GLES-everywhere promise isn't free.

## macOS options

Three realistic paths. Listed in order of recommendation.

### Option A — ANGLE (recommended)

ANGLE on macOS translates GLES2/3 to Metal (since 2021; previously
OpenGL). Chrome, Firefox, and Unity ship this path in production on
macOS.

- **Pros**
  - Zero divergence from Windows: the Windows backend already targets
    EGL/ANGLE, so the platform layer is `platform_cocoa.c` +
    vendored ANGLE `.dylib`s, nothing more.
  - Real GLES2/3 semantics. sRGB, MRT, float textures, instancing,
    VAOs all behave identically to Linux/Web.
  - Shaders are untouched.
- **Cons**
  - Vendored dependency: ~5–8 MB of `libEGL.dylib` +
    `libGLESv2.dylib`, plus their transitive Metal framework deps.
  - Build story: either bundle prebuilt ANGLE dylibs (like
    `win32libs/`) or add a submodule + depot_tools build step. The
    first is simpler; the second is what most projects regret not
    doing later.
  - Binary signing / notarization for distributed builds now has to
    cover the ANGLE dylibs too.
- **Window system**: Cocoa (`NSWindow` + `CAMetalLayer` for ANGLE's
  Metal backend, or `NSOpenGLView` for the GL backend).

### Option B — Desktop GL 4.1 Core shim

Write `platform_cocoa.c` against `NSOpenGLContext` / CGL, request a
4.1 Core profile, and translate GLES calls at compile time:

- `texture2D()` → `texture()` via `#define` in a shader preamble
- `attribute`/`varying` → `in`/`out`
- `gl_FragColor` → user-declared `out vec4`
- GLES2 extensions (`GL_OES_*`) stubbed or mapped to desktop
  equivalents

- **Pros**
  - No external binary dependencies. Just Cocoa + system OpenGL
    framework (still present on macOS 15, just frozen).
  - Small diff, easy to ship a signed app.
- **Cons**
  - sRGB framebuffer, float textures, MRT behave differently from
    WebGL 2 in subtle ways. Games that work on Linux will
    occasionally break on macOS in ways hard to debug.
  - Shader preamble hack is invasive; every sample shader has to
    survive two translation passes (Emscripten's + ours).
  - Apple's GL driver is frozen at 4.1 and unmaintained. Bugs will
    not be fixed.
  - Eventually Apple will remove the framework. Timeline: unknown,
    but the deprecation is 7 years old.

### Option C — Native Metal backend

Rewrite ludica's renderer in Metal for macOS/iOS. Requires MSL shader
variants for every built-in shader and every game shader.

- **Pros**: best performance, future-proof on Apple platforms, opens
  iOS path.
- **Cons**: doubles the renderer surface area. Contradicts the
  "GLES2/3 everywhere" framing in `CLAUDE.md`. Shader authoring story
  gets much more complex.
- **When this becomes right**: only if ludica grows an iOS target or
  starts caring about perf on Apple silicon past what ANGLE delivers.

## Recommendation

**Adopt ANGLE for macOS (Option A).** It preserves the single-shader,
single-API contract that makes ludica portable, and it reuses the
Windows backend's EGL code path almost verbatim.

Rejected alternatives:

- Option B loses the WebGL-equivalence guarantee in subtle ways
  (sRGB, float textures) and bets on a deprecated Apple framework.
- Option C is premature without an iOS target.

## Implementation sketch (when macOS work starts)

1. Add `src/ludica/platform_cocoa.m` — `NSApplication`, `NSWindow`,
   `CAMetalLayer`, event pump, EGL surface creation via
   `eglCreateWindowSurface` on the layer.
2. Vendor ANGLE dylibs under `src/ludica/macoslibs/` mirroring
   `win32libs/`. Use prebuilt binaries from the ANGLE project's
   Chromium mirror initially; revisit building from source if the
   prebuilt cadence doesn't track our needs.
3. Gamepad: `GCController` (GameController.framework). Add
   `src/ludica/mac/gamepad-mac.m`.
4. Audio: miniaudio already supports CoreAudio. No work.
5. `module.mk`: add `UNAME_S == Darwin` branch. Link
   `-framework Cocoa -framework QuartzCore -framework Metal
   -framework GameController`, plus the vendored dylibs with
   `@loader_path/../Frameworks` install names.
6. Bundle: `.app` layout with `Contents/MacOS/<exe>` and
   `Contents/Frameworks/libEGL.dylib` + `libGLESv2.dylib`.

## Open questions

- Universal binary (x86_64 + arm64) or arm64-only? ANGLE ships
  universal; our build does not yet.
- Notarization: who holds the developer ID? Unresolved.
- Minimum macOS version: ANGLE's Metal backend needs 10.15+. Fine
  for anything current; blocks Intel Macs frozen at 10.14.
