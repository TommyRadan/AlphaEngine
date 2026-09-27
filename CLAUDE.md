# CLAUDE.md

This file orients a contributor working in this repository: what AlphaEngine is, how to build, run and check it, how the modules relate, and the conventions the code follows. It describes the architecture as it stands today; a detail that belongs to one feature, not the architecture, lives in a doxygen comment next to that code instead of here.

AlphaEngine is a C++20, single-executable real-time 3D engine. `runtime::engine` owns SDL3-backed windowing, input and audio, a Vulkan renderer (`rendering_engine`), Jolt Physics (`runtime::physics`), embedded Lua scripting via sol2 (`runtime/scripting`), and a scene graph of pooled nodes and components; gameplay is added through self-registering game modules under `external/`. It targets Windows (MSVC) and Linux (GCC/Clang). There is no separate editor build and no `docs/` tree — the debug-only Dear ImGui overlay and gizmos under `rendering_engine/editor` are the closest thing to an editor, and reference material lives in doxygen comments next to the code they document.

## Build, run and check

- **Windows:** `./scripts/build.ps1` auto-detects the toolchain: MSVC via vcpkg at `C:\vcpkg` (the setup `scripts/setup-windows.ps1` produces), falling back to MSYS2 UCRT64 (Ninja + g++) at `C:\msys64`. Flags: `-Toolchain msvc|msys2`, `-Configuration Debug|Release|RelWithDebInfo|MinSizeRel` (default `Release`), `-Clean`.
- **Direct CMake (MSVC + vcpkg):**
  ```
  cmake -S . -B build -G "Visual Studio 17 2022" -A x64 \
    -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
  cmake --build build --config Release
  ```
- **Linux (Ninja):**
  ```
  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
  cmake --build build --target AlphaEngine -j2
  ```
  Clang builds the same way with `-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_CXX_FLAGS=-stdlib=libc++`; CI builds both.
- **Output:** `Binaries/AlphaEngine.exe` (Ninja) or `Binaries/<Configuration>/AlphaEngine.exe` (the Visual Studio generator) on Windows; `Binaries/AlphaEngine` on Linux.
- **The Vulkan SDK is a required build dependency on every platform**: `find_package(Vulkan REQUIRED)` runs unconditionally, and the Vulkan backend at `rendering_engine/gpu/backend/vulkan/` is the only GPU backend (`core::settings::graphics.backend`, the `ALPHAENGINE_GRAPHICS_BACKEND` env var, or `--backend`, which accept `vulkan` and warn about any other value).
- SDL3, GLM, nlohmann/json (backs `settings.json`), glslang (GLSL → SPIR-V for the Vulkan backend), the Vulkan Memory Allocator, Dear ImGui + ImGuizmo (Debug builds only), Jolt Physics, KTX-Software (KTX2 texture loading) and Lua + sol2 are all fetched from source by CMake via `FetchContent`; no system package is needed for any of them. stb and cgltf (vendored, header-only) round out the stack.
- **Format and naming gates**, both run in CI on Linux with clang-format/clang-tidy 18:
  - `./scripts/check-style.ps1` (`-Fix` to rewrite in place) runs clang-format over every `.cpp`/`.hpp`/`.h` under `runtime/`, `core/`, `rendering_engine/`, `external/`. See `.clang-format` (Allman braces, 4-space indent, 120 columns, pointer-left).
  - `./scripts/check-naming.ps1` (`-Fix` to apply renames) configures a Ninja `build-tidy/` directory for `compile_commands.json`, then runs clang-tidy's `readability-identifier-naming` check over the same four directories. See `.clang-tidy` (snake_case).
  - The Linux equivalents (what CI actually runs):
    ```
    find core runtime rendering_engine external -type f \( -name '*.cpp' -o -name '*.hpp' \) \
      | xargs clang-format-18 --style=file --dry-run -Werror
    cmake -S . -B build-tidy -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
      -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
    run-clang-tidy-18 -p build-tidy -quiet "(runtime|core|rendering_engine|external)/"
    ```
  - Every source, shader, CMake, script and workflow file under `core/`, `runtime/`, `rendering_engine/`, `external/`, `shaders/`, `cmake/`, `scripts/`, the root `CMakeLists.txt` and `.github/workflows/` must start with the SPDX header (see Conventions below); CI checks this alongside clang-format.
- **Headless smoke run:** a Debug binary can run with no real display or GPU, on Mesa's lavapipe software Vulkan implementation with the Khronos validation layer, for a bounded number of frames:
  ```
  export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation \
    ALPHAENGINE_GRAPHICS_BACKEND=vulkan SDL_AUDIO_DRIVER=dummy
  xvfb-run -a ./Binaries/AlphaEngine --width 640 --height 360 --frames 300 --fail-on-error
  ```
  `--fail-on-error` makes the run exit non-zero on any `[ERR]`/`[FTL]` log line, so a validation message fails it even without a crash. Add `VK_LAYER_ENABLES=VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT` to the environment for the synchronization-validation variant. There is no `docs/` directory and no unit or functional test suite; this smoke run is the runtime check.

## Module map

### `core`

The foundation module; nothing here depends on `runtime`, `rendering_engine` or `external` (see Dependency rules below). It holds: math (`core::math` — `vec2`/`vec3`/`vec4`, `mat3`/`mat4`, `quat`, and snake_case free functions `dot`, `cross`, `normalize`, `length`, `lerp`, `look_at`, `perspective`, `ortho`, `translate`, `rotate`, `scale`, `inverse`, `transpose` — thin GLM wrappers with identical memory layout, so a GLSL uniform upload uses `value.data()`); logging (`core/log.hpp`, see Conventions); `core::settings`, resolved once by `core::load_settings` from compiled defaults, then `<pref path>/settings.json`, then `ALPHAENGINE_*` environment variables, then the command line, each layer overriding the last; `core::time`; the slot-allocated `core::pool<T>` container; `core::event_bus`, a process-wide synchronous typed publish/subscribe hub (`subscribe<E>`, `emit<E>`, `enqueue<E>`, `flush`; reentrant, main-thread-only); `core::job_pool`, a worker pool for background dispatch and the one subsystem that is itself thread-safe; `core::input`; `core::audio` (an SDL3 playback device, software mixer and clip cache, with no dependency on the renderer); `core::vfs` (the mount points the asset loaders and the script host read files through); and `core::platform` (the crash handler, path and time helpers, dynamic-library loading, directory-watch polling).

### `runtime`

The composition root and world model. `runtime::engine` (`runtime/engine.hpp`) owns every subsystem as a `std::unique_ptr`, in dependency order, and publishes itself through `runtime::current_engine()`; `engine::init()` brings each subsystem up (events, audio, input, the VFS mount, the renderer and its GPU device, the asset cache, physics, scripting, the scenes, then the game modules), `runtime/main_loop.cpp`'s loop calls `engine::tick()` until `is_quit_requested()` is true, and the destructor tears every subsystem down in reverse. `runtime::scene` is the scene graph: `runtime::node` (a pooled, stable-address transform hierarchy) and per-type-pooled components (`runtime::component_handle`, `runtime::component_store`) — any default- and copy/move-constructible struct can be a component with no registration, and one that defines `on_update(node&)` gets it called once per rendered frame, in type order, parents before children. `runtime::behavior` is game logic attached through a `behavior_component`, with `on_enable`/`on_start`/`on_fixed_update`/`on_update`/`on_disable`/`on_destroy` lifecycle hooks. A structural change from inside a hook — destroying a node, removing a component, reparenting, enabling or disabling a subtree — must go through the scene's deferred commands (`destroy_node`, `defer_remove_component<C>`, `defer_reparent`, `defer_set_active`), applied once the current walk finishes; the immediate APIs refuse otherwise. `runtime::scene_manager` loads, unloads, enables and updates every scene (the persistent one plus whatever else is loaded); `runtime/scene_serializer.hpp` (`save_scene`, `load_scene`) saves and loads a scene or a subtree as JSON — scene files and prefabs; `REFLECT_TYPES()` and `runtime::type_builder<T>` (`runtime/reflection.hpp`; `registry.register_component<C>("name")` / `register_behavior<B>("name")`) let a component or behaviour declare the fields a scene file should carry. `runtime::physics::world` (`runtime/physics/`) wraps Jolt Physics, stepped at a fixed rate from `engine::tick`. Lua 5.4 is embedded through sol2 under `runtime/scripting/`: `runtime::lua_behavior` is an ordinary `behavior` whose hooks forward to a script loaded through the VFS. `runtime::instantiate_gltf` (`runtime/gltf_instantiate.hpp`) spawns nodes and components from an imported glTF model.

### `rendering_engine`

Owns the window, the GPU device and the ordered list of render passes, through `rendering_engine::renderer` (`renderer.hpp`), itself owned by `runtime::engine`. `renderer::render()` walks its pass list twice per frame — every pass's `prepare`, then every pass's `record` — inside `gpu::device::begin_frame`/`end_frame`, handing every pass the same per-frame `frame_context` (active camera, off-screen and swapchain targets, viewport, frame index, TAA jitter) so passes cannot disagree mid-frame; the scene pass renders into an off-screen HDR target, a post chain (tonemap, and depending on settings bloom, TAA, FXAA, motion blur, auto-exposure and volumetric fog) resolves it to LDR, and the UI pass composites on top. Renderables register into the world's registries (`register_scene_renderable`/`register_ui_renderable`/`register_debug_renderable`, forwarded by the renderer to `render_world`) rather than drawing themselves; each pass collects, sorts and dispatches its registry's `draw_item`s.

- **`gpu/`** — the backend-agnostic RHI: `gpu::device`, `command_encoder`, `pipeline`, `bind_group`, `buffer`, `texture`, `handle`, and `gpu::create_device(backend_type)`, which builds the implementation under `gpu/backend/vulkan/` (VMA-backed sub-allocation, a staging ring for uploads, the per-draw block in push constants). Shader compilation (`shader_compiler`, GLSL → SPIR-V via glslang) and lookup (`shader_library`) live here too.
- **`passes/`** (+ `passes/post/`) — one type per render pass (`scene_pass`, `shadow_pass`/`point_shadow_pass`/`spot_shadow_pass`, `depth_prepass`, `skybox_pass`, `ui_pass`, and the post passes), each implementing the `rendering_engine::pass` interface.
- **`materials/`** — a `material_template` per material type (its shaders, layouts, and a cache of pipeline variants) shared by any number of `material` instances; eight built-in types (`basic`, `phong`, `standard`, `instanced`, `points`, `line`, `grid`, `ui`).
- **`renderables/`** (+ `premade_2d/`, `premade_3d/`) — the `draw_item`-emitting objects a pass draws: `model`, `instanced_mesh`, `line`, `points`, the primitive shapes, and the 2D/UI widgets.
- **`assets/`** — `asset_cache`, a reference-counted, deduplicating loader for textures (including KTX2, via libktx), fonts and meshes, plus the glTF 2.0 importer (`gltf_importer.hpp`, via the vendored `cgltf`).
- **`editor/`** — debug-only helper geometry (grid, axes, light and camera gizmos) and the Dear ImGui + ImGuizmo overlay (`imgui_layer`); appended to the pass list only in Debug builds.
- **`lighting/`** — `ambient_light`, `directional_light`, `point_light`, `spot_light`, `environment_probe`.
- **`camera/`** — `camera`, `perspective_camera`, `orthographic_camera`, and `camera_registry`, which arbitrates the active camera by priority.
- **`animation/`** — `skeleton` and `animation_clip`, for skinned meshes.

### `external`

Gameplay code as self-registering **game modules**: one translation unit each, using `GAME_MODULE() { ... }` from `external/api/game_module.hpp`. The macro registers a bootstrap that the engine calls once, after every subsystem is up, with the active `runtime::scene&`, to spawn nodes, attach components and `runtime::add_behavior<B>(node, ...)` instances; a module names its behaviours in a `REFLECT_TYPES()` block so scene files can carry them. Modules include `core/`, `runtime/` and `rendering_engine/` headers directly rather than through a separate facade; `external/api/` supplies only the `GAME_MODULE()` bootstrap macro (`external/api/log.hpp` and `external/api/time.hpp` declare a small logging/timing surface that nothing in the tree currently calls).

### `shaders`

GLSL under `shaders/` (`include/` for shared `#include`-able pieces, `materials/` for the eight built-in materials, `passes/` for the pass stages), authored as Vulkan-style GLSL (`#version 450`, explicit `layout(set, binding)` on every resource). `shaders/CMakeLists.txt` lists every file explicitly; `cmake/embed_shaders.cmake` embeds them into the `shader_registry` static library at build time, served by `gpu::shader_library::source(path)`; `cmake/generate_shader_bindings.cmake` generates `include/bindings.glsl` from `rendering_engine/gpu/shader_bindings.hpp`, so the binding numbers have one owner. Compiled SPIR-V is cached on disk under `<pref path>/shader_cache/` (`ALPHAENGINE_SHADER_CACHE` disables it or redirects it).

### Dependency rules

- `core` depends on nothing else in the tree — it is the foundation every other module builds on.
- `runtime` and `rendering_engine` depend on each other: `runtime/components` attaches `rendering_engine` types (materials, renderables, cameras, lights) to nodes, and most of `rendering_engine`'s own code reaches other subsystems (the job pool, the asset cache, settings) through `runtime::current_engine()`, declared in `runtime/engine.hpp`. The asset cache's own internals avoid that coupling through the narrower `rendering_engine::asset_device()` accessor instead, so the asset layer alone does not need the whole engine linked in to be exercised.
- `external` depends on `core`, `runtime` and `rendering_engine`; nothing in those three depends on `external`.
- **GLM types and functions are named only inside `core/math/`.** Every other module reaches vectors, matrices and quaternions through `core::math`.

## Frame lifecycle

`runtime::engine::tick()` runs once per iteration of the main loop, in this order:

1. Advance the clock (`core::time::perform_tick`), so this tick's delta describes the frame about to run.
2. Pump OS and window events, then input (`rendering_engine::window::tick`, then `core::input::end_frame`, which latches this frame's cursor motion now that every pumped event has updated the live action/axis state).
3. Flush events buffered since the last tick (`core::event_bus::flush`).
4. Resolve asynchronous asset loads whose decode has finished, uploading to the GPU on the main thread (`asset_cache::pump`).
5. Run the fixed-step simulation, decoupled from the render rate: feed this frame's real delta into the accumulator, then, for each fixed step it drains, latch input (`core::input::begin_step`), emit `core::frame` (every enabled `behavior::on_fixed_update` runs from it), and step physics (`runtime::physics::world::step`) — a frame may run zero, one or several steps; afterwards, interpolate the simulated nodes between the last two steps (`runtime::physics::world::interpolate`) so they move smoothly at the render rate.
6. Emit `core::render_update`, for render-rate, input-driven logic.
7. Update every loaded scene (`scene_manager::update`): settle world transforms depth-first, then run each component type's `on_update` in turn — including every enabled `behavior::on_update` — parents before children, then apply the scene's deferred commands.
8. Update audio (`core::audio::update`), after the scene so this frame's transform changes are already applied; keeps playing while the window is minimized.
9. Unless the window is minimized: build the debug/editor overlay (`rendering_engine::editor::begin_frame`), render the frame (`renderer::render()` — every pass's `prepare`, then every pass's `record`, bracketed by `gpu::device::begin_frame`/`end_frame`; the device presents inside `end_frame`).
10. If `core::settings::diagnostics.frame_limit` is non-zero and reached, request quit (`core::quit_requested`).

## Conventions

- `snake_case` for every identifier — namespaces, classes, structs, enums, functions, variables — including in game modules; only macros (`UPPER_CASE`) and template parameters (`CamelCase`) differ.
- Allman braces, 4-space indent, 120-column limit, pointer-left (`int* p`, not `int *p`). See `.clang-format`.
- Explicit file lists in every `target_sources()`/`add_library()` call — no `file(GLOB ...)`.
- Logging goes through `core/log.hpp`'s `LOG_TRC`/`LOG_DBG`/`LOG_INF`/`LOG_WRN`/`LOG_ERR`/`LOG_FTL` macros, never a raw `printf`, `std::cout` or SDL's logging API. Per-frame or per-draw output is `LOG_TRC` only, dropped before formatting unless trace is enabled. The active level comes from the build type (`debug` in Debug, `info` otherwise) and the `ALPHAENGINE_LOG_LEVEL` environment variable (e.g. `info,gpu=trace`); each translation unit sets `LOG_CATEGORY` (`engine` by default) before its first include. `LOG_FTL` logs and flushes but does not abort — the call site throws right after.
- **Comments** are doxygen (`@brief`, `@param`, ...) or an explanation of logic that is not obvious from the code around it; they describe the code as it is, never its history — no "was", "used to", "previously", "now", "no longer", "follow-up", and no issue or PR references, anywhere (code, comments, CMake, YAML, or this file). A detail a reader of the code needs belongs in a doxygen comment next to that code.
- Every new source, shader, CMake, script and workflow file starts with:
  ```
  // SPDX-License-Identifier: MIT
  // Copyright (c) 2015-2026 Tomislav Radanovic
  ```
  (`#` in place of `//` for CMake, YAML and shell/PowerShell files), followed by one blank line.
- Commits use Conventional-Commits-style prefixes (`feat:`, `fix:`, `refactor:`, `chore:`, `perf:`, `style:`, `docs:`, `ci:`, `cmake:`, `shaders:`, `scripts:`, `repo:`) with a lowercase imperative subject and, when the change needs explaining, a body wrapped at 72 columns that explains why rather than what. No attribution trailers. The repository is rebase-merged, so every commit must build on its own.
- Branches use `type/short-kebab-description` (e.g. `fix/log-va-list-forwarding`, `feature/fps-counter-overlay`).
- There is no `docs/` directory and no unit or functional test suite for now; the headless smoke run above is the runtime check, and this file is the architecture reference.

## How to

- **Add a source file or directory:** append the file to its directory's `CMakeLists.txt` (`target_sources(AlphaEngine PRIVATE ...)`); for a new directory, give it its own `CMakeLists.txt` and add `add_subdirectory(...)` to the parent's.
- **Add a render pass:** derive a type from `rendering_engine::pass` (`passes/pass.hpp`), implement `record` (and `prepare`, `resize` or `declare_io` if the pass needs them), then construct it and call `m_passes.add(std::move(your_pass))` in `renderer::init`, in the right place in the ordering.
- **Add a material:** give the type (`materials/<name>_material.{hpp,cpp}`) a static `create_template(gpu::device&, ...)` that builds a `material_template`, and have `material_library::init` build and hold one instance; further instances share that template's pipeline cache.
- **Add a component:** any default- and copy/move-constructible struct works with no registration to exist on a node; give it an `on_update(node&)` method for per-frame logic, attach it with `node::add_component<C>(value)`, and register it (`registry.register_component<C>("name")` in a `REFLECT_TYPES()` block, `runtime/reflection.hpp`) if scene files should be able to save and load it.
- **Add a game module:** a new `.cpp` under `external/` with `GAME_MODULE() { ... }`; the body receives the active `runtime::scene&` to spawn nodes, attach components and `runtime::add_behavior<B>(node, ...)` instances, and may name behaviours in its own `REFLECT_TYPES()` block.
- **Add a script:** attach `runtime::add_behavior<runtime::lua_behavior>(node, "scripts/foo.lua")`; the script, read through the VFS, returns a table implementing any of `on_start`, `on_enable`, `on_disable`, `on_fixed_update(dt)`, `on_update(dt)`, `on_destroy`.

## CI

`.github/workflows/ci.yml` runs on every push and pull request to `master`:

- **`format`** (Ubuntu) — clang-format 18 `--dry-run -Werror` over `core/`, `runtime/`, `rendering_engine/`, `external/`, plus the SPDX-header check over those directories and `shaders/`, `cmake/`, `scripts/`, `CMakeLists.txt` and `.github/workflows/`.
- **`tidy`** (Ubuntu, Clang 18 + libc++) — configures the tree for `compile_commands.json`, then runs `run-clang-tidy` (the `readability-identifier-naming` check) over `core/`, `runtime/`, `rendering_engine/`, `external/`.
- **`build-linux`** (matrix: GCC, Clang + libc++) — configures and builds the `AlphaEngine` target on both Linux toolchains; it does not run the binary.
- **`smoke`** (matrix: plain, ASan+UBSan) — builds a Debug binary and runs it headlessly under `xvfb-run` on Mesa lavapipe with the Khronos validation layer, twice (once with synchronization validation added), 300 frames each, failing on any `[ERR]`/`[FTL]`/validation-layer line.
- **`build`** (Windows, matrix: Debug/Release) — installs the Vulkan SDK, builds with MSVC, stages the Khronos validation layer next to a Debug binary, and uploads both configurations as workflow artifacts.
- **`comment-artifacts`** — posts, or updates, a sticky pull-request comment linking to the workflow run's uploaded artifacts.
