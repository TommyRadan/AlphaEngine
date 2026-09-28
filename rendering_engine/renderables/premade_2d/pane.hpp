// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <vector>

#include <assets/color.hpp>
#include <assets/image.hpp>
#include <core/math/vec2.hpp>
#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/gpu/types.hpp>
#include <rendering_engine/renderables/premade_2d/rect_transform.hpp>
#include <rendering_engine/renderables/premade_2d/sprite_batch.hpp>
#include <rendering_engine/renderables/premade_2d/ui_element.hpp>

namespace rendering_engine
{
    namespace gpu
    {
        struct device;
    }

    struct ui_material;

    /**
     * @brief A rectangle of UI: a flat colour, an image, or an image tinted
     *        by a colour, placed by a @ref rect_transform in the drawable.
     *
     * A single quad on its own @ref sprite_batch, so one draw. The quad is
     * rebuilt on the CPU when a setter changes it and its pixel position is
     * resolved in the vertex shader, so moving, recolouring and resizing
     * the window all just work. It starts pinned to the drawable's
     * top-left corner, pivot top-left, @p size pixels large, drawing
     * opaque white. A @c runtime::ui_element_component draws it.
     */
    struct pane : public ui_element
    {
        // @p mat is the ui material (not owned); a texture @ref set_image
        // uploads lives on @p device.
        pane(gpu::device& device, ui_material* mat, const core::math::vec2& size);
        ~pane() override;

        pane(const pane&) = delete;
        pane& operator=(const pane&) = delete;

        /** @brief The colour the texture is multiplied by (the whole fill when there is none). */
        void set_color(const assets::color& color);
        const assets::color& get_color() const;

        // Upload @p image as the pane's own texture, replacing any
        // earlier one. @p space selects the RGBA8 format
        // (@ref rgba8_format). The default is @c linear — i.e. no
        // decode — because the UI pass composites straight onto the LDR
        // swapchain with no encode step, so an image must reach the
        // framebuffer with the bytes it was authored with.
        void set_image(const assets::image& image, assets::color_space space = assets::color_space::linear);

        // Draw @p uv_min .. @p uv_max of a texture the caller owns (a
        // texture asset, a font atlas) instead; it must outlive its use
        // here. An invalid handle goes back to the flat colour. Releases
        // a texture an earlier @ref set_image uploaded.
        void set_texture(gpu::texture texture,
                         const core::math::vec2& uv_min = core::math::vec2{0.0f, 0.0f},
                         const core::math::vec2& uv_max = core::math::vec2{1.0f, 1.0f});

        /** @brief The pane's placement; see @ref rect_transform. */
        const rect_transform& get_rect() const;
        void set_rect(const rect_transform& rect);

        // Field-wise setters over @ref set_rect. @ref set_anchor pins
        // both anchors to @p anchor; use @ref set_rect with
        // @ref rect_transform::stretched for a pane that stretches.
        void set_position(const core::math::vec2& position);
        void set_size(const core::math::vec2& size);
        void set_anchor(const core::math::vec2& anchor);
        void set_pivot(const core::math::vec2& pivot);
        void set_rotation(float radians);

        /**
         * @brief Hit test against @p drawable, the rect the pane is placed
         *        in (see @ref drawable_rect): whether @p point, in UI
         *        pixels (a mouse position goes through @ref window_to_pixels
         *        first), lies on the pane.
         */
        bool contains(const ui_rect& drawable, const core::math::vec2& point) const;

        /** @brief The pane's quad, rebuilt first if a setter changed it. */
        ui_element_data capture() override;

    private:
        void release_owned_texture();

        // The device the texture @ref set_image uploads is created on and
        // released through; it outlives the pane.
        gpu::device* m_device{nullptr};
        sprite_batch m_batch;
        rect_transform m_rect;
        assets::color m_color{255, 255, 255, 255};
        gpu::texture m_texture{};
        core::math::vec2 m_uv_min{0.0f, 0.0f};
        core::math::vec2 m_uv_max{1.0f, 1.0f};
        // Whether m_texture came from set_image and is freed here.
        bool m_owns_texture{false};
        // Whether the batch's quad is out of date.
        bool m_dirty{true};
    };
} // namespace rendering_engine
