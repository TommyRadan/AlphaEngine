// Unit tests for rendering_engine::gpu::compile_glsl_to_spirv through the
// real glslang: #include resolution against the shader library, #define
// injection, the diagnostics a failure throws, the on-disk SPIR-V cache,
// and - the one that matters most - that every embedded material, pass
// and compute shader compiles headless, so an include or binding mistake
// is caught before a GPU ever sees it.

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <rendering_engine/gpu/shader_compiler.hpp>
#include <rendering_engine/gpu/shader_library.hpp>

namespace gpu = rendering_engine::gpu;

namespace
{
    constexpr auto npos = std::string_view::npos;
    constexpr uint32_t spirv_magic = 0x07230203u;

    // Every test runs with the disk cache off unless it is testing the
    // cache, so a stale blob from another run can never mask a compile.
    struct shader_compiler : ::testing::Test
    {
        void SetUp() override
        {
            gpu::set_shader_cache_directory({});
        }

        void TearDown() override
        {
            gpu::set_shader_cache_directory({});
        }
    };

    struct scoped_temp_dir
    {
        std::filesystem::path path;

        explicit scoped_temp_dir(const std::string& name) : path{std::filesystem::temp_directory_path() / name}
        {
            std::filesystem::remove_all(path);
            std::filesystem::create_directories(path);
        }

        ~scoped_temp_dir()
        {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }

        scoped_temp_dir(const scoped_temp_dir&) = delete;
        scoped_temp_dir& operator=(const scoped_temp_dir&) = delete;
    };

    std::size_t count_files(const std::filesystem::path& directory, std::string_view extension)
    {
        std::size_t count = 0;
        for (const auto& entry : std::filesystem::directory_iterator{directory})
        {
            if (entry.is_regular_file() && entry.path().extension() == extension)
            {
                ++count;
            }
        }
        return count;
    }

    // Stage of an embedded shader from its suffix; the library only
    // carries the three stages the engine uses.
    bool stage_for(std::string_view path, gpu::shader_stage& stage)
    {
        if (path.ends_with(".vert.glsl"))
        {
            stage = gpu::shader_stage::vertex;
            return true;
        }
        if (path.ends_with(".frag.glsl"))
        {
            stage = gpu::shader_stage::fragment;
            return true;
        }
        if (path.ends_with(".comp.glsl"))
        {
            stage = gpu::shader_stage::compute;
            return true;
        }
        return false;
    }

    std::string message_of(const std::function<void()>& call)
    {
        try
        {
            call();
        }
        catch (const std::runtime_error& error)
        {
            return error.what();
        }
        return {};
    }
} // namespace

TEST_F(shader_compiler, compiles_a_shader_that_includes_from_the_library)
{
    constexpr std::string_view source = R"(#version 450
#include "include/per_frame.glsl"
#include "include/per_draw.glsl"

layout(location = 0) in vec3 position;

void main()
{
    gl_Position = u_frame.projectionMatrix * u_frame.viewMatrix * u_draw.modelMatrix * vec4(position, 1.0);
}
)";
    const std::vector<uint32_t> spirv = gpu::compile_glsl_to_spirv(source, gpu::shader_stage::vertex);
    ASSERT_GT(spirv.size(), 5u);
    EXPECT_EQ(spirv.front(), spirv_magic);
}

TEST_F(shader_compiler, nested_includes_resolve_once_each)
{
    // fog.glsl includes per_frame.glsl, which includes bindings.glsl; the
    // shader includes per_frame.glsl itself as well, so the guards must
    // hold across the nesting.
    constexpr std::string_view source = R"(#version 450
#include "include/per_frame.glsl"
#include "include/fog.glsl"
#include "include/per_frame.glsl"

layout(location = 0) out vec4 fragColor;

void main()
{
    fragColor = vec4(apply_fog(vec3(1.0), vec3(0.0), vec3(0.0, 0.0, 1.0)), 1.0);
}
)";
    EXPECT_NO_THROW((void)gpu::compile_glsl_to_spirv(source, gpu::shader_stage::fragment));
}

TEST_F(shader_compiler, defines_are_injected_ahead_of_the_source)
{
    constexpr std::string_view source = R"(#version 450
#ifndef HAS_TINT
#error HAS_TINT was not defined
#endif

layout(location = 0) out vec4 fragColor;

void main()
{
    fragColor = vec4(TINT_VALUE);
}
)";
    gpu::shader_compile_options options{};
    options.defines = {{"HAS_TINT", ""}, {"TINT_VALUE", "0.25"}};
    options.name = "tests/tint.frag.glsl";
    EXPECT_NO_THROW((void)gpu::compile_glsl_to_spirv(source, gpu::shader_stage::fragment, options));

    // Without the defines the #error fires and the message names both the
    // shader and the directive text.
    const std::string message =
        message_of([&] { (void)gpu::compile_glsl_to_spirv(source, gpu::shader_stage::fragment); });
    ASSERT_FALSE(message.empty());
    EXPECT_NE(message.find("HAS_TINT was not defined"), npos) << message;
    EXPECT_NE(message.find("<inline>"), npos) << message;
}

TEST_F(shader_compiler, parse_failure_reports_name_stage_and_log)
{
    constexpr std::string_view source = R"(#version 450
layout(location = 0) out vec4 fragColor;
void main()
{
    fragColor = this is not glsl;
}
)";
    gpu::shader_compile_options options{};
    options.name = "tests/broken.frag.glsl";
    EXPECT_THROW((void)gpu::compile_glsl_to_spirv(source, gpu::shader_stage::fragment, options), std::runtime_error);

    const std::string message =
        message_of([&] { (void)gpu::compile_glsl_to_spirv(source, gpu::shader_stage::fragment, options); });
    EXPECT_NE(message.find("tests/broken.frag.glsl"), npos) << message;
    EXPECT_NE(message.find("fragment"), npos) << message;
    EXPECT_NE(message.find("ERROR"), npos) << message;
}

TEST_F(shader_compiler, unknown_include_is_reported_by_name)
{
    constexpr std::string_view source = R"(#version 450
#include "include/does_not_exist.glsl"
void main() {}
)";
    const std::string message =
        message_of([&] { (void)gpu::compile_glsl_to_spirv(source, gpu::shader_stage::vertex); });
    ASSERT_FALSE(message.empty());
    EXPECT_NE(message.find("include/does_not_exist.glsl"), npos) << message;
}

TEST_F(shader_compiler, library_shader_takes_its_path_as_name)
{
    EXPECT_THROW((void)gpu::compile_library_shader("materials/missing.vert.glsl", gpu::shader_stage::vertex),
                 std::runtime_error);

    const std::vector<uint32_t> spirv = gpu::compile_library_shader("passes/fullscreen.vert.glsl", gpu::shader_stage::vertex);
    EXPECT_EQ(spirv.front(), spirv_magic);
}

TEST_F(shader_compiler, grid_fade_distance_is_a_define_variant)
{
    gpu::shader_variant variant{"materials/grid.frag.glsl"};
    variant.defines.emplace_back("GRID_FADE_DISTANCE", "42.500000");
    EXPECT_NO_THROW((void)gpu::compile_library_shader(variant, gpu::shader_stage::fragment));
    // The file's own default keeps it compiling on its own too.
    EXPECT_NO_THROW((void)gpu::compile_library_shader("materials/grid.frag.glsl", gpu::shader_stage::fragment));
}

TEST_F(shader_compiler, every_embedded_shader_compiles)
{
    std::size_t compiled = 0;
    for (const std::string_view path : gpu::shader_library::embedded_paths())
    {
        if (path.substr(0, 8) == "include/")
        {
            continue;
        }
        SCOPED_TRACE(std::string{path});
        gpu::shader_stage stage{};
        ASSERT_TRUE(stage_for(path, stage)) << "unknown stage suffix";
        std::vector<uint32_t> spirv;
        ASSERT_NO_THROW(spirv = gpu::compile_library_shader(path, stage));
        ASSERT_GT(spirv.size(), 5u);
        EXPECT_EQ(spirv.front(), spirv_magic);
        ++compiled;
    }
    // Eight materials x two stages plus the passes and the IBL kernels.
    EXPECT_GE(compiled, 16u + 16u);
}

TEST_F(shader_compiler, disk_cache_serves_the_second_compile)
{
    scoped_temp_dir cache{"alpha_engine_shader_cache_test"};
    gpu::set_shader_cache_directory(cache.path);
    ASSERT_EQ(gpu::shader_cache_directory(), cache.path);

    // Unique text so no other test's blob can satisfy the lookup.
    constexpr std::string_view source = R"(#version 450
#include "include/constants.glsl"
layout(location = 0) out vec4 fragColor;
void main()
{
    fragColor = vec4(PI * 0.31415, 0.0, 0.0, 1.0); // cache-test
}
)";
    const gpu::shader_cache_stats before = gpu::shader_cache_statistics();

    const std::vector<uint32_t> first = gpu::compile_glsl_to_spirv(source, gpu::shader_stage::fragment);
    const gpu::shader_cache_stats after_first = gpu::shader_cache_statistics();
    EXPECT_EQ(after_first.misses, before.misses + 1);
    EXPECT_EQ(after_first.hits, before.hits);
    EXPECT_EQ(count_files(cache.path, ".spv"), 1u);
    EXPECT_EQ(count_files(cache.path, ".tmp"), 0u);

    const std::vector<uint32_t> second = gpu::compile_glsl_to_spirv(source, gpu::shader_stage::fragment);
    const gpu::shader_cache_stats after_second = gpu::shader_cache_statistics();
    EXPECT_EQ(after_second.hits, after_first.hits + 1);
    EXPECT_EQ(after_second.misses, after_first.misses);
    EXPECT_EQ(first, second);

    // A different define set, stage or included text is a different key.
    gpu::shader_compile_options options{};
    options.defines = {{"VARIANT", "1"}};
    (void)gpu::compile_glsl_to_spirv(source, gpu::shader_stage::fragment, options);
    EXPECT_EQ(gpu::shader_cache_statistics().misses, after_second.misses + 1);
    EXPECT_EQ(count_files(cache.path, ".spv"), 2u);

    // Disabling the cache compiles again and touches no files.
    gpu::set_shader_cache_directory({});
    EXPECT_TRUE(gpu::shader_cache_directory().empty());
    (void)gpu::compile_glsl_to_spirv(source, gpu::shader_stage::fragment);
    EXPECT_EQ(gpu::shader_cache_statistics().misses, after_second.misses + 2);
    EXPECT_EQ(count_files(cache.path, ".spv"), 2u);
}

TEST_F(shader_compiler, corrupt_cache_entry_is_recompiled)
{
    scoped_temp_dir cache{"alpha_engine_shader_cache_corrupt_test"};
    gpu::set_shader_cache_directory(cache.path);

    constexpr std::string_view source = R"(#version 450
layout(location = 0) out vec4 fragColor;
void main()
{
    fragColor = vec4(0.5); // corrupt-cache-test
}
)";
    const std::vector<uint32_t> first = gpu::compile_glsl_to_spirv(source, gpu::shader_stage::fragment);
    ASSERT_EQ(count_files(cache.path, ".spv"), 1u);

    // Truncate the blob to garbage; the next compile must not hand it out.
    for (const auto& entry : std::filesystem::directory_iterator{cache.path})
    {
        std::filesystem::resize_file(entry.path(), 6);
    }
    const gpu::shader_cache_stats before = gpu::shader_cache_statistics();
    const std::vector<uint32_t> second = gpu::compile_glsl_to_spirv(source, gpu::shader_stage::fragment);
    EXPECT_EQ(gpu::shader_cache_statistics().misses, before.misses + 1);
    EXPECT_EQ(first, second);
    EXPECT_EQ(std::filesystem::file_size(*std::filesystem::directory_iterator{cache.path}),
              second.size() * sizeof(uint32_t));
}
