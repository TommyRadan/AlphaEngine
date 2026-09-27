// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

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

#include <assets/color.hpp>
#include <assets/image.hpp>

namespace assets
{
    /**
     * @brief The storage layout of decoded texels: 8-bit RGBA, or one of the
     *        block-compressed families a KTX2 file can hold or be
     *        transcoded to.
     *
     * The colour space is not part of it: the renderer uploads the same
     * bytes in the sRGB or the unorm form of the family, as the caller's
     * @ref color_space asks. BC4 (one channel) and BC5 (two) are data
     * formats with no sRGB form.
     */
    enum class texel_format
    {
        rgba8,
        bc1_rgba,
        bc3_rgba,
        bc4_r,
        bc5_rg,
        bc7_rgba,
        astc_4x4,
    };

    /** @brief True for the block-compressed families: 4x4 texel blocks, every level supplied by the file. */
    constexpr bool is_block_compressed(texel_format format)
    {
        return format != texel_format::rgba8;
    }

    /**
     * @brief Bytes of a tightly packed @p width x @p height image of
     *        @p format: whole 4x4 blocks for a compressed family (8 bytes
     *        for BC1 / BC4, 16 for the rest; a level smaller than a block
     *        still occupies one), four bytes per texel for RGBA8.
     */
    constexpr std::size_t texel_image_bytes(texel_format format, uint32_t width, uint32_t height)
    {
        if (!is_block_compressed(format))
        {
            return static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
        }
        const std::size_t blocks_x = (static_cast<std::size_t>(width) + 3u) / 4u;
        const std::size_t blocks_y = (static_cast<std::size_t>(height) + 3u) / 4u;
        const std::size_t block_bytes = format == texel_format::bc1_rgba || format == texel_format::bc4_r ? 8u : 16u;
        return blocks_x * blocks_y * block_bytes;
    }

    /**
     * @brief The block-compressed families a device can sample, captured by
     *        the renderer on the main thread so a worker can choose a KTX2
     *        transcode target without touching the device.
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

        /** @brief Whether @p format can be sampled; always true for RGBA8. */
        bool supports(texel_format format) const;
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
        assets::image image;

        // The pre-built levels (base level first, each tightly packed texels
        // or blocks of @ref format); empty for an @ref image.
        texel_format format{texel_format::rgba8};
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
    decoded_texture
    decode_texture_file(const std::filesystem::path& path, color_space space, const compressed_format_support& support);

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
     * @p space is the colour space the result will be sampled in, whatever
     * transfer function the file declares, exactly as for a PNG. Every mip
     * level in the file is kept; a single-level RGBA8 result comes back as a
     * @ref decoded_texture::image so the device generates its chain, while a
     * single-level compressed one stays single-level. Throws
     * @c std::runtime_error (after logging) on failure.
     */
    decoded_texture decode_ktx2(const std::byte* bytes,
                                std::size_t size,
                                color_space space,
                                const compressed_format_support& support,
                                const std::string& label);
} // namespace assets
