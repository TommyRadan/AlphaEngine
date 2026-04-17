# Logging

AlphaEngine logs through the thin wrapper in `infrastructure/log.hpp`, which
forwards to [loguru](https://github.com/emilk/loguru). Four macros are
available today:

| Macro     | Verbosity | Use for                                                     |
| --------- | --------- | ----------------------------------------------------------- |
| `LOG_INF` | INFO      | Subsystem lifecycle, version banner, parsed settings, one-shot registrations |
| `LOG_WRN` | WARNING   | Recoverable faults, fallbacks, missing optional data        |
| `LOG_ERR` | ERROR     | Unrecoverable faults that do not abort the process          |
| `LOG_FTL` | FATAL     | Unrecoverable faults that precede `throw` / `exit`          |

> The conventions in issue #24 also call for `TRACE` (hot paths) and `DEBUG`
> (lifecycle / resource loads) levels. The current wrapper does not expose
> those — adding them is out of scope for this change. Treat "what would have
> been DEBUG" as INFO for now, and do not add per-frame / per-draw TRACE calls
> until the wrapper grows a TRACE macro.

## Picking a level

1. Will this message fire more than a few times per second in steady state?
   If yes, don't add it yet — we have no TRACE sink.
2. Does it describe starting / stopping a subsystem, creating a window or GL
   context, compiling a shader, or parsing config? → `LOG_INF`.
3. Did something fail but the engine is carrying on (missing uniform, unknown
   event type, fallback path taken)? → `LOG_WRN`.
4. Did something fail in a way the caller has to handle (shader compile fail,
   image load fail) but we're still inside a `try`? → `LOG_ERR`.
5. Is the process about to die (GL context creation failed, SDL init failed,
   program link failed)? → `LOG_FTL`, then throw.

## Examples from the codebase

- Subsystem lifecycle — `control/main_loop.cpp`:
  `LOG_INF("Engine starting: initializing subsystems");`
- GL capability banner — `rendering_engine/opengl/opengl.cpp`:
  `LOG_INF("OpenGL renderer: %s", gl_renderer);`
- Settings summary — `infrastructure/settings.cpp`:
  `LOG_INF("Settings parsed: window=%ux%u ...");`
- Module registration — `external/cube_module.cpp`:
  `LOG_INF("Registering external module: cube_module");`
- Recoverable fault — `rendering_engine/renderers/renderer.cpp`:
  `LOG_WRN("Cannot find uniform: %s", name.c_str());`
- Unrecoverable fault — `rendering_engine/opengl/shader.cpp`:
  `LOG_FTL("Cannot compile shader");`

## Adding new logs

- Include `<infrastructure/log.hpp>` (not `"api/log.hpp"` — that one is the
  public game-module API).
- Prefer a single well-formatted line with the relevant identifiers (shader
  id, program id, file path, event type) over multiple short lines.
- On failure paths, log the detail (info log, SDL error, file name) at the
  matching level before the `throw`, and include a short fatal-level summary
  so the tail of the log tells the story.
- Do not emit per-frame or per-draw logs without gating them — they will
  drown the console.
