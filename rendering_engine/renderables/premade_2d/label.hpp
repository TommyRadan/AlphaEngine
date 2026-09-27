// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <memory>
#include <string>
#include <vector>

#include <assets/color.hpp>
#include <core/math/vec2.hpp>
#include <rendering_engine/renderables/premade_2d/rect_transform.hpp>
#include <rendering_engine/renderables/premade_2d/sprite_batch.hpp>
#include <rendering_engine/renderables/renderable.hpp>

namespace rendering_engine
{
    struct font_asset;
    struct ui_material;

    /**
     * @brief A block of text drawn from a font's glyph atlas in one draw call.
     *
     * The text is UTF-8; @c '\n' starts a new line (a @c '\r' is ignored).
     * Each line sits on its own baseline, @c font.line_height() below the
     * last, the first one @c font.ascent() below the top of the block;
     * glyphs are placed on the baseline by their bearings and advanced
     * with the face's kerning. A codepoint the atlas does not hold is
     * skipped (logged once per label).
     *
     * Every glyph is a quad on the label's own @ref sprite_batch over the
     * one atlas texture, so the whole label is a single draw. The layout
     * is redone only when the text changes, and the quads only when the
     * text, colour or placement does; where the block lands is resolved
     * in the vertex shader from its anchor and the live drawable size.
     * The placement is a @ref rect_transform whose size is the laid-out
     * block (@ref get_size): the pivot is a fraction of the block, so a
     * centred pivot centres the text on its position. It starts at the
     * drawable's top-left corner, pivot top-left, in opaque white.
     *
     * Register it with @c renderer::register_ui_renderable and unregister
     * it before destroying it.
     */
    struct label : public renderable
    {
        // @p font supplies the atlas and metrics and is held for the
        // label's lifetime; @p mat is the ui material (not owned).
        label(std::shared_ptr<font_asset> font, ui_material* mat, const std::string& text = {});
        ~label() override;

        label(const label&) = delete;
        label& operator=(const label&) = delete;

        void set_text(const std::string& text);
        const std::string& get_text() const;

        /** @brief The text colour: the atlas coverage becomes its alpha. */
        void set_color(const assets::color& color);
        const assets::color& get_color() const;

        // Placement; see @ref rect_transform. @ref set_anchor pins both
        // anchors to @p anchor (text does not stretch).
        void set_position(const core::math::vec2& position);
        void set_anchor(const core::math::vec2& anchor);
        void set_pivot(const core::math::vec2& pivot);
        void set_rotation(float radians);
        const rect_transform& get_rect() const;

        /** @brief Pixel size of the laid-out block: the widest line by the lines' extent. */
        core::math::vec2 get_size() const;
        float get_width() const;

        /**
         * @brief Hit test of the text block against the live drawable:
         *        whether @p point, in UI pixels (a mouse position goes
         *        through @ref window_to_pixels first), lies on it.
         */
        bool contains(const core::math::vec2& point) const;

        /** @brief Nothing to upload up front: the quads are streamed at collect time. */
        void upload() final;
        void collect_draw_items(std::vector<draw_item>& out) final;

        bool casts_shadow() const override
        {
            return false;
        }

    private:
        // One glyph's quad and atlas rect, in pixels from the block's
        // top-left corner.
        struct glyph_quad
        {
            core::math::vec2 min;
            core::math::vec2 max;
            core::math::vec2 uv_min;
            core::math::vec2 uv_max;
        };

        // Lays m_text out into m_glyphs and m_rect.size.
        void layout();

        std::shared_ptr<font_asset> m_font;
        sprite_batch m_batch;
        std::string m_text;
        rect_transform m_rect;
        assets::color m_color{255, 255, 255, 255};
        std::vector<glyph_quad> m_glyphs;
        // Whether the batch's quads are out of date.
        bool m_dirty{true};
        // The first codepoint the atlas lacks is logged once per label
        // rather than on every layout.
        bool m_warned_missing_glyph{false};
    };
} // namespace rendering_engine
