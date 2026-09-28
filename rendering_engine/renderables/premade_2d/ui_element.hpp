// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file ui_element.hpp
 * @brief Base of the 2D UI widgets a scene node draws on top of the frame.
 */

#pragma once

#include <cstdint>

#include <rendering_engine/ui_proxy.hpp>

namespace rendering_engine
{
    /**
     * @brief Something the UI pass draws: quads grouped by texture, in the
     *        drawable's pixel space.
     *
     * An element holds what it draws on the CPU and answers @ref capture
     * with it. Whoever owns it (a @c runtime::ui_element_component) keeps a
     * UI proxy in a @ref render_world; the render extraction copies
     * @ref capture into that proxy whenever @ref revision moved, so a change
     * made to an element reaches the frame after it, and the renderer never
     * reads the element itself.
     */
    struct ui_element
    {
        virtual ~ui_element() = default;

        ui_element(const ui_element&) = delete;
        ui_element& operator=(const ui_element&) = delete;

        /**
         * @brief What the element draws: its quad groups in paint order and
         *        the ui material they draw with. May first bring the quads
         *        up to date with the element's settings.
         */
        virtual ui_element_data capture() = 0;

        /**
         * @brief Advances whenever @ref capture would answer differently, so
         *        a proxy is rewritten only after a change. Never 0.
         */
        uint64_t revision() const noexcept
        {
            return m_revision;
        }

    protected:
        ui_element() = default;

        // Advances @ref revision after a change to what the element draws.
        void changed() noexcept
        {
            ++m_revision;
        }

    private:
        uint64_t m_revision{1};
    };
} // namespace rendering_engine
