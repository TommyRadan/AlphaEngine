# Unit-test suite. A standalone executable that links GoogleTest and compiles
# only the device-free subsystem translation units under test — no GPU, window,
# or renderer code is pulled in, so the suite builds and runs headless (this is
# what the Linux CI job exercises).
#
# Sources under test are listed explicitly rather than reusing the AlphaEngine
# target: that target is a single monolithic executable, so there is no library
# to link against. Compiling the handful of pure-logic TUs directly keeps the
# test binary lean and the wiring obvious.
#
# tests/ is intentionally kept out of the clang-format / clang-tidy scope
# (SOURCE_DIRS in .github/workflows/ci.yml): GoogleTest's macros and house
# style do not fit the snake_case naming gate enforced on engine code. See
# docs/testing.md.

set(ALPHAENGINE_TEST_SOURCES_UNDER_TEST
    # core (device-free)
    ${CMAKE_SOURCE_DIR}/core/math/aabb.cpp
    ${CMAKE_SOURCE_DIR}/core/math/frustum.cpp
    ${CMAKE_SOURCE_DIR}/core/math/mat3.cpp
    ${CMAKE_SOURCE_DIR}/core/math/mat4.cpp
    ${CMAKE_SOURCE_DIR}/core/math/math.cpp
    ${CMAKE_SOURCE_DIR}/core/math/quat.cpp
    ${CMAKE_SOURCE_DIR}/core/math/sphere.cpp
    ${CMAKE_SOURCE_DIR}/core/math/vec2.cpp
    ${CMAKE_SOURCE_DIR}/core/math/vec3.cpp
    ${CMAKE_SOURCE_DIR}/core/math/vec4.cpp
    ${CMAKE_SOURCE_DIR}/core/event_engine.cpp
    ${CMAKE_SOURCE_DIR}/core/jobs.cpp
    ${CMAKE_SOURCE_DIR}/core/log.cpp
    ${CMAKE_SOURCE_DIR}/core/version.cpp
    # runtime::node — scene-graph hierarchy (depends only on math + the
    # component store; no renderer or device).
    ${CMAKE_SOURCE_DIR}/runtime/node.cpp
    ${CMAKE_SOURCE_DIR}/rendering_engine/util/transform.cpp
    # asset_cache and the asset handles. These resolve their gpu device through
    # asset_device(), so the tests install a fake device and link none of the
    # gpu backends. device.cpp supplies the abstract device's out-of-line dtor;
    # util/image and util/font back the texture/font loaders the cache compiles.
    ${CMAKE_SOURCE_DIR}/rendering_engine/assets/asset_cache.cpp
    ${CMAKE_SOURCE_DIR}/rendering_engine/assets/asset_device.cpp
    ${CMAKE_SOURCE_DIR}/rendering_engine/assets/mesh_asset.cpp
    ${CMAKE_SOURCE_DIR}/rendering_engine/assets/texture_asset.cpp
    ${CMAKE_SOURCE_DIR}/rendering_engine/gpu/device.cpp
    ${CMAKE_SOURCE_DIR}/rendering_engine/util/image.cpp
    ${CMAKE_SOURCE_DIR}/rendering_engine/util/font.cpp
)

set(ALPHAENGINE_TEST_SOURCES
    core/math/vec_test.cpp
    core/math/mat_test.cpp
    core/math/quat_test.cpp
    core/math/bounds_test.cpp
    core/pool_test.cpp
    core/event_bus_test.cpp
    core/jobs_test.cpp
    runtime/node_test.cpp
    rendering_engine/assets/asset_cache_test.cpp
)

add_executable(AlphaEngineTests
    ${ALPHAENGINE_TEST_SOURCES}
    ${ALPHAENGINE_TEST_SOURCES_UNDER_TEST}
)

# tests/ for the support/ helpers (fake_device); vendor/stb for the
# header-only stb backends util/image and util/font compile against. The repo
# root is already on the include path via the top-level include_directories().
target_include_directories(AlphaEngineTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/vendor/stb
)

# glm backs core/math + transforms, SDL3 backs core/log, Threads backs
# core/jobs. The asset layer links no gpu backend — the fake device stands in.
target_link_libraries(AlphaEngineTests PRIVATE
    GTest::gtest_main
    glm::glm
    SDL3::SDL3-static
    Threads::Threads
)

# Register every TEST() with ctest so `ctest --output-on-failure` runs them.
# Discovery runs the freshly-built test binary with --gtest_list_tests at build
# time; the default 5s timeout is too tight for a cold-start MSVC Debug binary
# on CI (it intermittently times out), so give it generous headroom.
gtest_discover_tests(AlphaEngineTests DISCOVERY_TIMEOUT 60)
