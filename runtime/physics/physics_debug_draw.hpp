// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file physics_debug_draw.hpp
 * @brief Line gizmo that draws the physics world's colliders and contacts.
 */

#pragma once

#include <cstdint>
#include <vector>

#include <core/math/vec3.hpp>
#include <rendering_engine/debug_draw/line_helper.hpp>

namespace runtime::physics
{
    struct world;

    /**
     * @brief The physics world's debug wireframe, as an overlay line helper
     *        named "Physics".
     *
     * Created by the engine in debug builds only, once the world is up, on
     * the renderer whose debug pass draws it, and destroyed before the world
     * and that renderer go; like every helper it appears in the
     * debug overlay's Helpers panel, whose checkbox toggles it. Before each
     * draw it re-reads @ref world::debug_lines, but only when the world's
     * @ref world::revision moved — once per physics step while anything is
     * simulated.
     */
    struct debug_draw final : rendering_engine::debug_draw::line_helper
    {
        debug_draw(rendering_engine::renderer& renderer, const world& source);

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
