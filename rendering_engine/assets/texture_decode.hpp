/**
 * Copyright (c) 2015-2026 Tomislav Radanovic
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * @file texture_decode.hpp
 * @brief The CPU half of a texture load: a file decoded into what the device
 *        upload needs, through stb_image or, for KTX2, libktx.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <rendering_engine/assets/image.hpp>
#include <rendering_engine/gpu/types.hpp>

namespace rendering_engine
{
    namespace gpu
    {
        struct device;
    }

    /**
     * @brief The block-compressed families a device can sample, captured on
     *        the main thread so a worker can choose a KTX2 transcode target
     *        without touching the device (an OpenGL query needs the
     *        context's thread).
     *
     * A colour family counts only when both its unorm and its sRGB form
     * sample, so the loader never has to give up on the colour space.
     */
    struct compressed_format_support
    {
        bool bc1{false};
        bool bc3{false};
        bool bc4{false};
        bool bc5{false};
        bool bc7{false};
        bool astc_4x4{false};

        /** @brief Asks @p device (through @c format_support) which families it samples. */
        static compressed_format_support query(const gpu::device& device);

        /** @brief Whether @p format can be sampled; always true for an uncompressed format. */
        bool supports(gpu::texture_format format) const;
    };

    /**
     * @brief A texture file decoded on the CPU, ready for the device upload.
     *
     * Either an RGBA8 @ref image — a PNG / JPEG / ... through stb_image, or a
     * single-level KTX2 image that is (or was transcoded to) RGBA8 — uploaded
     * as level 0 with the rest of the chain generated on the device; or, for
     * any other KTX2 file, every mip level pre-built in @ref format (block-
     * compressed, or RGBA8 with the file's own chain), uploaded as it is.
     */
    struct decoded_texture
    {
        rendering_engine::image image;

        // The pre-built levels (base level first, each tightly packed texels
        // or blocks of @ref format); empty for an @ref image.
        gpu::texture_format format{gpu::texture_format::rgba8_unorm};
        uint32_t width{0};
        uint32_t height{0};
        std::vector<std::vector<std::byte>> levels;

        /** @brief Whether the texture comes as pre-built @ref levels rather than an @ref image. */
        bool has_levels() const noexcept
        {
            return !levels.empty();
        }
    };

    /** @brief Whether @p path names a KTX2 container, by its @c .ktx2 extension (in any case). */
    bool is_ktx2_path(const std::filesystem::path& path);

    /**
     * @brief Decodes the texture file @p path, read through the VFS.
     *
     * A @c .ktx2 file goes through @ref decode_ktx2; anything else through
     * @ref image (stb_image) as RGBA8. Safe to call from any thread:
     * asynchronous loads run it on the worker pool. Throws
     * @c std::runtime_error (after logging) when the file cannot be read or
     * decoded.
     */
    decoded_texture decode_texture_file(const std::filesystem::path& path,
                                        gpu::color_space space,
                                        const compressed_format_support& support);

    /**
     * @brief Decodes the KTX2 container in @p bytes (@p size bytes, named
     *        @p label in the log) through libktx.
     *
     * Only single-layer 2D textures are accepted (no arrays, cube maps or
     * volumes). zstd / zlib supercompression is inflated. Basis Universal
     * content (ETC1S / BasisLZ or UASTC) is transcoded to the best format in
     * @p support: BC4 / BC5 for one- / two-channel linear data (they have no
     * sRGB form), else BC7, then ASTC 4x4, then BC3 (with alpha) or BC1
     * (without), and RGBA8 when the device samples none of them. A file
     * already in a format the engine knows (BC1 / BC3 / BC4 / BC5 / BC7 /
     * ASTC 4x4, or RGBA8) is taken as it is; ASTC the device cannot sample
     * (or in another block size) is decoded to RGBA8, and any other format
     * is an error.
     * @p space picks the sRGB or unorm form of the result whatever transfer
     * function the file declares, exactly as it picks the RGBA8 format of a
     * PNG. Every mip level in the file is kept; a single-level RGBA8 result
     * comes back as a @ref decoded_texture::image so the device generates its
     * chain, while a single-level compressed one stays single-level. Throws
     * @c std::runtime_error (after logging) on failure.
     */
    decoded_texture decode_ktx2(const std::byte* bytes,
                                std::size_t size,
                                gpu::color_space space,
                                const compressed_format_support& support,
                                const std::string& label);
} // namespace rendering_engine
