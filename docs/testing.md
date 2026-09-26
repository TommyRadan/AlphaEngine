# Testing

AlphaEngine has a unit-test suite under `tests/`, built on **GoogleTest** and
run through **ctest**. The suite covers the engine's pure-logic core — the code
that needs no GPU, window, or audio device and is therefore trivially testable.

## Running the tests

The suite is built whenever the project is configured with
`-DALPHAENGINE_BUILD_TESTS=ON` (the default). GoogleTest is fetched from source
via `FetchContent`, matching the no-system-packages approach used for SDL3,
GLM, and glslang.

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target AlphaEngineTests
ctest --test-dir build --output-on-failure
```

To configure without the tests (skipping the GoogleTest fetch entirely):

```
cmake -S . -B build -DALPHAENGINE_BUILD_TESTS=OFF
```

CI runs the suite on Linux on every push/PR: the `build (linux-gcc)` and
`build (linux-clang)` jobs in `.github/workflows/ci.yml` run it through ctest
after compiling the full engine, and the `sanitizers (linux ...)` jobs run it
again under the sanitizers (below). Because the tests are device-free they run
headless — no GL or Vulkan context is created.

### Under the sanitizers

`-DALPHAENGINE_SANITIZE=<value>` (root `CMakeLists.txt`, GCC / Clang only)
passes `-fsanitize=<value> -fno-omit-frame-pointer` to every target, the
fetched SDL3 and GoogleTest included, so reports inside dependency code carry a
usable stack:

```
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DALPHAENGINE_SANITIZE=address,undefined
cmake --build build-asan --target AlphaEngineTests
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
  ctest --test-dir build-asan --output-on-failure
```

`halt_on_error=1` matters: UBSan keeps running after a report by default and
the process still exits 0. ThreadSanitizer cannot share a binary with ASan, so
it is a build of its own (`-DALPHAENGINE_SANITIZE=thread`); CI runs the suites
that exercise threads through it repeatedly, in one process, so a race that
needs an unlucky interleaving has a chance to surface:

```
Binaries/AlphaEngineTests --gtest_filter='jobs.*:time.*:event_bus.*:pool.*' --gtest_repeat=20
```

Every build directory writes its binaries to `Binaries/`, so build one
configuration at a time.

## How the test target is wired

The engine is built as a single monolithic executable (every subsystem adds its
sources to the `AlphaEngine` target via `target_sources`), so there is no
library to link a test binary against. Instead, `tests/CMakeLists.txt` defines a
standalone `AlphaEngineTests` executable that compiles the specific subsystem
translation units under test directly, alongside the test sources, and links
`GTest::gtest_main` plus only the libraries those sources need (GLM for
`core/math`, SDL3 for `core/log`, Threads for `core/jobs`, and — for the
shader tests — the embedded `shader_registry` library generated from
`shaders/` and the static glslang front end). Nothing from the renderer,
window, or GPU backend layers is pulled in (`mesh/tangent.cpp` is pure
geometry; the `gpu` header it includes declares types only; glslang compiles
GLSL to SPIR-V words in memory and needs no device).
The one Vulkan backend TU compiled, `gpu/backend/vulkan/vk_negotiate.cpp`, is a
set of pure functions over the constants in the Vulkan headers — the
format-support query is a callback — so it needs the SDK's include directory
and links no loader.

Tests are registered with ctest via `gtest_discover_tests`, so each `TEST()`
shows up as an individual ctest case.

## Current coverage

All device-free:

- `core/math` — `vec2/3/4`, `mat3/4`, `quat`, `aabb`, `sphere`, `frustum`, and
  the scalar `lerp`; including the row-vector `vec4 * mat4` overload (the
  transposed product, as `camera_module` relies on), `ortho` (box corners
  onto the NDC cube, invertible), `quat_look_at` (forward onto the direction,
  up kept upright), the general-case `sphere::merge` (partially overlapping
  and disjoint spheres give the tight enclosing sphere; an enclosed one comes
  back unchanged), and the documented NaN that `normalize` of a zero vector
  yields (no guard in the wrappers; callers test `length` first).
- `core::pool` — insert/get/erase, generation-based handle invalidation
  (including the wrap past generation 0), slot recycling, in-place
  construction/destruction of move-only and non-default-constructible values,
  and live-slot iteration (`for_each`, `begin`/`end`).
- `core::event_bus` — synchronous `emit`, type keying, listener order,
  subscription tokens (RAII unsubscribe, `reset` / `release`, move, manual
  `unsubscribe(id)`, a token outliving its bus), reentrancy (subscribe /
  unsubscribe / emit from inside a dispatch, nested dispatch), buffered
  `enqueue`/`flush` ordering, deferral of events enqueued during a flush.
- `core::jobs` — `parallel_for` coverage and correctness, `dispatch` +
  `wait_idle` completion, the exception boundary (a throwing job neither
  escapes nor wedges `wait_idle`), low-priority work not delaying a
  `parallel_for`, the documented `hardware_concurrency() - 1` worker sizing,
  `dispatch` and `parallel_for` interleaved (bursts between batches, a batch
  body that dispatches, a dispatched job that forks its own batch, several
  forking at once), and teardown: the destructor drains queued jobs, waits
  for one still executing, and finishes background work a batch jumped ahead
  of.
- `core::time` — the fixed-step accumulator (drain, remainder, clamp),
  constructor validation, and the wall-clock side: `perform_tick` /
  `delta_time` / `frame_count`, per-instance timestamps, and the FPS readings
  (bounds only — exact frame times are not reproducible).
- `core::logging` — level filtering (global and per-category), the
  `ALPHAENGINE_LOG_LEVEL` specification parser, the bounded recent-message
  ring, the command-line stash, and the contract that `LOG_FTL` returns so
  the caller can throw.
- `core::fnv1a_64` (`core/hash.hpp`) — the published FNV-1a reference vectors,
  `constexpr` evaluation, seeded chaining, and the incremental hasher agreeing
  with the one-shot digest over the concatenated input.
- `core::version` — the version string is the dotted triplet built from the
  `VERSION_MAJOR` / `VERSION_MINOR` / `VERSION_PATCH` definitions (the test
  binary receives the same ones), the build date has the predefined
  `__DATE__` layout, and both are stable across calls.
- `gpu::shader_library` — the embedded registry generated from `shaders/`
  serves every listed file by its `shaders/`-relative path, the generated
  `include/bindings.glsl` mirrors `gpu/shader_bindings.hpp`, an unknown path
  throws, the `PerFrame` / `Lights` blocks and the shadow lookups are declared
  in exactly one file, and (debug builds) an on-disk override root wins over
  the embedded copy and can be cleared again.
- `gpu::compile_glsl_to_spirv` — through the real glslang, headless: a shader
  that `#include`s from the library compiles (nested and repeated includes
  included), `#define`s are injected, a parse failure or unknown include
  throws with the shader name and glslang's log, `GRID_FADE_DISTANCE` works
  as a define variant, the on-disk SPIR-V cache misses once then hits (keyed
  apart by defines, off when disabled, recompiling a truncated blob), and
  **every embedded material, pass and compute shader compiles** — the check
  that catches an include or binding mistake before a GPU sees it.
- `runtime::node` — parent/child links and re-parenting, cached world matrices,
  `world_position` / `set_world_position`, `find`, active / effective-active
  flags, and the component-store attach/get/remove path.
- `runtime::context` — the per-frame traversal (only effectively active
  nodes update; a node detached from a disabled parent is active again), the
  deferred command queue (`defer_destroy` with its release callback,
  `defer_remove_component`, `defer_reparent`, `defer_set_active`, queue
  order, commands queued by commands), `on_destroy` dispatch when a store dies
  with live components, ancestor-cycle rejection in `node::add`, component
  migration on a cross-scene re-parent, and (debug builds, as a death test)
  the assert on an immediate mutation from inside a hook.
- The component lifecycle hooks, as ordered sequences over a
  `recording_component` (`tests/support/recording_component.hpp`, shared with
  the `runtime::context` suite) that implements all four: `on_attach` /
  `on_update` / `on_active_changed` / `on_destroy` in that order for one
  component, `add_component` to a disabled node (or under a disabled
  ancestor) attaching then hiding, a same-type replacement destroying the old
  before attaching the new, `remove_all_components`, moves between enabled
  and disabled parents flipping exactly once, `on_active_changed` and
  `on_update` reaching parents before children (children in insertion order,
  a disabled subtree skipped without touching its siblings), `on_update`
  seeing the settled world transform, what `~node` does (frees its
  components, detaches from its parent, orphans its children with their
  components and store intact), and `node::look_at` — as a root, under a
  translated parent, under a rotated parent (the forward axis is exact
  either way), with the given or default up axis, and as a no-op at the
  node's own position.
- `light_component` over the light registry (`light.cpp` is a plain vector of
  back-pointers, so it compiles headless) — a disabled node's light leaves
  `registered_lights()` and returns on enable, a disabled ancestor counts,
  a light added to a disabled node starts disabled, `on_update` tracks the
  node's world position, and removing the component unregisters the light.
- `pack_lights` (`rendering_engine/lighting/lights_ubo.cpp`) — the std140
  mirror structs match the GLSL `Lights` block byte-for-byte (sizes and
  offsets), packing fully overwrites the block, ambient lights sum into one
  premultiplied term, directional lights pack a unit direction and point
  lights their position / attenuation (both with colour premultiplied by
  intensity), each kind fills its array in input order, and lights past
  `max_directional_lights` / `max_point_lights` are dropped without blocking
  the other kind.
- `gpu::backend::handle_pool` (the slot allocator behind both device backends'
  handles) — insert/lookup/remove, generation-based rejection of a removed
  handle when its slot is recycled, that `clear()` advances generations so a
  handle from before a `quit()`/`init()` cycle never resolves, and that a
  `lookup` pointer stays valid across pool growth. Header-only, so no engine
  translation unit is compiled for it.
- `asset_cache` — dedup by structural key, builder-runs-only-on-miss,
  `collect_unused()` / weak-ref semantics, that a `mesh_asset` releases its
  GPU buffers when the last handle drops, that the `vertex_format` a
  builder declares is carried onto the asset (or demoted to `custom` when it
  contradicts the stride), and that `load_texture` requests `rgba8_srgb` by
  default and `rgba8_unorm` for `color_space::linear` (recording the format
  on the `texture_asset`, and keying the two spaces apart). The texture tests
  decode a tiny PPM written to the temp directory, so the real image loader
  runs headless.
- `util::image` / `util::color` — the packed 4-byte RGBA8 texel layout the
  upload sites rely on; deep copies, copy-and-swap assignment (larger over
  smaller, over an empty image, self-assignment), moves that leave the source
  empty, and that loading a missing file throws. Images are built in memory, so
  no decoder runs.
- `util::font` — a missing, empty, or non-font file throws instead of reading
  garbage (the success path needs a real TTF and the rasterizer, so it is not
  covered).
- `mesh` — an empty mesh reports zero vertices and its `vertices()` accessor is
  safe to call; `upload_obj` stores a copy.
- `vertex_format` (`rendering_engine/mesh/vertex.hpp`) — every vertex struct
  maps to its format and stride, the named strides pin the attribute offsets
  the built-in materials hard-code, and `vertex_format_compatible` admits
  exactly the prefix layouts (a position+uv reader accepts a tangent record;
  a tangent reader rejects a 32-byte position+uv+normal record).
- `generate_tangents` (`rendering_engine/mesh/tangent.cpp`) — tangent follows
  the u gradient, mirrored UVs flip the handedness sign, degenerate triangles
  contribute nothing, and shared vertices weight their triangles by corner
  angle.
- `camera_id` (the `external/api/camera.hpp` facade) — the slot-index /
  generation packing of the generational id and, over a `core::pool` like the
  one the facade allocates from, that a destroyed camera's id never resolves
  again once its slot is recycled. Only the header is exercised: the facade's
  translation unit links the renderer.
- `sdl_input` — the SDL keycode / mouse-button / gamepad-button / gamepad-axis
  to engine-enum translation behind `window::tick`: every named key maps, left
  and right modifiers stay distinct, an unnamed key yields `key_code::unknown`,
  an unnamed mouse button yields no code (so the window drops the event), and
  raw axis readings normalise to the unit range at both extremes. Only SDL's
  header constants are used; no video subsystem is initialised.
- `render_graph::frame_graph` — `pass_io_builder` bookkeeping, `compile()`'s
  produced-before-read check (imported resources, in-order writes, same-pass
  read+write, reads ahead of their producer), execution in registration order
  with `execute_range` clamping, `clear()`, and a hand-kept mirror of the
  engine's built-in pass declarations (including `scene_depth`) that must
  compile hazard-free. The encoder the graph forwards is driven by a stub.
- `vk_negotiate` (`rendering_engine/gpu/backend/vulkan/vk_negotiate.cpp`) —
  the Vulkan depth-format fallback chains (`depth24` prefers the packed
  24-bit format, falls back to `D32_SFLOAT` and reaches the stencil formats
  last; `depth32_float` resolves to `D32_SFLOAT`; `depth24_stencil8` never
  resolves to a depth-only format; an empty support set or a colour format
  resolves to nothing), the image aspect of a resolved format, and the
  descriptor-pool budget growth (base sizes, doubling per pool, the cap,
  and that every pool holds a full material set per allocated set). The
  support query is a table; no loader call is made.

### Testing the asset layer headless

`asset_cache` and the reference-counted asset handles (`texture_asset`,
`mesh_asset`) need a `gpu::device` to upload and free resources — including from
their destructors. They used to reach the device through the
`runtime::current_engine()` global, which would have pulled the whole engine
(window, both gpu backends, glslang) into anything that links them.

They now resolve the device through a small accessor,
`rendering_engine::asset_device()` (declared in
`rendering_engine/assets/asset_device.hpp`). The engine installs the live
device via `set_asset_device()` once the renderer has brought it up, and clears
it as the device is torn down — so production behaviour is identical. Tests
install a lightweight `test_support::fake_device` (see
`tests/support/fake_device.hpp`) that hands back unique handles and records
create/destroy traffic, then exercise the cache with no engine, window, or
backend present. This is what keeps the asset-cache tests in the same lean,
headless binary as the rest of the suite.

The fake device does not reach further than the asset layer. `material`
(pipeline construction), every renderable's `collect_draw_items`, the passes
(including the shadow pass's light-frustum fit, a file-local helper of
`shadow_pass.cpp`), `runtime::engine` and the `external/api` translation units
all resolve the device through `runtime::current_engine()`, so they cannot be
compiled into this binary without the whole engine; they stay uncovered until
that seam is widened.

## Style and naming scope

`tests/` is intentionally **excluded** from the clang-format and clang-tidy
gates (it is not listed in `SOURCE_DIRS` in `.github/workflows/ci.yml`, which
covers `runtime core rendering_engine external`). GoogleTest's macros and house
style — `TEST(suite, name)`, `EXPECT_*` — do not fit the strict `snake_case`
naming gate enforced on engine code, and the framework headers are vendored
third-party code. Test code still follows the surrounding conventions where it
can (snake_case test and suite names, Allman braces, 4-space indent).
