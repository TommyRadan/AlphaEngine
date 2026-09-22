# Logging

AlphaEngine logs through the thin wrapper in `core/log.hpp`, which
forwards to SDL's logging API (`SDL_LogMessageV`). Six macros are
available:

| Macro     | Verbosity | Use for                                                     |
| --------- | --------- | ----------------------------------------------------------- |
| `LOG_TRC` | TRACE     | Hot-path detail: per-frame / per-draw / per-event; off unless explicitly enabled |
| `LOG_DBG` | DEBUG     | Resource loads, cache hits/misses, state transitions; on by default in Debug builds |
| `LOG_INF` | INFO      | Subsystem lifecycle, version banner, parsed settings, one-shot registrations |
| `LOG_WRN` | WARN      | Recoverable faults, fallbacks, missing optional data        |
| `LOG_ERR` | ERROR     | Unrecoverable faults that do not abort the process          |
| `LOG_FTL` | FATAL     | Unrecoverable faults that precede `throw` / `exit`          |

Every line written to the sinks looks like

```
2025-01-01 12:00:00.123 [INF] [engine] [tid=1234] runtime/main_loop.cpp:33 | Engine starting: initializing subsystems
```

— timestamp, level, category, thread id, call site, message. The same
line goes to stderr and to `engine.log` beside the executable (so a
double-clicked `AlphaEngine.exe` whose console closes on exit still leaves
a trace). Both sinks are written under a mutex, so worker threads can log
without interleaving lines, and the file is closed by
`core::logging::shutdown()`, which `LOG_INIT` registers with `atexit`.

## Levels and runtime control

Messages below the effective level are dropped before they are formatted,
so a `LOG_TRC` in a hot loop costs a level comparison when trace is off.
The level is resolved once by `LOG_INIT` (in `runtime/main_loop.cpp`):

- the build-type default is `debug` for Debug configurations and `info`
  otherwise;
- the `ALPHAENGINE_LOG_LEVEL` environment variable overrides it. It takes
  a level name — `trace`, `debug`, `info`, `warn`, `error` or `fatal`,
  case-insensitive — optionally followed by per-category overrides:

  ```
  ALPHAENGINE_LOG_LEVEL=warn                  # global
  ALPHAENGINE_LOG_LEVEL=info,gpu=trace        # global + one category
  ALPHAENGINE_LOG_LEVEL=gpu=debug,assets=debug # overrides only
  ```

  An invalid specification is reported with `LOG_WRN` and ignored as a
  whole.

The same knobs are available programmatically — `set_level`,
`set_category_level`, `clear_category_levels`, `configure_levels`,
`level_for`, `is_enabled` in `core::logging` — which is what the tests
and a future in-engine console use. A `--log-level` command-line flag
is not parsed yet; `LOG_INIT` stashes `argc`/`argv` in
`core::logging::arguments()` for the command-line parser to consume.

SDL's own messages (category `sdl`) follow the global level, or a
`sdl=<level>` override.

## Categories

Every message carries a category. It defaults to `engine`; a translation
unit tags all of its messages by defining `LOG_CATEGORY` **before its
first include** (headers such as `runtime/node.hpp` pull `core/log.hpp`
in transitively, so a later definition would be a redefinition):

```cpp
#define LOG_CATEGORY "gpu"

#include <rendering_engine/gpu/backend/opengl/gl_device.hpp>
#include <core/log.hpp>
```

The call sites stay `LOG_INF(...)` etc. Categories are free-form strings;
`gpu`, `assets`, `scene` are the intended names for the corresponding
subsystems. A one-off category goes through `core::logging::message`
directly, which takes the category as its second argument.

## Fatal contract

`LOG_FTL` logs and flushes the sinks; it does **not** terminate the
process. The call site throws (or exits) immediately after it:

```cpp
if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
{
    LOG_FTL("Could not initialize video system: %s", SDL_GetError());
    throw std::runtime_error{"Could not initialize video system"};
}
```

That is what lets `main` catch the exception, show the error dialog
through `window::show_message` and tear the engine down. Never rely on
`LOG_FTL` to stop execution.

## Format checking

`core::logging::message` carries a printf-format attribute
(`ALPHAENGINE_PRINTF_FORMAT` in `core/log.hpp`), so GCC and Clang check
every `LOG_*` call site's conversions against its arguments with
`-Wformat`, which the CMake enables for the engine and test targets.
Mind `%zu` for `size_t`, `%u` for `unsigned` and `%lld` with an explicit
`long long` cast for 64-bit values. MSVC has no equivalent outside
`/analyze`, so the Linux builds are the ones that catch mismatches.

## Recent-message ring

The last `core::logging::k_recent_capacity` (512) messages that reached
the sinks are kept in memory as `core::logging::record`s — timestamp,
level, category, call site and text — and returned by
`core::logging::recent_messages()`, oldest first. It is thread-safe and
intended for a console panel in the debug overlay.

## Picking a level

1. Will this message fire more than a few times per second in steady state?
   → `LOG_TRC`, and keep the formatting cheap; it is dropped before
   formatting unless trace is enabled.
2. Does it describe loading a resource, a cache miss, a state transition
   that is useful when debugging but noise in a release log? → `LOG_DBG`.
3. Does it describe starting / stopping a subsystem, creating a window or
   device, compiling a shader, or parsing config? → `LOG_INF`.
4. Did something fail but the engine is carrying on (missing uniform, unknown
   event type, fallback path taken)? → `LOG_WRN`.
5. Did something fail in a way the caller has to handle (shader compile fail,
   image load fail) but we're still inside a `try`? → `LOG_ERR`.
6. Is the process about to die (GL context creation failed, SDL init failed,
   program link failed)? → `LOG_FTL`, then throw.

## Examples from the codebase

- Subsystem lifecycle — `runtime/main_loop.cpp`:
  `LOG_INF("Engine starting: initializing subsystems");`
- GL capability banner — `rendering_engine/gpu/backend/opengl/gl_device.cpp`:
  `LOG_INF("OpenGL renderer: %s", gl_renderer ? gl_renderer : "<unknown>");`
- Settings summary — `core/settings.cpp`:
  `LOG_INF("Settings parsed: window=%ux%u ...");`
- Module registration — `external/phong_demo_module.cpp`:
  `LOG_INF("Registering external module: phong_demo_module");`
- Recoverable fault — `rendering_engine/window.cpp`:
  `LOG_WRN("Could not set swap interval to %d: %s", swap_interval, SDL_GetError());`
- Unrecoverable fault — `rendering_engine/gpu/backend/opengl/gl_device_shader.cpp`:
  `LOG_FTL("create_shader_module: glCreateShader returned 0");` followed by
  `throw std::runtime_error{"shader creation failed"};`

## Adding new logs

- Include `<core/log.hpp>` (not `"api/log.hpp"` — that one is the
  public game-module API).
- Prefer a single well-formatted line with the relevant identifiers (shader
  id, program id, file path, event type) over multiple short lines.
- On failure paths, log the detail (info log, SDL error, file name) at the
  matching level before the `throw`, and include a short fatal-level summary
  so the tail of the log tells the story.
- Per-frame or per-draw logs are `LOG_TRC`; anything noisier than a few
  lines per second at INFO or DEBUG will drown the console.
