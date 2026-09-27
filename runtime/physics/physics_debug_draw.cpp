// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

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
