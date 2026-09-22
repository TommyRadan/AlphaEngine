# Settings

Engine-wide configuration lives in `core::settings` (`core/settings.hpp`): a
plain struct of per-subsystem blocks — `window`, `graphics`, `camera`,
`input` — that `runtime::engine` owns and every subsystem reads after
construction. `main` resolves it once with `core::load_settings(argc, argv)`
before constructing the engine (`--help` prints the usage text and exits
there) and the resolved values are logged once at INFO.

## Resolution order

Each layer overrides the one before it; a value no layer sets keeps the
compiled default.

1. **Compiled defaults** (`core::settings::settings()`): Debug builds run
   1600x900 windowed, Release builds fullscreen at the display's native size;
   vsync off, Vulkan backend, temporal AA on, 70° field of view, mouse
   sensitivity 0.005.
2. **`settings.json`** in `SDL_GetPrefPath("AlphaEngine", "AlphaEngine")`
   (for example `%APPDATA%\AlphaEngine\AlphaEngine\settings.json` on Windows,
   `~/.local/share/AlphaEngine/AlphaEngine/settings.json` on Linux), or the
   file named by `--settings <path>`. The path is logged at INFO; a missing
   file is the normal first run, a malformed one is warned about and skipped.
3. **Environment variables** (`ALPHAENGINE_*`, below).
4. **Command line** (below).

Every parser trims its input and matches names ignoring case; an
unrecognised or out-of-range value is reported with `LOG_WRN` and leaves the
setting unchanged — bad configuration never stops the engine from starting.

A width or height of `0` means "match the primary display". That query needs
SDL's video subsystem, so `rendering_engine::window::init` resolves it after
bringing video up (falling back to 1280x720 windowed with a warning if the
display cannot be queried) and writes the concrete size back into the
settings for everything that reads it later — the swapchain, the projection
matrix, the debug inspector.

## settings.json

Only the keys present are applied; an unknown key is warned about and
ignored, as is a value of the wrong JSON type or outside its range.

```json
{
    "window": {
        "width": 1920,
        "height": 1080,
        "mode": "borderless",
        "vsync": true,
        "title": "AlphaEngine"
    },
    "graphics": {
        "backend": "vulkan",
        "temporal_aa": true
    },
    "camera": {
        "fov_degrees": 70.0
    },
    "input": {
        "mouse_sensitivity": 0.005,
        "mouse_reversed": false
    }
}
```

| Key                              | Type             | Values                                   |
| -------------------------------- | ---------------- | ---------------------------------------- |
| `window.width`, `window.height`  | unsigned integer | 0 (match the display) to 16384           |
| `window.mode`                    | string           | `windowed`, `fullscreen`, `borderless`   |
| `window.vsync`                   | boolean          |                                          |
| `window.title`                   | string           |                                          |
| `graphics.backend`               | string           | `opengl`, `vulkan`                       |
| `graphics.temporal_aa`           | boolean          |                                          |
| `camera.fov_degrees`             | number           | 1 to 179                                 |
| `input.mouse_sensitivity`        | number           | 0.0001 to 10                             |
| `input.mouse_reversed`           | boolean          |                                          |

## Environment variables

| Variable                                 | Values                                                  |
| ---------------------------------------- | ------------------------------------------------------- |
| `ALPHAENGINE_WIDTH`, `ALPHAENGINE_HEIGHT` | unsigned integer, 0 = match the display                 |
| `ALPHAENGINE_WINDOW_MODE`                | `windowed`, `fullscreen`, `borderless`                  |
| `ALPHAENGINE_VSYNC`                      | `true`/`false`, `1`/`0`, `yes`/`no`, `on`/`off`         |
| `ALPHAENGINE_GRAPHICS_BACKEND`           | `opengl`, `vulkan`                                      |
| `ALPHAENGINE_TAA`                        | boolean, as above                                       |
| `ALPHAENGINE_LOG_LEVEL`                  | log level specification, read by `LOG_INIT` — see [logging.md](./logging.md) |

An unset or blank variable leaves its setting as it is.

## Command line

Both `--key value` and `--key=value` are accepted. An unknown option, a
missing value or an unparsable value is warned about and skipped; when an
option repeats, the last valid occurrence wins.

| Option                                       | Effect                                                        |
| -------------------------------------------- | ------------------------------------------------------------- |
| `--width <n>`, `--height <n>`                | window size in points, 0 = match the display                  |
| `--windowed`, `--fullscreen`, `--borderless` | window mode                                                   |
| `--backend <name>`                           | GPU backend, `opengl` or `vulkan`                             |
| `--vsync <bool>`                             | vertical sync, any boolean spelling above                     |
| `--log-level <spec>`                         | same specification as `ALPHAENGINE_LOG_LEVEL`; applied after it, so it wins |
| `--settings <path>`                          | read this file instead of the pref-path `settings.json`       |
| `-h`, `--help`                               | print the usage text and exit                                 |

## Testing

The parsers and the three layers are pure functions in
`core/settings_parse.hpp`: `apply_json` takes the document as text,
`apply_environment` takes a lookup callable and `parse_command_line` takes an
argument array, so `tests/core/settings_test.cpp` exercises them — including
the layering order — without touching the process environment, the file
system or SDL. Only `core::load_settings` in `core/settings.cpp` talks to the
real process (`SDL_GetPrefPath`, `std::getenv`, the file).
