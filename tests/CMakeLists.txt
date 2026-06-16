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
)

set(ALPHAENGINE_TEST_SOURCES
    core/math/vec_test.cpp
    core/math/mat_test.cpp
    core/math/quat_test.cpp
    core/math/bounds_test.cpp
    core/pool_test.cpp
    core/event_bus_test.cpp
    core/jobs_test.cpp
)

add_executable(AlphaEngineTests
    ${ALPHAENGINE_TEST_SOURCES}
    ${ALPHAENGINE_TEST_SOURCES_UNDER_TEST}
)

# The subsystem sources resolve <core/...> against the repo root, which the
# top-level include_directories() already publishes. glm backs core/math, SDL3
# backs core/log, and Threads backs core/jobs.
target_link_libraries(AlphaEngineTests PRIVATE
    GTest::gtest_main
    glm::glm
    SDL3::SDL3-static
    Threads::Threads
)

# Register every TEST() with ctest so `ctest --output-on-failure` runs them.
gtest_discover_tests(AlphaEngineTests)
