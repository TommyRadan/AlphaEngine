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
    ${CMAKE_SOURCE_DIR}/core/subscription.cpp
    ${CMAKE_SOURCE_DIR}/core/time.cpp
    ${CMAKE_SOURCE_DIR}/core/version.cpp
    # runtime::node + runtime::context — scene-graph hierarchy, traversal and
    # the deferred command queue (depend only on math + the component store;
    # no renderer or device).
    ${CMAKE_SOURCE_DIR}/runtime/node.cpp
    ${CMAKE_SOURCE_DIR}/runtime/scene_graph.cpp
    ${CMAKE_SOURCE_DIR}/rendering_engine/util/transform.cpp
    # light_component over the light registry — a plain vector of
    # back-pointers, no device — so a disabled node's light can be checked to
    # leave registered_lights().
    ${CMAKE_SOURCE_DIR}/runtime/components/light_component.cpp
    ${CMAKE_SOURCE_DIR}/rendering_engine/lighting/light.cpp
    ${CMAKE_SOURCE_DIR}/rendering_engine/lighting/directional_light.cpp
    ${CMAKE_SOURCE_DIR}/rendering_engine/lighting/point_light.cpp
    # mesh/tangent — pure geometry over core/math (the gpu header it includes
    # is types only, no device).
    ${CMAKE_SOURCE_DIR}/rendering_engine/mesh/tangent.cpp
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
    # mesh — the CPU-side vertex store behind model::upload_mesh (math only).
    ${CMAKE_SOURCE_DIR}/rendering_engine/mesh/mesh.cpp
    # sdl_input — the SDL keycode / button / axis -> engine enum translation
    # the window layer uses. Pure functions over SDL's header constants; no
    # video subsystem is initialised.
    ${CMAKE_SOURCE_DIR}/rendering_engine/sdl_input.cpp
    # render_graph::frame_graph — declared-I/O validation and ordered
    # execution over callbacks (log only). The encoder it forwards is the
    # abstract gpu interface whose vtable anchor lives in command_encoder.cpp;
    # the tests drive it with a stub, no backend is linked.
    ${CMAKE_SOURCE_DIR}/rendering_engine/render_graph/frame_graph.cpp
    ${CMAKE_SOURCE_DIR}/rendering_engine/gpu/command_encoder.cpp
    # vk_negotiate — the Vulkan backend's depth-format fallback chains and
    # descriptor-pool budgets. Pure functions over Vulkan's header constants
    # (the format-support query is a callback); no loader call is made, so
    # only the Vulkan headers are needed and nothing links the library.
    ${CMAKE_SOURCE_DIR}/rendering_engine/gpu/backend/vulkan/vk_negotiate.cpp
)

set(ALPHAENGINE_TEST_SOURCES
    core/math/vec_test.cpp
    core/math/mat_test.cpp
    core/math/quat_test.cpp
    core/math/bounds_test.cpp
    core/pool_test.cpp
    core/event_bus_test.cpp
    core/jobs_test.cpp
    core/log_test.cpp
    core/time_test.cpp
    runtime/node_test.cpp
    runtime/scene_graph_test.cpp
    runtime/light_component_test.cpp
    rendering_engine/assets/asset_cache_test.cpp
    rendering_engine/mesh/mesh_test.cpp
    rendering_engine/mesh/tangent_test.cpp
    rendering_engine/mesh/vertex_format_test.cpp
    rendering_engine/sdl_input_test.cpp
    rendering_engine/render_graph/frame_graph_test.cpp
    rendering_engine/gpu/vk_format_fallback_test.cpp
    rendering_engine/util/font_test.cpp
    rendering_engine/util/image_test.cpp
    external/camera_id_test.cpp
)

add_executable(AlphaEngineTests
    ${ALPHAENGINE_TEST_SOURCES}
    ${ALPHAENGINE_TEST_SOURCES_UNDER_TEST}
)

# tests/ for the support/ helpers (fake_device); vendor/stb for the
# header-only stb backends util/image and util/font compile against; the
# Vulkan SDK headers (found by the root find_package(Vulkan)) for
# vk_negotiate, which uses the VkFormat constants and nothing from the
# loader. The repo root is already on the include path via the top-level
# include_directories().
target_include_directories(AlphaEngineTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/vendor/stb
    ${Vulkan_INCLUDE_DIRS}
)

# glm backs core/math + transforms, SDL3 backs core/log, Threads backs
# core/jobs. The asset layer links no gpu backend — the fake device stands in.
target_link_libraries(AlphaEngineTests PRIVATE
    GTest::gtest_main
    glm::glm
    SDL3::SDL3-static
    Threads::Threads
)

# Same printf-format checking as the AlphaEngine target (see the root
# CMakeLists.txt): the LOG_* call sites in the sources under test are
# validated here too, which is what the Linux CI job compiles.
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(AlphaEngineTests PRIVATE -Wformat)
endif()

# Register every TEST() with ctest so `ctest --output-on-failure` runs them.
# Discovery runs the freshly-built test binary with --gtest_list_tests at build
# time; the default 5s timeout is too tight for a cold-start MSVC Debug binary
# on CI (it intermittently times out), so give it generous headroom.
gtest_discover_tests(AlphaEngineTests DISCOVERY_TIMEOUT 60)
