// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <memory>

#include <rendering_engine/debug_draw/helper.hpp>
#include <rendering_engine/render_proxies.hpp>

namespace rendering_engine
{
    struct grid_material;
    struct mesh_asset;
    struct render_world;
} // namespace rendering_engine

namespace rendering_engine::debug_draw
{
    // The CAD-style infinite ground grid — an unbounded grid.
    // Unlike the line-based @ref grid_helper it is a mesh proxy the scene
    // pass draws: a single fullscreen triangle fronts the analytic
    // @ref grid_material, whose fragment shader reconstructs the ground
    // plane (z = 0, the engine is Z-up), draws minor / major lines plus the
    // coloured world axes, fades with distance and depth-tests against the
    // scene, so it stretches to the horizon and is occluded by scene
    // geometry. The proxy sits at the world origin, on @ref layer_editor,
    // with no bounds (it is never culled) and no shadow.
    //
    // Created as a built-in gizmo in debug builds and toggled from the
    // debug UI like the other helpers.
    struct infinite_grid : public helper
    {
        // @p fade_distance is the world-space radius past which the grid
        // has fully faded. It is baked into the grid template's shader, so
        // the grid builds its own material on a template for that distance
        // through @p owner's @ref renderer::create_grid_material (the
        // shaders are served from the SPIR-V cache after the first
        // compile), its vertex buffer on @p owner's device and its proxy in
        // @p owner's world.
        explicit infinite_grid(renderer& owner, float fade_distance = 100.0f);
        ~infinite_grid() override;

        // Shows or hides the grid's proxy.
        void set_visible(bool visible) override;

    private:
        // The material (and the grid template it keeps alive) at this
        // grid's fade distance, and the fullscreen triangle it draws;
        // released with the grid, before the device.
        std::unique_ptr<grid_material> m_material;
        std::shared_ptr<mesh_asset> m_mesh;

        // The grid's proxy in its renderer's world.
        render_world* m_world{nullptr};
        mesh_proxy_handle m_proxy{};
    };
} // namespace rendering_engine::debug_draw
