# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & tooling

- **Primary build (Windows):** `./scripts/build.ps1` auto-detects the toolchain. It prefers MSVC via vcpkg at `C:\vcpkg` (the setup produced by `scripts/setup-windows.ps1`) and falls back to MSYS2 UCRT64 + Ninja + g++ at `C:\msys64`. Pass `-Toolchain msvc|msys2`, `-Configuration Debug|Release|...`, or `-Clean`.
- **Direct CMake (MSVC + vcpkg):**
  ```
  cmake -S . -B build -G "Visual Studio 17 2022" -A x64 \
    -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
  cmake --build build --config Release
  ```
- **SDL3 and GLM are fetched from source** by CMake via `FetchContent` — no system packages are required. GLM is pinned to tag `1.0.1` and header-only; SDL3 is built statically. OpenGL, glad, and stb are linked in as well (glad is built from `vendor/glad`, stb is header-only in `vendor/`). Logging is backed by SDL3's logging API behind `core/log.hpp`.
- **The Vulkan SDK is a required build dependency.** Both the OpenGL and Vulkan backends are always compiled into the binary; CMake calls `find_package(Vulkan REQUIRED)` unconditionally. On Windows the LunarG SDK exposes `VULKAN_SDK`; on Linux install `libvulkan-dev`.
- **Output:** `Binaries/AlphaEngine.exe` (Ninja / single-config) or `Binaries/<Configuration>/AlphaEngine.exe` (Visual Studio multi-config).
- **No test suite exists.** CI does not run tests; don't invent a test target.

## Style and naming gates

Both gates run in CI on Linux with clang-format/clang-tidy version 18. Run locally before pushing:

- **Formatting:** `./scripts/check-style.ps1` (add `-Fix` to rewrite in place). Allman braces, 4-space indent, 120-col, pointer-left. See `.clang-format`.
- **Naming:** `./scripts/check-naming.ps1` (add `-Fix` to apply renames). This configures a separate `build-tidy/` dir with Ninja to produce `compile_commands.json`. **All identifiers are `snake_case`** — including classes, structs, types, enums, namespaces, and functions. Only macros and template parameters differ (UPPER_CASE and CamelCase respectively). See `.clang-tidy`.
- The format/tidy scope is limited to `runtime/ core/ rendering_engine/ external/`. Vendored code under `vendor/` is excluded.

## Architecture

Entry point is `runtime/main_loop.cpp`. It constructs a `runtime::engine` (declared in `runtime/engine.hpp`) whose constructor wires every subsystem up as a `std::unique_ptr` and publishes itself through `runtime::current_engine()`. `engine::init()` brings each subsystem up in dependency order, the main loop calls `engine::tick()` until `is_quit_requested()` is true, and the engine's destructor tears the subsystems down in reverse. Each subsystem still exposes its own `init()` / `quit()` pair and a plain `context` / `window` / `time` struct — none of them are singletons. Everything is main-thread-only; member access is not synchronised.

The subsystems:

- **`core`** — foundation module: math (`core::math`, see below), logging (`core/log.hpp`), settings, time, version, the `pool<T>` container, and the event bus. The event bus is a process-wide synchronous publish/subscribe hub (`core::event_bus`). Listeners subscribe by event struct type (`std::type_index`-keyed) and receive a typed `const E&` callback; the bus exposes `subscribe<E>(listener)`, immediate `emit<E>(args...)`, buffered `enqueue<E>(args...)`, and `flush()`. Not thread-safe; register during init, dispatch from the main loop. Built-in events include `engine_start`, `engine_stop`, `render_scene`, `render_ui`, `render_debug`, input events, and `quit_requested`; game modules may define their own event structs without touching core headers.
- **`rendering_engine`** — owns the window, SDL/GL context, the built-in materials (`lit_material`, `ui_material`), an off-screen HDR scene-colour target (`rgba16f` + depth), and an ordered list of passes. Renderables register with the context via `register_scene_renderable` / `register_ui_renderable` / `register_debug_renderable` (and the matching unregister calls); each pass collects `draw_item`s from its registry, sorts them by `pipeline().id`, and dispatches them in one place — renderables never call `set_pipeline` / `set_bind_group` / `draw_indexed` themselves. The scene pass owns the per-frame camera bind group at slot 0 and renders into the HDR target (clearing it even when no camera is attached so the post chain never samples stale contents); the post chain (currently a single tonemap pass that maps the HDR target to LDR via ACES filmic + gamma encode) runs in between; the UI pass loads the swapchain, disables depth, and composites on top. The debug pass is only appended to the pass list in debug builds (`#if _DEBUG` at the construction site in `context::init`) so the registry-driven debug geometry (future wireframe / gizmo / frustum / bounds visualisations) is dropped entirely in release; inside a debug build it always runs last. After each pass's draw walk it broadcasts `render_scene` / `render_ui` / `render_debug` as the documented escape hatch for debug-line / gizmo callers; the bulk of geometry comes from the registry. It does **not** swap buffers — the main loop calls `window::swap_buffers()` explicitly. Subfolders: `camera/`, `debug_ui/` (Dear ImGui debug overlay — see below), `mesh/`, `gpu/` (backend-agnostic device with both OpenGL and Vulkan backends), `materials/`, `passes/` (+ `passes/post/` for fullscreen-triangle post effects), `renderables/` (+ `premade_2d/`, `premade_3d/`), `util/` (color, font, image, transform).
- **`rendering_engine/debug_ui`** — debug-only Dear ImGui overlay (FPS counter, frame-time profiler, read-only settings inspector, ImGui demo). ImGui is fetched via `FetchContent` (docking branch) and built as a small `imgui` static library from explicit file lists (core + the SDL3 platform backend + the OpenGL3 and Vulkan renderer backends); it is linked and the `ALPHAENGINE_HAS_IMGUI` macro defined **only for Debug** configurations (generator expressions in the root `CMakeLists.txt`), so release builds never link ImGui and `imgui_layer.cpp` compiles to empty no-ops. The layer drives whichever backend the engine brought up: `debug_ui::init` / `shutdown` bracket the device lifetime in `context::init` / `quit`, `window::tick` forwards SDL events through `debug_ui::process_event` (and skips emitting input the overlay is capturing), and `engine::tick` calls `debug_ui::begin_frame` (new-frame + build panels + `ImGui::Render`) before the passes run. The draw data is then recorded inside the swapchain-targeted **debug pass**: the layer subscribes to `core::render_debug` and, while that render pass is still open, calls `ImGui_ImplOpenGL3_RenderDrawData` (immediate-mode, into the bound framebuffer) or `ImGui_ImplVulkan_RenderDrawData` (into the encoder's `VkCommandBuffer`, exposed via `render_pass_encoder::native_command_buffer`). For Vulkan the layer acquires the same `(colour-load, no-depth)` swapchain render pass the debug pass begins, so ImGui's pipeline is render-pass-compatible.
- **Shaders** — authored as Vulkan-style GLSL (`#version 450` with explicit `layout(set, binding)` decorations on every UBO / sampler / texture) and compiled to SPIR-V via `gpu::compile_glsl_to_spirv` (backed by glslang). The OpenGL backend uploads the resulting SPIR-V via `glShaderBinary` + `glSpecializeShader` (core ARB_gl_spirv in OpenGL 4.6) — no GLSL source ever reaches the driver from the engine. Plain-data uniforms live in UBOs (`gpu::binding_kind::uniform_buffer`); per-frame, per-material, per-draw payloads each get their own buffer. Bindings are global within a pipeline (UBOs and samplers each have their own namespace in OpenGL) and unique within a Vulkan descriptor set, so the same numbering works on both backends.
- **`runtime`** — the composition root and world model. Owns `engine` (the subsystem owner / bootstrap described above, reachable via `runtime::current_engine()`) and the scene graph: `runtime::context` (the scene-graph subsystem with the usual init/quit shape), `node` (transform hierarchy), and the components (`camera_component`, `light_component`, `mesh_component`) that attach renderer/lighting resources to nodes.

### Game modules (the `external/` layer)

Gameplay-style code lives in `external/` as **game modules** registered through a self-installing pattern. Each module is a single translation unit that uses the `GAME_MODULE()` macro from `external/api/game_module.hpp`; the macro declares a static `init_status` initializer that calls `module_init()` at static-init time, which fills a `game_module_info` struct of lifecycle callbacks (`on_engine_start`, `on_frame`, `on_render_scene`, `on_render_ui`, input handlers…) and calls `register_game_module(info)`. The registrar wires each callback up as an event listener on the appropriate `event_type`. Existing examples: `cube_module`, `camera_module`, `frame_module`, `exit_module`.

Game modules must include the **public API headers** under `external/api/` — `api/game_module.hpp`, `api/log.hpp`, `api/time.hpp`, `api/camera.hpp` — not the corresponding internal headers. Engine/subsystem code does the opposite and includes `<core/log.hpp>` etc. directly. Mixing these up is a common mistake; see `docs/logging.md`.

### Adding sources

Each source directory ships its own `CMakeLists.txt` that contributes files to the `AlphaEngine` target via `target_sources(AlphaEngine PRIVATE ...)`, with explicit file lists (no `file(GLOB ...)`). The root `CMakeLists.txt` defines the executable and pulls each subsystem in via `add_subdirectory()`; subsystem `CMakeLists.txt` files do the same for their child folders. **When adding a new file, append it to the file list of the directory's `CMakeLists.txt`. When adding a new directory, create a `CMakeLists.txt` in it and add an `add_subdirectory()` call to the parent.** Both backends at `gpu/backend/opengl/` and `gpu/backend/vulkan/` are always linked in; the runtime choice is driven by `settings::get_graphics_backend()` (set via the `ALPHAENGINE_GRAPHICS_BACKEND` env var, default `opengl`). The engine reads it once during construction and dispatches through `gpu::create_device(backend_type)`; `window::init()` then picks the OpenGL path (`SDL_WINDOW_OPENGL`, `SDL_GL_CreateContext`, `SDL_GL_SwapWindow`) or the Vulkan path (`SDL_WINDOW_VULKAN`, `SDL_Vulkan_CreateSurface`, `vkQueuePresentKHR`) accordingly.

## Logging

Use the wrapper in `core/log.hpp`: `LOG_INF`, `LOG_WRN`, `LOG_ERR`, `LOG_FTL`. There is **no TRACE or DEBUG sink yet** — do not add per-frame or per-draw logs. Level-selection guidance and examples are in `docs/logging.md`.

## Math

Use the engine-owned math types in `core/math/math.hpp`: `core::math::vec2`, `vec3`, `vec4`, `mat3`, `mat4`, `quat`, and the snake_case free functions `dot`, `cross`, `normalize`, `length`, `distance`, `lerp`, `look_at`, `perspective`, `ortho`, `translate`, `rotate`, `scale`, `inverse`, `transpose`. These are thin wrappers over GLM with identical memory layout, so GLSL uniform uploads use `value.data()` (matrices) or the existing pointer form for arrays. **Do not reference `glm::` anywhere outside `core/math/`** — that directory is the only place that may include or name GLM directly.

## Working conventions

Before finishing any task:

1. **Follow existing branch and commit conventions.** Check `git log --oneline` and `git branch -a` and match the style you see — don't invent a new one. Commits use Conventional-Commits-style prefixes (`feat:`, `fix:`, `chore:`, `refactor:`, `style:`, `docs:`, `ci:`, `cmake:`, `scripts:`, `repo:`) with a lowercase imperative subject. Branches use `type/short-kebab-description` (e.g. `fix/log-va-list-forwarding`, `chore/sdl3-static`, `feature/fps-counter-overlay`, `docs/doxygen-public-headers`).
2. **Fix style and naming before handing back.** Run `./scripts/check-style.ps1 -Fix` and `./scripts/check-naming.ps1 -Fix`, then re-run both without `-Fix` and confirm they pass. CI will fail otherwise.
3. **Confirm the code builds.** Run `./scripts/build.ps1` (or the equivalent `cmake --build build` invocation for the active toolchain) and verify the configure + build steps both succeed. A passing format/tidy check is not a substitute for a real build.

## CI

`.github/workflows/ci.yml` runs three jobs on every push/PR to `master`: clang-format (Ubuntu, LLVM 18), clang-tidy (Ubuntu, LLVM 18 + libc++ — the CMake passes `-stdlib=libc++` when the compiler is Clang, so the libc++ headers are required), and a Windows MSVC build matrix (Debug + Release) that uploads the built executable as an artifact and posts a sticky PR comment linking to it.
