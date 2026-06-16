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

Not yet covered (tracked as follow-up): `runtime::node` (transform hierarchy)
and `asset_cache` (dedup / `collect_unused` weak-ref semantics). `asset_cache`
reaches its GPU device through the `runtime::current_engine()` global, so
testing it headless needs a fake `gpu::device` and a decision on how to inject
it — deferred to a later change.

## Style and naming scope

`tests/` is intentionally **excluded** from the clang-format and clang-tidy
gates (it is not listed in `SOURCE_DIRS` in `.github/workflows/ci.yml`, which
covers `runtime core rendering_engine external`). GoogleTest's macros and house
style — `TEST(suite, name)`, `EXPECT_*` — do not fit the strict `snake_case`
naming gate enforced on engine code, and the framework headers are vendored
third-party code. Test code still follows the surrounding conventions where it
can (snake_case test and suite names, Allman braces, 4-space indent).
