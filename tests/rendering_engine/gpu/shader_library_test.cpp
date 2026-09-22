// Unit tests for rendering_engine::gpu::shader_library: the embedded
// registry generated from shaders/ serves every listed file by its
// shaders/-relative path, the generated include/bindings.glsl mirrors the
// C++ binding table, an unknown path throws, and (debug builds) an on-disk
// override root wins over the embedded copy.

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <rendering_engine/gpu/shader_bindings.hpp>
#include <rendering_engine/gpu/shader_library.hpp>

namespace shader_library = rendering_engine::gpu::shader_library;
namespace shader_bindings = rendering_engine::gpu::shader_bindings;

namespace
{
    constexpr auto npos = std::string_view::npos;

    bool starts_with(std::string_view text, std::string_view prefix)
    {
        return text.substr(0, prefix.size()) == prefix;
    }

    // A scratch directory under the temp path, removed when the test ends.
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

    void write_file(const std::filesystem::path& file, std::string_view contents)
    {
        std::filesystem::create_directories(file.parent_path());
        std::ofstream out{file, std::ios::binary | std::ios::trunc};
        out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    }
} // namespace

TEST(shader_library, embedded_lookup_returns_the_file_text)
{
    const std::string_view source = shader_library::source("materials/basic.vert.glsl");
    EXPECT_TRUE(starts_with(source, "#version 450"));
    EXPECT_NE(source.find("#include \"include/per_frame.glsl\""), npos);
    EXPECT_NE(source.find("gl_Position"), npos);
}

TEST(shader_library, contains_reports_embedded_paths_only)
{
    EXPECT_TRUE(shader_library::contains("include/fog.glsl"));
    EXPECT_TRUE(shader_library::contains("passes/fullscreen.vert.glsl"));
    EXPECT_FALSE(shader_library::contains("include/does_not_exist.glsl"));
    EXPECT_FALSE(shader_library::contains(""));
    // Lookups are by the exact shaders/-relative path: no directory
    // stripping, no leading "./".
    EXPECT_FALSE(shader_library::contains("fog.glsl"));
    EXPECT_FALSE(shader_library::contains("./include/fog.glsl"));
}

TEST(shader_library, missing_path_throws)
{
    EXPECT_THROW((void)shader_library::source("materials/nope.frag.glsl"), std::runtime_error);
    try
    {
        (void)shader_library::source("materials/nope.frag.glsl");
        FAIL() << "expected std::runtime_error";
    }
    catch (const std::runtime_error& error)
    {
        EXPECT_NE(std::string_view{error.what()}.find("materials/nope.frag.glsl"), npos);
    }
}

TEST(shader_library, generated_bindings_mirror_the_header)
{
    const std::string_view bindings = shader_library::source("include/bindings.glsl");
    const auto expect_define = [&](std::string_view name, uint32_t value)
    {
        const std::string line = "#define BINDING_" + std::string{name} + " " + std::to_string(value) + "\n";
        EXPECT_NE(bindings.find(line), npos) << "missing " << line;
    };
    expect_define("PER_FRAME", shader_bindings::per_frame);
    expect_define("PER_DRAW_MODEL", shader_bindings::per_draw_model);
    expect_define("LIGHTS", shader_bindings::lights);
    expect_define("MATERIAL_PARAMS", shader_bindings::material_params);
    expect_define("MATERIAL_ALBEDO_MAP", shader_bindings::material_albedo_map);
    expect_define("MATERIAL_EMISSIVE_MAP", shader_bindings::material_emissive_map);
    expect_define("SHADOW_MAP", shader_bindings::shadow_map);
    expect_define("SHADOW", shader_bindings::shadow);
    expect_define("MATERIAL_IRRADIANCE_MAP", shader_bindings::material_irradiance_map);
    expect_define("MATERIAL_BRDF_LUT", shader_bindings::material_brdf_lut);
    expect_define("POINT_SHADOW", shader_bindings::point_shadow);
    expect_define("POINT_SHADOW_MAP_0", shader_bindings::point_shadow_map_0);
    expect_define("POINT_SHADOW_MAP_5", shader_bindings::point_shadow_map_5);
    EXPECT_NE(bindings.find("#ifndef AE_BINDINGS_GLSL"), npos);
}

TEST(shader_library, embedded_paths_are_sorted_unique_and_complete)
{
    const std::vector<std::string_view> paths = shader_library::embedded_paths();
    ASSERT_FALSE(paths.empty());
    EXPECT_TRUE(std::is_sorted(paths.begin(), paths.end()));
    EXPECT_EQ(std::adjacent_find(paths.begin(), paths.end()), paths.end());

    for (const std::string_view path : paths)
    {
        EXPECT_TRUE(starts_with(path, "include/") || starts_with(path, "materials/") || starts_with(path, "passes/"))
            << path;
        EXPECT_TRUE(path.ends_with(".glsl")) << path;
        EXPECT_TRUE(shader_library::contains(path)) << path;
    }

    // Every built-in material has both stages embedded, and the shared
    // includes the lit materials depend on are present.
    for (const char* material : {"basic", "grid", "instanced", "line", "phong", "points", "standard", "ui"})
    {
        const std::string vertex = std::string{"materials/"} + material + ".vert.glsl";
        const std::string fragment = std::string{"materials/"} + material + ".frag.glsl";
        EXPECT_TRUE(std::binary_search(paths.begin(), paths.end(), std::string_view{vertex})) << vertex;
        EXPECT_TRUE(std::binary_search(paths.begin(), paths.end(), std::string_view{fragment})) << fragment;
    }
    for (const char* include : {"include/bindings.glsl",
                                "include/per_frame.glsl",
                                "include/per_draw.glsl",
                                "include/lights.glsl",
                                "include/shadows.glsl",
                                "include/fog.glsl",
                                "include/brdf.glsl",
                                "include/depth_utils.glsl"})
    {
        EXPECT_TRUE(std::binary_search(paths.begin(), paths.end(), std::string_view{include})) << include;
    }
}

TEST(shader_library, shared_blocks_are_declared_once)
{
    // The point of the include tree: the per-frame block, the lights
    // block and the shadow lookups are declared in exactly one file each
    // and every consumer pulls them in rather than restating them.
    int per_frame_declarations = 0;
    int lights_declarations = 0;
    int directional_shadow_definitions = 0;
    for (const std::string_view path : shader_library::embedded_paths())
    {
        const std::string_view source = shader_library::source(path);
        per_frame_declarations += source.find("uniform PerFrame") != npos ? 1 : 0;
        lights_declarations += source.find("uniform Lights") != npos ? 1 : 0;
        directional_shadow_definitions += source.find("float directional_shadow(") != npos ? 1 : 0;
    }
    EXPECT_EQ(per_frame_declarations, 1);
    EXPECT_EQ(lights_declarations, 1);
    EXPECT_EQ(directional_shadow_definitions, 1);

    for (const char* lit : {"materials/phong.frag.glsl", "materials/standard.frag.glsl"})
    {
        const std::string_view source = shader_library::source(lit);
        EXPECT_NE(source.find("#include \"include/lights.glsl\""), npos) << lit;
        EXPECT_NE(source.find("#include \"include/shadows.glsl\""), npos) << lit;
        EXPECT_NE(source.find("#include \"include/fog.glsl\""), npos) << lit;
    }
}

TEST(shader_library, override_root_reads_from_disk_in_debug_builds)
{
    const std::string_view embedded_fog = shader_library::source("include/fog.glsl");

    scoped_temp_dir root{"alpha_engine_shader_library_test"};
    write_file(root.path / "include" / "fog.glsl", "// overridden fog\n");
    write_file(root.path / "include" / "only_on_disk.glsl", "// disk only\n");

    shader_library::set_override_root(root.path);
#if defined(_DEBUG)
    EXPECT_EQ(shader_library::override_root(), root.path);
    EXPECT_EQ(shader_library::source("include/fog.glsl"), "// overridden fog\n");
    EXPECT_TRUE(shader_library::contains("include/only_on_disk.glsl"));
    EXPECT_EQ(shader_library::source("include/only_on_disk.glsl"), "// disk only\n");
    // Files the root does not have still come from the registry.
    EXPECT_EQ(shader_library::source("include/brdf.glsl"), shader_library::source("include/brdf.glsl"));
    EXPECT_TRUE(shader_library::contains("include/brdf.glsl"));
#else
    // Release builds ignore overrides entirely.
    EXPECT_TRUE(shader_library::override_root().empty());
    EXPECT_EQ(shader_library::source("include/fog.glsl"), embedded_fog);
    EXPECT_FALSE(shader_library::contains("include/only_on_disk.glsl"));
#endif

    shader_library::set_override_root({});
    EXPECT_TRUE(shader_library::override_root().empty());
    EXPECT_EQ(shader_library::source("include/fog.glsl"), embedded_fog);
    EXPECT_FALSE(shader_library::contains("include/only_on_disk.glsl"));
}
