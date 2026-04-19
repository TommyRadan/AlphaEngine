# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & tooling

- **Primary build (Windows):** `./scripts/build.ps1` auto-detects the toolchain. It prefers MSVC via vcpkg at `C:\vcpkg` (the setup produced by `scripts/setup-windows.ps1`) and falls back to MSYS2 UCRT64 + Ninja + g++ at `C:\msys64`. Pass `-Toolchain msvc|msys2`, `-Configuration Debug|Release|...`, or `-Clean`.
- **Direct CMake (MSVC + vcpkg):**
  ```
  cmake -S . -B build -G "Visual Studio 17 2022" -A x64 \
    -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DGLM_INCLUDE_DIR=C:/vcpkg/installed/x64-windows/include/glm
  cmake --build build --config Release
  ```
  `FindGLM.cmake` only searches Homebrew paths, so `GLM_INCLUDE_DIR` must be set explicitly on Windows and Linux.
- **SDL3 is fetched and built statically** by CMake via `FetchContent` — no system SDL package is required. GLM, OpenGL, glad, loguru, and stb are linked in as well (glad is built from `vendor/glad`, loguru/stb are header-only in `vendor/`).
- **Output:** `Binaries/AlphaEngine.exe` (Ninja / single-config) or `Binaries/<Configuration>/AlphaEngine.exe` (Visual Studio multi-config).
- **No test suite exists.** CI does not run tests; don't invent a test target.

## Style and naming gates

Both gates run in CI on Linux with clang-format/clang-tidy version 18. Run locally before pushing:

- **Formatting:** `./scripts/check-style.ps1` (add `-Fix` to rewrite in place). Allman braces, 4-space indent, 120-col, pointer-left. See `.clang-format`.
- **Naming:** `./scripts/check-naming.ps1` (add `-Fix` to apply renames). This configures a separate `build-tidy/` dir with Ninja to produce `compile_commands.json`. **All identifiers are `snake_case`** — including classes, structs, types, enums, namespaces, and functions. Only macros and template parameters differ (UPPER_CASE and CamelCase respectively). See `.clang-tidy`.
- The format/tidy scope is limited to `control/ event_engine/ rendering_engine/ scene_graph/ infrastructure/ external/`. Vendored code under `vendor/` is excluded.

## Architecture

Entry point is `control/main_loop.cpp`. It initializes three subsystems, runs the render loop, and tears them down in reverse. All three follow the same shape: a `context` struct in its own namespace that inherits from `infrastructure::singleton<T>` and exposes `init()` / `quit()`. The singleton is a Meyers-style function-local static — construction is thread-safe but member access is not; treat everything as main-thread-only.

The subsystems:

- **`event_engine`** — process-wide synchronous publish/subscribe hub (`event_engine::event_bus`). Listeners subscribe by event struct type (`std::type_index`-keyed) and receive a typed `const E&` callback; the bus exposes `subscribe<E>(listener)`, immediate `emit<E>(args...)`, buffered `enqueue<E>(args...)`, and `flush()`. Not thread-safe; register during init, dispatch from the main loop. Built-in events include `engine_start`, `engine_stop`, `render_scene`, `render_ui`, input events, and `quit_requested`; game modules may define their own event structs without touching core headers.
- **`rendering_engine`** — owns the window, SDL/GL context, and built-in renderers (`basic_renderer`, `overlay_renderer`). `render()` broadcasts `render_scene` (if a camera is active) then `render_ui` with depth test disabled. It does **not** swap buffers — the main loop calls `window::swap_buffers()` explicitly. Subfolders: `camera/`, `mesh/`, `opengl/` (GL wrapper types: program, shader, buffer, texture, framebuffer, vertex_array), `renderables/` (+ `premade_2d/`, `premade_3d/`), `renderers/`, `util/` (color, font, image, transform).
- **`scene_graph`** — currently a lifecycle stub with the same init/quit shape as the others.

### Game modules (the `external/` layer)

Gameplay-style code lives in `external/` as **game modules** registered through a self-installing pattern. Each module is a single translation unit that uses the `GAME_MODULE()` macro from `external/api/game_module.hpp`; the macro declares a static `init_status` initializer that calls `module_init()` at static-init time, which fills a `game_module_info` struct of lifecycle callbacks (`on_engine_start`, `on_frame`, `on_render_scene`, `on_render_ui`, input handlers…) and calls `register_game_module(info)`. The registrar wires each callback up as an event listener on the appropriate `event_type`. Existing examples: `cube_module`, `camera_module`, `frame_module`, `fps_overlay_module`, `exit_module`.

Game modules must include the **public API headers** under `external/api/` — `api/game_module.hpp`, `api/log.hpp`, `api/time.hpp`, `api/camera.hpp` — not the corresponding internal headers. Engine/subsystem code does the opposite and includes `<infrastructure/log.hpp>` etc. directly. Mixing these up is a common mistake; see `docs/logging.md`.

### Adding sources

`CMakeLists.txt` uses `file(GLOB ...)` across each subsystem's top-level directory plus the known `rendering_engine/*` subdirectories. New files in those locations are picked up on reconfigure. **New rendering subdirectories** (beyond `mesh/`, `renderables/`, `renderables/premade_2d/`, `renderables/premade_3d/`, `renderers/`, `camera/`, `opengl/`, `util/`) require a matching GLOB in `CMakeLists.txt`.

## Logging

Use the wrapper in `infrastructure/log.hpp`: `LOG_INF`, `LOG_WRN`, `LOG_ERR`, `LOG_FTL`. There is **no TRACE or DEBUG sink yet** — do not add per-frame or per-draw logs. Level-selection guidance and examples are in `docs/logging.md`.

## Working conventions

Before finishing any task:

1. **Follow existing branch and commit conventions.** Check `git log --oneline` and `git branch -a` and match the style you see — don't invent a new one. Commits use Conventional-Commits-style prefixes (`feat:`, `fix:`, `chore:`, `refactor:`, `style:`, `docs:`, `ci:`, `cmake:`, `scripts:`, `repo:`) with a lowercase imperative subject. Branches use `type/short-kebab-description` (e.g. `fix/log-va-list-forwarding`, `chore/sdl3-static`, `feature/fps-counter-overlay`, `docs/doxygen-public-headers`).
2. **Fix style and naming before handing back.** Run `./scripts/check-style.ps1 -Fix` and `./scripts/check-naming.ps1 -Fix`, then re-run both without `-Fix` and confirm they pass. CI will fail otherwise.
3. **Confirm the code builds.** Run `./scripts/build.ps1` (or the equivalent `cmake --build build` invocation for the active toolchain) and verify the configure + build steps both succeed. A passing format/tidy check is not a substitute for a real build.

## CI

`.github/workflows/ci.yml` runs three jobs on every push/PR to `master`: clang-format (Ubuntu, LLVM 18), clang-tidy (Ubuntu, LLVM 18 + libc++ — the CMake passes `-stdlib=libc++` when the compiler is Clang, so the libc++ headers are required), and a Windows MSVC build matrix (Debug + Release) that uploads the built executable as an artifact and posts a sticky PR comment linking to it.
