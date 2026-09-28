// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file ui_proxy.hpp
 * @brief The renderer's copy of one UI element: its quads, grouped by the
 *        texture they sample, and the material they draw with.
 */

#pragma once

#include <cstdint>
#include <vector>

#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/materials/ui_material.hpp>

namespace rendering_engine
{
    /**
     * @brief One draw of a UI element: the quads that sample one texture,
     *        four @ref ui_vertex corners each (top-left, top-right,
     *        bottom-right, bottom-left), split along the 0-2 diagonal.
     */
    struct ui_quad_group
    {
        // The texture the quads sample; a resource, shared by reference.
        gpu::texture texture{};
        std::vector<ui_vertex> vertices;
    };

    /**
     * @brief What a UI element draws: its quad groups, drawn in order, and
     *        the ui material they draw with.
     */
    struct ui_element_data
    {
        // Not owned; its owner (the renderer, for its built-in ui
        // material) keeps it alive while a proxy names it.
        ui_material* mat{nullptr};
        std::vector<ui_quad_group> groups;
    };

    /**
     * @brief Everything the UI pass reads about one element, owned by a
     *        @ref render_world: a copy of the element's draw data and a
     *        stamp that advances with every write, so the renderer uploads
     *        the quads again only after a change.
     */
    struct ui_proxy
    {
        ui_element_data data;
        uint64_t revision{0};
    };
} // namespace rendering_engine
