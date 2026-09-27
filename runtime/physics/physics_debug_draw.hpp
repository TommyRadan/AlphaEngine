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
 * @file physics_debug_draw.hpp
 * @brief Line gizmo that draws the physics world's colliders and contacts.
 */

#pragma once

#include <cstdint>
#include <vector>

#include <core/math/vec3.hpp>
#include <rendering_engine/editor/line_helper.hpp>

namespace runtime::physics
{
    struct world;

    /**
     * @brief The physics world's debug wireframe, as an overlay line helper
     *        named "Physics".
     *
     * Created by @ref world::init in debug builds only; like every helper it
     * appears in the debug overlay's Helpers panel, whose checkbox toggles
     * it. Before each draw it re-reads @ref world::debug_lines, but only when
     * the world's @ref world::revision moved — once per physics step while
     * anything is simulated.
     */
    struct debug_draw final : rendering_engine::editor::line_helper
    {
        explicit debug_draw(const world& source);

    protected:
        void refresh() override;

    private:
        const world& m_world;
        std::uint64_t m_revision{0};
        bool m_built{false};
        std::vector<core::math::vec3> m_positions;
        std::vector<core::math::vec3> m_colors;
    };
} // namespace runtime::physics
