// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file font_asset.hpp
 * @brief A font's glyph atlas and metrics owned through a reference-counted asset handle.
 */

#pragma once

#include <string>

#include <rendering_engine/assets/font.hpp>
#include <rendering_engine/gpu/handle.hpp>

namespace rendering_engine
{
    /**
     * @brief A @c font loaded from a TTF file at a fixed pixel size,
     *        with its glyph atlas uploaded to the GPU.
     *
     * Produced by @ref asset_cache::load_font and handed out as a
     * @c std::shared_ptr, keyed on @c (path, size) so two callers asking for
     * the same face at the same size share one atlas. The constructor packs
     * the face (see @ref font) and uploads the atlas once as an
     * @c rgba8_unorm texture — linear, so the coverage reaches the LDR
     * swapchain the UI composites onto with the bytes it was rasterized as —
     * sampled bilinearly without mipmaps (a mip level would blend packed
     * neighbours together) and clamped at the edges. The destructor releases
     * it, so the atlas lives exactly as long as the last handle; text
     * renderables hold one for as long as they draw.
     *
     * Throws @c std::runtime_error, as @ref font does, when the file
     * cannot be loaded. Non-copyable and non-movable: the GPU handle has a
     * single owner and is freed exactly once.
     */
    struct font_asset
    {
        font_asset(const std::string& filename, float size);
        ~font_asset();

        font_asset(const font_asset&) = delete;
        font_asset& operator=(const font_asset&) = delete;
        font_asset(font_asset&&) = delete;
        font_asset& operator=(font_asset&&) = delete;

        /** @brief The glyph metrics, kerning, vertical metrics and the CPU copy of the atlas. */
        rendering_engine::font font;

        /** @brief @ref font::atlas on the GPU; the uv rects in the glyph metrics address it. */
        gpu::texture atlas{};
    };
} // namespace rendering_engine
