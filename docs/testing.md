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

CI runs the suite on Linux on every push/PR (the `unit-tests (linux)` job in
`.github/workflows/ci.yml`). Because the tests are device-free they run
headless — no GL or Vulkan context is created.

## How the test target is wired

The engine is built as a single monolithic executable (every subsystem adds its
sources to the `AlphaEngine` target via `target_sources`), so there is no
library to link a test binary against. Instead, `tests/CMakeLists.txt` defines a
standalone `AlphaEngineTests` executable that compiles the specific subsystem
translation units under test directly, alongside the test sources, and links
`GTest::gtest_main` plus only the libraries those sources need (GLM for
`core/math`, SDL3 for `core/log`, Threads for `core/jobs`). Nothing from the
renderer, window, or GPU layers is pulled in.

Tests are registered with ctest via `gtest_discover_tests`, so each `TEST()`
shows up as an individual ctest case.

## Current coverage

All device-free:

- `core/math` — `vec2/3/4`, `mat3/4`, `quat`, `aabb`, `sphere`, `frustum`, and
  the scalar `lerp`.
- `core::pool` — insert/get/erase, generation-based handle invalidation, slot
  recycling.
- `core::event_bus` — synchronous `emit`, type keying, buffered
  `enqueue`/`flush` ordering, deferral of events enqueued during a flush.
- `core::jobs` — `parallel_for` coverage and correctness, `dispatch` +
  `wait_idle` completion.
- `runtime::node` — parent/child links and re-parenting, cached world matrices,
  `world_position` / `set_world_position`, `find`, active / effective-active
  flags, and the component-store attach/get/remove path.
- `asset_cache` — dedup by structural key, builder-runs-only-on-miss,
  `collect_unused()` / weak-ref semantics, and that a `mesh_asset` releases its
  GPU buffers when the last handle drops.

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

## Style and naming scope

`tests/` is intentionally **excluded** from the clang-format and clang-tidy
gates (it is not listed in `SOURCE_DIRS` in `.github/workflows/ci.yml`, which
covers `runtime core rendering_engine external`). GoogleTest's macros and house
style — `TEST(suite, name)`, `EXPECT_*` — do not fit the strict `snake_case`
naming gate enforced on engine code, and the framework headers are vendored
third-party code. Test code still follows the surrounding conventions where it
can (snake_case test and suite names, Allman braces, 4-space indent).
