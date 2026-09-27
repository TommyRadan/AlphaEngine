# AlphaEngine

AlphaEngine is a C++20 3D game engine with a Vulkan renderer, with DirectX 12
and Metal backends planned. It includes a scene graph with components, Jolt
physics, Lua scripting, an audio subsystem, and a debug editor overlay. It
targets Windows and Linux and builds with CMake.

## Requirements

- A C++20 compiler: MSVC from Visual Studio 2022 or later, GCC 13+, or
  Clang 18+ with libc++. These are what CI builds against.
- CMake 3.22 or newer.
- Ninja on Linux.
- The Vulkan SDK (LunarG) on Windows; on Linux, these development packages:
  `libgl1-mesa-dev libvulkan-dev libx11-dev libxext-dev libxrandr-dev
  libxcursor-dev libxi-dev libxfixes-dev libxss-dev`.
- Supported platforms: Windows and Linux. macOS is not supported yet (Metal
  is on the roadmap).

## Dependencies

CMake fetches these automatically via `FetchContent`; no system packages are
required for them:

- SDL3 (`release-3.2.0`)
- GLM (`1.0.1`)
- nlohmann/json (`v3.11.3`)
- glslang (`15.0.0`)
- Vulkan Memory Allocator (`v3.4.0`)
- Dear ImGui (`v1.92.8-docking`, Debug builds only)
- ImGuizmo
- Jolt Physics (`v5.6.0`)
- KTX-Software (`v4.4.2`)
- Lua (`v5.4.8`)
- sol2 (`v3.5.0`)

Vendored under `vendor/`: stb and cgltf.

## Building

**Windows**

```
.\scripts\setup-windows.ps1
.\scripts\build.ps1 -Configuration Debug
```

`build.ps1` also takes `-Configuration Release` (the default) and `-Clean`.

**Linux**

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

The binary is written to `Binaries/` (`Binaries/<Configuration>/` with the
Visual Studio generator). Run it from the repository root:

```
./Binaries/AlphaEngine --help
```

`--help` lists every setting.

Formatting and naming checks:

```
./scripts/check-style.ps1
./scripts/check-naming.ps1
```

## License

MIT. See [LICENSE](./LICENSE).
