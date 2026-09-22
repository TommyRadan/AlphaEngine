// Unit tests for rendering_engine::util::font's error handling: a missing
// file, an empty file and a file that is not a font each fail with a
// std::runtime_error instead of resizing the read buffer to size_t(-1) or
// handing garbage to stb_truetype. The success path needs a real TTF face and
// exercises the rasterizer, so it stays out of the headless suite.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include <rendering_engine/util/font.hpp>

namespace
{
    // Writes `contents` to a uniquely named file in the temp directory and
    // removes it again when the test body ends.
    struct scoped_temp_file
    {
        std::filesystem::path path;

        explicit scoped_temp_file(const std::string& name, const std::string& contents)
            : path{std::filesystem::temp_directory_path() / name}
        {
            std::ofstream out{path, std::ios::binary | std::ios::trunc};
            out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        }

        ~scoped_temp_file()
        {
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
        }

        scoped_temp_file(const scoped_temp_file&) = delete;
        scoped_temp_file& operator=(const scoped_temp_file&) = delete;
    };
} // namespace

TEST(util_font, missing_file_throws)
{
    EXPECT_THROW((rendering_engine::util::font{"/nonexistent/alpha_engine_font_test_missing.ttf", 16.0f}),
                 std::runtime_error);
}

TEST(util_font, empty_file_throws)
{
    const scoped_temp_file file{"alpha_engine_font_test_empty.ttf", ""};
    EXPECT_THROW((rendering_engine::util::font{file.path.string(), 16.0f}), std::runtime_error);
}

TEST(util_font, non_font_file_throws)
{
    // No TrueType/OpenType/collection tag at offset 0, so stb_truetype rejects
    // it before parsing any table.
    const scoped_temp_file file{"alpha_engine_font_test_not_a_font.ttf", std::string(256, 'x')};
    EXPECT_THROW((rendering_engine::util::font{file.path.string(), 16.0f}), std::runtime_error);
}
