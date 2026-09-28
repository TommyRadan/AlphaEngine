// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/renderables/premade_2d/label.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

#include <core/log.hpp>
#include <rendering_engine/resources/font_asset.hpp>

namespace
{
    constexpr char32_t replacement_character = 0xFFFD;

    // Decodes the UTF-8 sequence starting at text[index] and advances
    // @p index past it. A malformed, truncated, overlong or surrogate
    // sequence decodes to U+FFFD (which no atlas holds, so it is skipped)
    // and consumes only its lead byte, so decoding resynchronises on the
    // next one.
    char32_t next_codepoint(const std::string& text, std::size_t& index)
    {
        const auto lead = static_cast<unsigned char>(text[index]);
        if (lead < 0x80)
        {
            ++index;
            return lead;
        }

        std::size_t length = 0;
        char32_t codepoint = 0;
        char32_t smallest = 0;
        if ((lead & 0xE0) == 0xC0)
        {
            length = 2;
            codepoint = lead & 0x1Fu;
            smallest = 0x80;
        }
        else if ((lead & 0xF0) == 0xE0)
        {
            length = 3;
            codepoint = lead & 0x0Fu;
            smallest = 0x800;
        }
        else if ((lead & 0xF8) == 0xF0)
        {
            length = 4;
            codepoint = lead & 0x07u;
            smallest = 0x10000;
        }
        else
        {
            ++index;
            return replacement_character;
        }

        if (text.size() - index < length)
        {
            ++index;
            return replacement_character;
        }
        for (std::size_t k = 1; k < length; ++k)
        {
            const auto continuation = static_cast<unsigned char>(text[index + k]);
            if ((continuation & 0xC0) != 0x80)
            {
                ++index;
                return replacement_character;
            }
            codepoint = (codepoint << 6) | (continuation & 0x3Fu);
        }
        if (codepoint < smallest || codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF))
        {
            ++index;
            return replacement_character;
        }
        index += length;
        return codepoint;
    }
} // namespace

rendering_engine::label::label(std::shared_ptr<font_asset> font, ui_material* mat, const std::string& text)
    : m_font{std::move(font)}, m_batch{mat}, m_text{text},
      m_rect{rect_transform::anchored(
          ui_anchor::top_left, ui_anchor::top_left, core::math::vec2{0.0f, 0.0f}, core::math::vec2{0.0f, 0.0f})}
{
    layout();
}

rendering_engine::label::~label() = default;

void rendering_engine::label::set_text(const std::string& text)
{
    if (text == m_text)
    {
        return;
    }
    m_text = text;
    layout();
}

const std::string& rendering_engine::label::get_text() const
{
    return m_text;
}

void rendering_engine::label::set_color(const assets::color& color)
{
    m_color = color;
    m_dirty = true;
    changed();
}

const assets::color& rendering_engine::label::get_color() const
{
    return m_color;
}

void rendering_engine::label::set_position(const core::math::vec2& position)
{
    m_rect.position = position;
    m_dirty = true;
    changed();
}

void rendering_engine::label::set_anchor(const core::math::vec2& anchor)
{
    m_rect.anchor_min = anchor;
    m_rect.anchor_max = anchor;
    m_dirty = true;
    changed();
}

void rendering_engine::label::set_pivot(const core::math::vec2& pivot)
{
    m_rect.pivot = pivot;
    m_dirty = true;
    changed();
}

void rendering_engine::label::set_rotation(float radians)
{
    m_rect.rotation = radians;
    m_dirty = true;
    changed();
}

const rendering_engine::rect_transform& rendering_engine::label::get_rect() const
{
    return m_rect;
}

core::math::vec2 rendering_engine::label::get_size() const
{
    return m_rect.size;
}

float rendering_engine::label::get_width() const
{
    return m_rect.size.x;
}

bool rendering_engine::label::contains(const ui_rect& drawable, const core::math::vec2& point) const
{
    return m_rect.contains(drawable, point);
}

void rendering_engine::label::layout()
{
    m_glyphs.clear();
    m_dirty = true;
    changed();
    if (m_font == nullptr)
    {
        m_rect.size = core::math::vec2{0.0f, 0.0f};
        return;
    }

    const assets::font& font = m_font->font;
    float widest = 0.0f;
    float pen = 0.0f;
    std::size_t line = 0;
    char32_t previous = 0;
    for (std::size_t index = 0; index < m_text.size();)
    {
        const char32_t codepoint = next_codepoint(m_text, index);
        if (codepoint == U'\n')
        {
            widest = std::max(widest, pen);
            pen = 0.0f;
            ++line;
            previous = 0;
            continue;
        }
        if (codepoint == U'\r')
        {
            continue;
        }

        const assets::glyph_metrics* glyph = font.glyph(codepoint);
        if (glyph == nullptr)
        {
            if (!m_warned_missing_glyph)
            {
                LOG_WRN("label: font has no glyph for U+%04X in \"%s\"; skipping it",
                        static_cast<unsigned>(codepoint),
                        m_text.c_str());
                m_warned_missing_glyph = true;
            }
            previous = 0;
            continue;
        }

        if (previous != 0)
        {
            pen += font.kerning(previous, codepoint);
        }
        if (glyph->size.x > 0.0f && glyph->size.y > 0.0f)
        {
            // The bearings are whole pixels, so snapping the pen and the
            // baseline keeps every glyph texel-aligned with the atlas.
            const float baseline = std::round(font.ascent() + static_cast<float>(line) * font.line_height());
            glyph_quad quad{};
            quad.min = core::math::vec2{std::round(pen) + glyph->bearing.x, baseline + glyph->bearing.y};
            quad.max = quad.min + glyph->size;
            quad.uv_min = glyph->uv_min;
            quad.uv_max = glyph->uv_max;
            m_glyphs.push_back(quad);
        }
        pen += glyph->advance;
        previous = codepoint;
    }
    widest = std::max(widest, pen);

    // Every line is one line height below the last; the block spans the
    // first line's ascent to the last line's descent.
    const float height =
        m_text.empty() ? 0.0f : static_cast<float>(line) * font.line_height() + font.ascent() - font.descent();
    m_rect.size = core::math::vec2{std::ceil(widest), std::ceil(height)};
}

rendering_engine::ui_element_data rendering_engine::label::capture()
{
    if (m_font == nullptr)
    {
        return m_batch.capture();
    }
    if (m_dirty)
    {
        m_batch.clear();
        // The pivot's place in the block, snapped so the glyphs keep
        // their whole-pixel offsets from the (shader-snapped) pivot.
        const core::math::vec2 origin{std::round(m_rect.pivot.x * m_rect.size.x),
                                      std::round(m_rect.pivot.y * m_rect.size.y)};
        for (const glyph_quad& glyph : m_glyphs)
        {
            sprite_quad quad{};
            quad.pivot_anchor = m_rect.anchor_min;
            quad.pivot_offset = m_rect.position;
            quad.corner_min_offset = glyph.min - origin;
            quad.corner_max_offset = glyph.max - origin;
            quad.rotation = m_rect.rotation;
            quad.uv_min = glyph.uv_min;
            quad.uv_max = glyph.uv_max;
            quad.color = m_color;
            m_batch.add(m_font->atlas, quad);
        }
        m_dirty = false;
    }
    return m_batch.capture();
}
