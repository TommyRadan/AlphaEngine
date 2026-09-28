// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <vector>

#include <rendering_engine/renderables/draw_item.hpp>

namespace rendering_engine
{
    // Anything that contributes draws to the UI or the debug pass (the scene
    // is drawn from mesh proxies instead; see @ref mesh_proxy). @ref upload
    // allocates GPU resources (buffers, bind groups) once the device is
    // alive; @ref collect_draw_items appends one or more @ref draw_item
    // values describing what to draw this frame. The pass sorts the collected
    // items by material and dispatches them in one place — renderables
    // never call @c set_pipeline / @c set_bind_group / @c draw_indexed
    // themselves.
    //
    // The output vector is provided by the caller so the pass can reuse
    // a single allocation across frames; composite renderables (e.g. a
    // @c sprite_batch over several textures) push N items into the same
    // vector.
    struct renderable
    {
        virtual ~renderable() = default;
        virtual void upload() = 0;
        virtual void collect_draw_items(std::vector<draw_item>& out) = 0;
    };
} // namespace rendering_engine
