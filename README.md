# AlphaEngine

AlphaEngine is a C++20 real-time rendering and simulation engine. It renders
through a backend-agnostic `gpu::device` with two implementations, **Vulkan**
(the default) and **OpenGL 4.6 core**, that are both compiled into every
binary. Shaders are written once as Vulkan-style GLSL and compiled to SPIR-V
with glslang at start-up, so the same byte code feeds both backends. The
engine, its demo modules and its unit tests build on Windows and Linux.

## Features

**Rendering**

- Physically based `standard_material` (metallic-roughness, Cook-Torrance
  GGX) with albedo, normal, metalness, roughness and emissive maps, plus
  Phong, basic, instanced, line, points, grid and UI materials.
- Image-based lighting from a cube-map skybox: diffuse irradiance,
  GGX-prefiltered specular and a split-sum BRDF LUT, convolved with compute
  shaders on both backends (with a CPU fallback).
- Colour-space-aware texture loading: `asset_cache::load_texture` and the
  material map setters take a `gpu::color_space` (sRGB by default), so
  colour maps are stored as `rgba8_srgb` and decoded to linear on sample
  while data maps (normals, metalness, roughness) stay linear.
- Ambient, directional and point lights. Directional shadows come from a
  4096x4096 depth map with PCF filtering; point lights cast omnidirectional
  shadows from six 1024x1024 faces.
- An HDR scene target feeding a post chain of bloom, ACES filmic tonemap,
  temporal anti-aliasing (Halton-jittered, with a velocity pass for the
  reprojection) and FXAA. The scene depth is exposed to post passes as a
  sampleable texture (`frame_context::scene_depth_texture`, a `scene_depth`
  frame-graph resource) with shared GLSL helpers to linearise it.
- Linear and exponential distance fog, instanced meshes, line and point
  renderables, 2D labels and panes, and premade primitives (box, sphere,
  cubed sphere, capsule, cone, cylinder, torus, ring, circle, plane and the
  platonic solids).
- Debug helpers (axes, grids, boxes, lines, camera and light gizmos) and a
  Dear ImGui overlay in Debug builds: FPS counter, frame-time profiler,
  scene statistics, a settings inspector and helper visibility toggles.

**Engine**

- `runtime::engine` owns every subsystem (settings, time, job pool, event
  bus, window, GPU device, asset cache, renderer, scene graph) and brings
  them up and down in dependency order; nothing is a singleton.
- An entity/component scene graph: nodes carry a transform and generational
  handles into per-subsystem component pools (`mesh_component`,
  `light_component`, `camera_component`). See
  [docs/scene_graph.md](./docs/scene_graph.md).
- An asset cache that deduplicates textures (keyed by path and colour
  space), fonts and meshes by identity and gives GPU resources
  reference-counted lifetimes.
- A synchronous event bus with RAII subscription tokens that carries engine
  lifecycle, window (resize, focus, minimise, close), keyboard, mouse,
  text-input and gamepad events and the render hooks, and a fixed-size
  worker pool with `parallel_for`.
- A frame graph: each render pass declares what it reads and writes and the
  graph validates those dependencies before executing the passes in order.
- Game code lives in self-registering **game modules** under `external/`;
  the demo modules in the tree exercise the features above.

## Requirements

- CMake 3.16 or newer and a C++20 compiler (MSVC 2022 or newer, GCC or Clang).
- The **Vulkan SDK**. On Windows install the
  [LunarG SDK](https://vulkan.lunarg.com/), which sets `VULKAN_SDK`; on
  Linux install `libvulkan-dev`. Both backends are always built, so this is
  required even if you only ever run OpenGL.
- OpenGL development headers (on Linux, `libgl1-mesa-dev`).
- On Linux, the X11 development packages SDL3 needs in order to configure.
  The full package list CI installs is:

  ```sh
  sudo apt-get install cmake ninja-build g++ libgl1-mesa-dev libvulkan-dev \
      libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev \
      libxfixes-dev libxss-dev
  ```

- Ninja is recommended; `scripts/build.sh` uses it, and so does
  `scripts/check-naming.sh` when it configures its own `build-tidy/`.

Everything else is fetched from source by CMake via `FetchContent` and built
statically: SDL3 (3.2.0), GLM (1.0.1), glslang (15.0.0), Dear ImGui (docking
branch, linked into Debug builds only) and GoogleTest (tests only). glad (the
OpenGL 4.6 core loader) and stb are vendored under `vendor/`.

## Building

### Windows

`scripts/setup-windows.ps1` bootstraps a machine with Git, the Visual Studio
2022 Build Tools (C++ workload), CMake and vcpkg. Then:

```powershell
.\scripts\build.ps1                              # Release, auto-detected toolchain
.\scripts\build.ps1 -Configuration Debug -Clean  # wipe the build dir first
.\scripts\build.ps1 -Toolchain msys2             # MSYS2 UCRT64 + Ninja + g++
```

`build.ps1` prefers MSVC via vcpkg at `C:\vcpkg` and falls back to MSYS2 at
`C:\msys64`. The equivalent direct CMake invocation is:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
    -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

The executable is written to `Binaries\<Configuration>\AlphaEngine.exe`.

### Linux / macOS

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

or `scripts/build.sh`, which wraps the same steps:

```sh
scripts/build.sh                                  # Release build into build/
scripts/build.sh --config Debug --clean           # wipe the build dir first
scripts/build.sh --tests                          # also build and run the unit tests
scripts/build.sh -- -DCMAKE_CXX_COMPILER=clang++  # extra configure arguments
```

The executable is written to `Binaries/AlphaEngine`. `CMAKE_BUILD_TYPE`
defaults to `Debug` when it is not given.

## Running

The engine reads its configuration from environment variables at start-up:

| Variable | Values | Default | Effect |
| --- | --- | --- | --- |
| `ALPHAENGINE_GRAPHICS_BACKEND` | `vulkan`, `opengl` | `vulkan` | Selects the GPU backend. |
| `ALPHAENGINE_TAA` | `1` / `on` / `true`, `0` / `off` / `false` | on | Enables or disables temporal anti-aliasing. |
| `ALPHAENGINE_LOG_LEVEL` | `trace`, `debug`, `info`, `warn`, `error` or `fatal`, optionally followed by `category=level` overrides (e.g. `info,gpu=trace`) | `debug` in Debug builds, `info` otherwise | Sets the log level globally and per category. |

Debug builds open a 1600x900 window with the ImGui overlay; right-click the
FPS counter to toggle the inspector panels. Release builds go fullscreen at
the desktop resolution and do not link ImGui. The window is resizable and
requests a high-pixel-density drawable: the swapchain follows the drawable
size through the `window_resized` event, while the off-screen render targets
are still allocated at the start-up size. Gamepads are opened as they
connect and report through the gamepad events.

## Tests

The unit tests (`tests/`, GoogleTest) cover the device-free engine core and
run headless. They are built by default (`-DALPHAENGINE_BUILD_TESTS=ON`) and
registered with ctest:

```sh
cmake --build build --target AlphaEngineTests
ctest --test-dir build --output-on-failure
```

CI builds the engine on Windows (MSVC, Debug and Release) and builds and runs
the tests on Linux. See [docs/testing.md](./docs/testing.md) for what is
covered and how the test target is wired.

## Style and naming gates

CI runs clang-format and clang-tidy (LLVM 18) over `runtime/`, `core/`,
`rendering_engine/` and `external/`. All identifiers are `snake_case`
(macros `UPPER_CASE`, template parameters `CamelCase`); formatting is Allman
braces, 4-space indent, 120 columns, pointer-left. The rules live in
`.clang-format` and `.clang-tidy`. Run the gates locally before pushing:

| | Windows | Linux / macOS |
| --- | --- | --- |
| Check formatting | `.\scripts\check-style.ps1` | `scripts/check-style.sh` |
| Fix formatting | `.\scripts\check-style.ps1 -Fix` | `scripts/check-style.sh --fix` |
| Check naming | `.\scripts\check-naming.ps1` | `scripts/check-naming.sh` |
| Fix naming | `.\scripts\check-naming.ps1 -Fix` | `scripts/check-naming.sh --fix` |

The naming scripts configure a separate `build-tidy/` directory with Ninja
to produce `compile_commands.json`; `check-naming.sh --build-dir build`
reuses a build directory that was already configured with
`-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`. The scripts use the `clang-format-18`
/ `clang-tidy-18` binaries when they are installed and fall back to the
unsuffixed names.

The same gates are exposed as CMake targets, `format-check`, `format-fix`
and `tidy`, whenever the tools are found at configure time (`tidy` also
needs `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` and a Ninja or Makefile
generator):

```sh
cmake -S . -B build -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build --target format-check
cmake --build build --target tidy
```

`tests/` is excluded from both gates.

## Logging

Logging goes through the thin wrapper over SDL's logging API in
`core/log.hpp`. Six levels are available (`LOG_TRC`, `LOG_DBG`, `LOG_INF`,
`LOG_WRN`, `LOG_ERR`, `LOG_FTL`), every message carries a category, and the
runtime level is set with the `ALPHAENGINE_LOG_LEVEL` environment variable
(see [Running](#running)). See [docs/logging.md](./docs/logging.md) for the
conventions and for guidance on picking a level.

## Project layout

| Path | Contents |
| --- | --- |
| `core/` | Math (`core::math`), logging, settings, time, version, `pool<T>`, the event bus and the job pool. |
| `rendering_engine/` | Window and SDL input translation, `gpu::device` with the OpenGL and Vulkan backends, materials, render passes and the post chain, renderables, cameras, lighting, IBL, the asset cache, the frame graph, debug helpers and the ImGui overlay. |
| `runtime/` | `engine` (subsystem owner and main loop), the scene graph (`node`, components) and `main_loop.cpp`. |
| `external/` | Game modules and the public game-module API (`external/api/`). |
| `tests/` | GoogleTest unit tests and the fake GPU device they run against. |
| `vendor/` | Vendored glad and stb. |
| `scripts/` | Build, formatting and naming scripts (PowerShell and bash). |
| `cmake/` | CMake helpers (the clang-format script behind the `format-*` targets). |
| `docs/` | Design notes: logging, scene graph, testing. |

## License

AlphaEngine is licensed under the MIT License. See
[LICENCE.txt](./LICENCE.txt) for details.
