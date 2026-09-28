// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file render_texture.hpp
 * @brief A colour target the application owns, which a camera renders into
 *        and a material samples.
 */

#pragma once

#include <cstdint>
#include <memory>

#include <rendering_engine/gpu/handle.hpp>

namespace rendering_engine
{
    namespace gpu
    {
        struct device;
    }

    struct texture_asset;

    /**
     * @brief A fixed-size colour target a camera renders its view into
     *        (@ref camera::set_target), whose colour a material samples like
     *        any other texture — a mirror, a monitor, a minimap.
     *
     * The application owns it, and it keeps its size however the window is
     * resized. It holds the view's final image — tonemapped, anti-aliased
     * and gamma-encoded, like the swapchain's — as @c rgba8_unorm, so a
     * material reads those encoded values as they are; row 0 is the bottom
     * of the image, as in every off-screen target, so a surface shows it
     * upright where its v = 0 edge is its bottom edge. The renderer renders
     * the views on render textures before the views on the swapchain, so a
     * material samples the image of the same frame; a view on one texture
     * that samples another renders after it when its camera ranks higher
     * (see @ref render_world::collect_views), and a view never samples the
     * texture it renders into (keep the surface off its camera's layers).
     *
     * @ref texture hands the colour out as a @ref texture_asset, the form a
     * material's texture slot takes (e.g.
     * @c standard_material::set_emissive_map); the asset does not own the
     * texture, which goes with this object. The render texture must outlive
     * the cameras that target it and the materials that sample it (or be
     * taken off them first). Main-thread only; non-copyable.
     */
    class render_texture
    {
    public:
        /** @brief Allocates a @p width x @p height colour target on @p device. */
        render_texture(gpu::device& device, uint32_t width, uint32_t height);
        ~render_texture();

        render_texture(const render_texture&) = delete;
        render_texture& operator=(const render_texture&) = delete;

        /** @brief The texture's size in pixels, fixed at construction. */
        uint32_t width() const noexcept
        {
            return m_width;
        }

        uint32_t height() const noexcept
        {
            return m_height;
        }

        /** @brief The colour target the camera's view renders into. */
        gpu::render_target target() const noexcept
        {
            return m_target;
        }

        /** @brief The target's colour, for a material to sample. */
        const std::shared_ptr<texture_asset>& texture() const noexcept
        {
            return m_texture;
        }

    private:
        gpu::device* m_device{nullptr};
        uint32_t m_width{0};
        uint32_t m_height{0};
        gpu::render_target m_target{};
        std::shared_ptr<texture_asset> m_texture;
    };
} // namespace rendering_engine
