# Shaders

AlphaEngine's shaders are GLSL files under `shaders/`, embedded into the
binary at build time and compiled to SPIR-V with glslang at runtime. Both
GPU backends consume the same SPIR-V: Vulkan natively, OpenGL 4.6 through
`glShaderBinary` + `glSpecializeShader` (ARB_gl_spirv). No GLSL text ever
reaches a driver.

## Layout of `shaders/`

```
shaders/
  CMakeLists.txt            explicit list of every file below (no GLOB)
  include/                  #include-able pieces, never compiled on their own
    bindings.glsl           GENERATED from rendering_engine/gpu/shader_bindings.hpp
    constants.glsl          PI
    per_frame.glsl          the PerFrame view block (set 0, binding 0)
    per_draw.glsl           the PerDraw model-matrix block (set 1, binding 1)
    lights.glsl             the Lights block + DirectionalLight / PointLight
    shadows.glsl            directional_shadow() (5x5 PCF), point_shadow() (omni)
    fog.glsl                apply_fog()
    brdf.glsl               GGX / Schlick-GGX / Fresnel terms and brdf()
    depth_utils.glsl        depth_to_ndc(), linearize_depth() for scene-depth readers
    ibl_common.glsl         cube-face mapping, Hammersley, GGX importance sampling
  materials/                one .vert.glsl + .frag.glsl per built-in material
    basic, grid, instanced, line, phong, points, standard, ui
  passes/                   pass-private stages and the IBL compute kernels
    fullscreen.vert.glsl    the shared fullscreen-triangle vertex stage
    shadow.{vert,frag}      depth-only, shared by the directional and omni passes
    skybox.{vert,frag}
    bloom_{threshold,blur,composite}.frag, fxaa.frag, taa_{resolve,copy}.frag,
    tonemap.frag, velocity.frag
    ibl_{irradiance,prefilter,brdf_lut}.comp
```

A shader is named by its path relative to `shaders/`, forward slashes:
`"materials/standard.frag.glsl"`, `"include/fog.glsl"`. That name is what
`gpu::shader_library::source()` looks up, what a `#include` directive
spells, and what appears in compiler diagnostics.

## Include rules

- Every compiled shader starts with `#version 450`. `#include` lines come
  after it and name the file by its library path, always from the root:
  `#include "include/fog.glsl"`, never `#include "fog.glsl"` or a relative
  `../` path. Both `"..."` and `<...>` resolve the same way.
- Include files carry an `#ifndef AE_<NAME>_GLSL` guard and may include
  other includes (`fog.glsl` pulls in `per_frame.glsl`, which pulls in
  `bindings.glsl`). Guards are what make repeated and nested includes safe;
  there is no `#pragma once`.
- Shared declarations live in exactly one include. The `PerFrame` block is
  declared only in `per_frame.glsl`, the `Lights` block only in
  `lights.glsl`, the shadow lookups only in `shadows.glsl`; a material that
  needs them includes the file rather than restating the block (the unit
  test `shader_library.shared_blocks_are_declared_once` enforces this).
- Include files take everything they need as parameters. `apply_fog` and
  the shadow lookups receive the fragment's world position and the camera
  position explicitly instead of reaching for an `in` variable the
  including shader happens to declare.
- Bindings are never literal numbers in a scene shader: use the
  `BINDING_*` macros from `include/bindings.glsl`. Pass-private pipelines
  (the post chain, the shadow passes, the IBL kernels) bind nothing from
  the scene's per-frame set and number their few resources locally from 0.
- Keep the files ASCII: MSVC compiles the embedded literals under the
  system code page unless told otherwise.

The compiler enables `GL_GOOGLE_include_directive` on every shader's
behalf, so no `#extension` line is needed in the source.

## The binding table

`rendering_engine/gpu/shader_bindings.hpp` owns the global binding
numbers. The OpenGL backend flattens `layout(set, binding)` pairs into one
namespace per resource class, so every resource a scene pipeline can see
needs a number unique across all of its descriptor sets; Vulkan only needs
uniqueness within a set, so the one numbering serves both. The build
generates `shaders/include/bindings.glsl` from the header
(`cmake/generate_shader_bindings.cmake`): each `constexpr uint32_t name = N;`
becomes `#define BINDING_NAME N`. C++ spells `gpu::shader_bindings::name`,
GLSL spells `BINDING_NAME`, and neither can drift from the other. To add a
binding, add a constant to the header; the generated file is never edited
and is never on disk in the source tree.

| binding | set | resource                                   |
|--------:|:---:|--------------------------------------------|
| 0       | 0   | `PerFrame` view block (scene pass)          |
| 1       | 1   | `PerDraw` model matrix (every renderable)   |
| 2       | 0   | `Lights`                                    |
| 3       | 2   | material params block                       |
| 4-8     | 2   | albedo, normal, metalness, roughness, emissive maps |
| 9, 10   | 0   | directional shadow map, `Shadow` block       |
| 11-13   | 2   | IBL irradiance cube, prefiltered cube, BRDF LUT |
| 14      | 0   | `PointShadow` block                          |
| 15-20   | 0   | the six omni shadow faces                    |

## Adding or changing a shader

1. Put the file under `shaders/materials/`, `shaders/passes/` or
   `shaders/include/` with the `.vert.glsl` / `.frag.glsl` / `.comp.glsl`
   suffix (or plain `.glsl` for an include).
2. List it in `shaders/CMakeLists.txt`. The list is explicit; a file that
   is not listed is not embedded and `#include` cannot find it.
3. Reference it from C++ by path: a material hands
   `gpu::shader_variant{"materials/x.frag.glsl"}` (plus any defines) to
   `material::construct_pipeline`; a pass calls
   `gpu::compile_library_shader("passes/x.frag.glsl", stage)`.
4. The unit test `shader_compiler.every_embedded_shader_compiles` compiles
   every non-include file through glslang headless, so a build of
   `AlphaEngineTests` plus `ctest` catches include and syntax mistakes
   without a GPU.

Editing a shader and rebuilding regenerates the registry
(`build/generated/shaders/shader_registry.cpp`) and relinks; nothing else
recompiles.

### Variants and defines

`gpu::shader_compile_options::defines` (or the `defines` of a
`gpu::shader_variant`) are injected ahead of the source as
`#define NAME VALUE`. `grid_material` is the example: its fragment shader
reads `GRID_FADE_DISTANCE`, carries a default under `#ifndef` so the file
compiles on its own, and the material defines it per instance so each
fade distance is its own pipeline. Every distinct define set is a separate
cache entry.

## Compilation and the SPIR-V cache

`gpu::compile_glsl_to_spirv(source, stage, options)` runs glslang with an
includer that resolves against the shader library, and throws
`std::runtime_error` on failure; the message carries the shader name, the
stage and glslang's full log (the same text is logged with `LOG_ERR`).
`gpu::compile_library_shader(path, stage, defines)` is the usual entry
point and uses the path as the name.

Compiled blobs are cached on disk so a launch that compiles what a
previous launch compiled reads the SPIR-V back instead of running glslang.
The key is an FNV-1a digest of the source text, every file it transitively
includes (found by a textual scan of `#include` lines, so a change to any
include invalidates its users), the defines, the stage and the glslang
version. Each entry is `<key>.spv`, written through a temporary and
renamed into place; a blob that fails a size / magic-number check is
recompiled and overwritten.

- Location: `SDL_GetPrefPath("AlphaEngine", "AlphaEngine")/shader_cache/`
  (for example `~/.local/share/AlphaEngine/AlphaEngine/shader_cache/` on
  Linux, `%APPDATA%\AlphaEngine\AlphaEngine\shader_cache\` on Windows).
  The directory is logged at INFO on first use.
- `ALPHAENGINE_SHADER_CACHE=0` (or `off` / `false`) disables it; any other
  value is used as the cache directory.
- `gpu::set_shader_cache_directory()` overrides both from code (the tests
  point it at a scratch directory or turn it off).
- The renderer logs the hit / miss counts at DEBUG once the built-in
  pipelines are up (`Shader cache: N hits, M misses`).

glslang's optimizer (`ENABLE_OPT`) stays off: the engine's shallow tag
fetch of glslang carries no SPIRV-Tools, and glslang's CMake hard-errors
with the optimizer on unless it is present. Turning it on means fetching
SPIRV-Tools and SPIRV-Headers as well.

## Editing shaders without a C++ rebuild (debug builds)

Debug builds can read shaders from disk. When an override root is set and
`<root>/<path>` exists, its text wins over the embedded copy, so a shader
under `shaders/` can be edited and picked up at the next launch. The root
is, in order of precedence:

1. `ALPHAENGINE_SHADER_DIR=<directory>` (set it to `0` / `off` / empty to
   force the embedded copies);
2. the source tree's `shaders/` directory, baked in at build time as
   `ALPHAENGINE_SHADER_SOURCE_DIR`.

The generated `include/bindings.glsl` is never on disk and always comes
from the embedded registry. Files are read once per process; a running
engine has to be restarted to see an edit. Release builds ignore the
override entirely and touch no files.

Live reload (polling `last_write_time`, recompiling and rebuilding the
affected pipelines) is a follow-on; the override root is the piece it
needs. So are a `VkPipelineCache` serialised across runs and
`glGetProgramBinary` blobs on the OpenGL side — the SPIR-V cache covers
the front end only, the driver still builds its pipelines at startup.
