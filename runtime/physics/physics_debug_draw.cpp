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

#include <runtime/physics/physics_debug_draw.hpp>

#include <runtime/physics/physics_world.hpp>

namespace runtime::physics
{
    debug_draw::debug_draw(const world& source) : line_helper("Physics"), m_world{source} {}

    void debug_draw::refresh()
    {
        const std::uint64_t revision = m_world.revision();
        if (m_built && revision == m_revision)
        {
            return;
        }
        m_revision = revision;
        m_built = true;
        m_positions.clear();
        m_colors.clear();
        m_world.debug_lines(m_positions, m_colors);
        set_segments(m_positions, m_colors);
    }
} // namespace runtime::physics
