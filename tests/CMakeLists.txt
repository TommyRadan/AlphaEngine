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
    ${CMAKE_SOURCE_DIR}/core/math/easing.cpp
    ${CMAKE_SOURCE_DIR}/core/math/frustum.cpp
    ${CMAKE_SOURCE_DIR}/core/math/mat3.cpp
    ${CMAKE_SOURCE_DIR}/core/math/mat4.cpp
    ${CMAKE_SOURCE_DIR}/core/math/math.cpp
    ${CMAKE_SOURCE_DIR}/core/math/quat.cpp
    ${CMAKE_SOURCE_DIR}/core/math/sphere.cpp
    ${CMAKE_SOURCE_DIR}/core/math/trs.cpp
    ${CMAKE_SOURCE_DIR}/core/math/vec2.cpp
    ${CMAKE_SOURCE_DIR}/core/math/vec3.cpp
    ${CMAKE_SOURCE_DIR}/core/math/vec4.cpp
    ${CMAKE_SOURCE_DIR}/core/event_engine.cpp
    ${CMAKE_SOURCE_DIR}/core/jobs.cpp
    ${CMAKE_SOURCE_DIR}/core/log.cpp
    # platform — the OS services core/log, core/time and core/settings go
    # through (clock, paths, file I/O, native log capture). platform_sdl.cpp
    # and dynamic_library.cpp are the SDL-backed halves; nothing here brings a
    # video subsystem up.
    ${CMAKE_SOURCE_DIR}/core/platform/directory_watcher.cpp
    ${CMAKE_SOURCE_DIR}/core/platform/dynamic_library.cpp
    ${CMAKE_SOURCE_DIR}/core/platform/platform.cpp
    ${CMAKE_SOURCE_DIR}/core/platform/platform_sdl.cpp
    # vfs — the mount table util/image, util/font, the asset cache and the
    # glTF importer read through; with nothing mounted every path is native,
    # which is what the temp-file fixtures rely on.
    ${CMAKE_SOURCE_DIR}/core/vfs/directory_mount.cpp
    ${CMAKE_SOURCE_DIR}/core/vfs/vfs.cpp
    # settings — the parsers and layers are pure; settings.cpp only reaches
    # the platform layer for the pref path inside load_settings, which the
    # tests do not call.
    ${CMAKE_SOURCE_DIR}/core/settings.cpp
    ${CMAKE_SOURCE_DIR}/core/settings_parse.cpp
    ${CMAKE_SOURCE_DIR}/core/string_id.cpp
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
    ${CMAKE_SOURCE_DIR}/rendering_engine/lighting/ambient_light.cpp
    ${CMAKE_SOURCE_DIR}/rendering_engine/lighting/directional_light.cpp
    ${CMAKE_SOURCE_DIR}/rendering_engine/lighting/point_light.cpp
    # camera + camera_registry + the two projections, and camera_component
    # over them. The cameras are math over their transform (the perspective
    # camera takes its field of view and aspect from the caller; the
    # renderer, not the camera, reads the window) and the registry is the
    # same plain vector of back-pointers as the light registry, so the
    # projection dirty flag, the view derivation and the arbitration all run
    # device-free.
    ${CMAKE_SOURCE_DIR}/rendering_engine/camera/camera.cpp
    ${CMAKE_SOURCE_DIR}/rendering_engine/camera/camera_registry.cpp
    ${CMAKE_SOURCE_DIR}/rendering_engine/camera/orthographic_camera.cpp
    ${CMAKE_SOURCE_DIR}/rendering_engine/camera/perspective_camera.cpp
    ${CMAKE_SOURCE_DIR}/runtime/components/camera_component.cpp
    # mesh/tangent — pure geometry over core/math (the gpu header it includes
    # is types only, no device).
    ${CMAKE_SOURCE_DIR}/rendering_engine/mesh/tangent.cpp
    # asset_cache and the asset handles. These resolve their gpu device through
    # asset_device(), so the tests install a fake device and link none of the
    # gpu backends. device.cpp supplies the abstract device's out-of-line dtor;
    # util/image and util/font back the texture/font loaders the cache compiles.
    ${CMAKE_SOURCE_DIR}/rendering_engine/assets/asset_cache.cpp
    ${CMAKE_SOURCE_DIR}/rendering_engine/assets/asset_device.cpp
    ${CMAKE_SOURCE_DIR}/rendering_engine/assets/font_asset.cpp
    ${CMAKE_SOURCE_DIR}/rendering_engine/assets/mesh_asset.cpp
    ${CMAKE_SOURCE_DIR}/rendering_engine/assets/texture_asset.cpp
    # texture_decode — the CPU half of a texture load the cache calls (stb_image,
    # or libktx for .ktx2), linked against ktx_read below.
    ${CMAKE_SOURCE_DIR}/rendering_engine/assets/texture_decode.cpp
    ${CMAKE_SOURCE_DIR}/rendering_engine/gpu/device.cpp
    ${CMAKE_SOURCE_DIR}/rendering_engine/util/image.cpp
    ${CMAKE_SOURCE_DIR}/rendering_engine/util/font.cpp
    # gltf_importer — parses through the vendored cgltf header and builds
    # meshes / textures through the asset cache above; materials go through
    # the gltf_material_factory interface, so the tests pass a recorder and
    # the renderer-backed factory (gltf_material_factory.cpp) stays out.
    ${CMAKE_SOURCE_DIR}/rendering_engine/assets/gltf_importer.cpp
    # The skeleton and clip assets the importer builds from skins and
    # animations (math only).
    ${CMAKE_SOURCE_DIR}/rendering_engine/animation/animation_clip.cpp
    ${CMAKE_SOURCE_DIR}/rendering_engine/animation/skeleton.cpp
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
    # passes/projection_jitter — the temporal-AA jitter sequence and the
    # clip-space shift it applies to a projection (math only).
    ${CMAKE_SOURCE_DIR}/rendering_engine/passes/projection_jitter.cpp
    # gpu/shader_library + gpu/shader_compiler — the embedded shader
    # registry (linked below as the shader_registry library generated from
    # shaders/) and the glslang front end behind every pipeline. Compiling
    # every embedded shader here, headless, is what catches an #include or
    # binding mistake before a GPU sees it; no backend is linked.
    ${CMAKE_SOURCE_DIR}/rendering_engine/gpu/shader_compiler.cpp
    ${CMAKE_SOURCE_DIR}/rendering_engine/gpu/shader_library.cpp
    # materials — the variant key / keyword mapping (pure), the template
    # with its pipeline-variant cache and the instance base, plus the
    # standard, line and ui facades. They take their gpu::device from the
    # template (never the engine global), so the tests build them over the
    # fake device and compile the real standard shaders through glslang to
    # prove N instances share one pipeline. environment's texture getters
    # are inline, so standard_material links without the IBL builder.
    ${CMAKE_SOURCE_DIR}/rendering_engine/materials/material.cpp
    ${CMAKE_SOURCE_DIR}/rendering_engine/materials/material_template.cpp
    ${CMAKE_SOURCE_DIR}/rendering_engine/materials/pipeline_variant.cpp
    ${CMAKE_SOURCE_DIR}/rendering_engine/materials/standard_material.cpp
    ${CMAKE_SOURCE_DIR}/rendering_engine/materials/line_material.cpp
    ${CMAKE_SOURCE_DIR}/rendering_engine/materials/ui_material.cpp
    # renderables/per_draw_ubo — the PerDraw block (model + normal matrix,
    # mirror detection) over core/math and the abstract device.
    ${CMAKE_SOURCE_DIR}/rendering_engine/renderables/per_draw_ubo.cpp
    # vk_negotiate — the Vulkan backend's depth-format fallback chains and
    # descriptor-pool budgets. Pure functions over Vulkan's header constants
    # (the format-support query is a callback); no loader call is made, so
    # only the Vulkan headers are needed and nothing links the library.
    ${CMAKE_SOURCE_DIR}/rendering_engine/gpu/backend/vulkan/vk_negotiate.cpp
    # vk_staging_ring — the offset / alignment / wrap / reclaim arithmetic of
    # the backend's staging ring, over plain integers (no Vulkan header at
    # all; the device drives it against the real buffer and fences).
    ${CMAKE_SOURCE_DIR}/rendering_engine/gpu/backend/vulkan/vk_staging_ring.cpp
)

set(ALPHAENGINE_TEST_SOURCES
    core/math/vec_test.cpp
    core/math/mat_test.cpp
    core/math/quat_test.cpp
    core/math/bounds_test.cpp
    core/math/conventions_test.cpp
    core/pool_test.cpp
    core/event_bus_test.cpp
    core/jobs_test.cpp
    core/log_test.cpp
    core/hash_test.cpp
    core/settings_test.cpp
    core/time_test.cpp
    runtime/node_test.cpp
    runtime/scene_graph_test.cpp
    runtime/light_component_test.cpp
    runtime/camera_component_test.cpp
    rendering_engine/assets/asset_cache_test.cpp
    rendering_engine/assets/gltf_importer_test.cpp
    rendering_engine/camera/aspect_ratio_test.cpp
    rendering_engine/camera/camera_test.cpp
    rendering_engine/util/transform_test.cpp
    rendering_engine/gpu/handle_pool_test.cpp
    rendering_engine/gpu/shader_compiler_test.cpp
    rendering_engine/gpu/shader_library_test.cpp
    rendering_engine/materials/material_template_test.cpp
    rendering_engine/materials/pipeline_variant_test.cpp
    rendering_engine/mesh/mesh_test.cpp
    rendering_engine/mesh/tangent_test.cpp
    rendering_engine/mesh/vertex_format_test.cpp
    rendering_engine/sdl_input_test.cpp
    rendering_engine/debug/grid_lines_test.cpp
    rendering_engine/passes/projection_jitter_test.cpp
    rendering_engine/render_graph/frame_graph_test.cpp
    rendering_engine/render_stats_test.cpp
    rendering_engine/renderables/per_draw_ubo_test.cpp
    rendering_engine/gpu/vk_format_fallback_test.cpp
    rendering_engine/gpu/vk_staging_ring_test.cpp
    rendering_engine/util/font_test.cpp
    rendering_engine/util/image_test.cpp
)

add_executable(AlphaEngineTests
    ${ALPHAENGINE_TEST_SOURCES}
    ${ALPHAENGINE_TEST_SOURCES_UNDER_TEST}
)

# tests/ for the support/ helpers (fake_device); vendor/stb for the
# header-only stb backends util/image and util/font compile against;
# vendor/cgltf for the glTF parser behind gltf_importer; the Vulkan SDK
# headers (found by the root find_package(Vulkan)) for vk_negotiate, which
# uses the VkFormat constants and nothing from the loader. The repo root is
# already on the include path via the top-level include_directories().
target_include_directories(AlphaEngineTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/vendor/stb
    ${CMAKE_SOURCE_DIR}/vendor/cgltf
    ${Vulkan_INCLUDE_DIRS}
)

# glm backs core/math + transforms, SDL3 backs core/platform (and the shader
# cache's SDL_GetPrefPath), Threads backs core/jobs,
# nlohmann_json backs core/settings_parse. The asset layer links no gpu
# backend — the fake device stands in. shader_registry is the embedded
# shaders/ tree the shader-library tests read, and glslang is the front end
# the shader-compiler tests drive; both are static and need no GPU or window.
target_link_libraries(AlphaEngineTests PRIVATE
    GTest::gtest_main
    glm::glm
    SDL3::SDL3-static
    Threads::Threads
    shader_registry
    glslang::glslang
    glslang::SPIRV
    glslang::glslang-default-resource-limits
    nlohmann_json::nlohmann_json
    ktx_read
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
