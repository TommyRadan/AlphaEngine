// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file mesh_proxy.hpp
 * @brief The renderer's copy of one drawn mesh: what it draws, where, and the
 *        instance and joint data captured for it.
 */

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <core/math/aabb.hpp>
#include <core/math/mat4.hpp>
#include <core/math/vec4.hpp>
#include <rendering_engine/render_proxies.hpp>
#include <rendering_engine/renderables/per_draw_ubo.hpp>

namespace rendering_engine
{
    struct material;
    struct mesh_asset;

    /**
     * @brief One record of an instanced draw's per-instance vertex stream:
     *        the instance's world transform (four vec4 columns) and its tint.
     *
     * The layout the instanced material's slot-1 vertex attributes read,
     * 80 bytes with no trailing padding.
     */
    struct mesh_instance
    {
        core::math::mat4 model{};
        core::math::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    };

    static_assert(sizeof(mesh_instance) == 80, "an instance record is a mat4 and a vec4");

    /**
     * @brief The arguments of one indexed-indirect draw, in the five-uint32
     *        layout of Vulkan's @c VkDrawIndexedIndirectCommand that
     *        @c gpu::render_pass_encoder::draw_indexed_indirect reads.
     */
    struct mesh_indirect_args
    {
        uint32_t index_count{0};
        uint32_t instance_count{0};
        uint32_t first_index{0};
        uint32_t base_vertex{0};
        uint32_t base_instance{0};

        friend bool operator==(const mesh_indirect_args&, const mesh_indirect_args&) = default;
    };

    static_assert(sizeof(mesh_indirect_args) == 20, "an indirect record is five uint32s");

    /**
     * @brief What a mesh proxy draws and how it takes part in the frame —
     *        everything but its placement.
     *
     * The geometry and the material are resources, shared by reference; the
     * rest is plain data. Whoever owns the proxy writes it with
     * @ref render_world::set_mesh_source when any of it changes.
     */
    struct mesh_description
    {
        // The uploaded geometry the draw binds, or null to draw nothing.
        std::shared_ptr<const mesh_asset> mesh;

        // The material the draw uses, not owned; its owner keeps it alive
        // for as long as a proxy names it.
        material* mat{nullptr};

        // The geometry's box — in object space when @ref placed, in world
        // space otherwise — or none, for geometry that is never culled.
        std::optional<core::math::aabb> bounds;

        // For a draw without indices: how many vertices of @ref mesh it
        // draws, when not all of them.
        std::optional<uint32_t> vertex_count;

        // Layer bits the proxy belongs to (see @ref layer_default).
        uint32_t layer_mask{layer_default};

        // Whether the shadow passes draw it as an occluder.
        bool casts_shadow{true};

        // Whether the geometry is placed by the proxy's world matrix. An
        // instanced draw carries world transforms per instance instead, and
        // ignores the proxy's placement.
        bool placed{true};

        // Whether the proxy draws its instance snapshot
        // (@ref mesh_proxy::instances) as one indexed-indirect draw.
        bool instanced{false};

        // Whether the geometry's vertex format is checked against the
        // material's before anything is drawn (see @ref validate_vertex_format).
        bool check_format{true};

        // Labels the proxy in log lines.
        const char* name{"mesh"};
    };

    /**
     * @brief An instanced proxy's per-instance data, captured from its source
     *        by the render extraction: every record slot, the indirect
     *        arguments of the draw (which carry how many of them are drawn),
     *        and which records changed since the renderer last uploaded them.
     */
    struct mesh_instances
    {
        // One record per instance slot (the source's capacity).
        std::vector<mesh_instance> records;

        // The draw's arguments; its instance count is how many records,
        // from the front of @ref records, are drawn.
        mesh_indirect_args args{};

        // Records written since the renderer last uploaded them:
        // [dirty_begin, dirty_end), empty when equal.
        uint32_t dirty_begin{0};
        uint32_t dirty_end{0};
    };

    /**
     * @brief Everything the renderer reads about one drawn mesh.
     *
     * Owned by a @ref render_world and written only through its API: the
     * description (@ref render_world::set_mesh_source), the placement
     * (@ref render_world::set_mesh_world, which keeps @ref per_draw,
     * @ref mirrored and @ref world_bounds consistent with @ref world), the
     * visibility, the instance snapshot and the joint palette. The passes
     * build their draws from it and nothing else.
     */
    struct mesh_proxy
    {
        mesh_description source;

        // The world matrix the geometry is placed by, and the PerDraw block
        // (model and normal matrix) built from it.
        core::math::mat4 world{};
        per_draw_payload per_draw{};

        // Whether @ref world reverses triangle winding (a negative
        // determinant), so the draw uses the material's clockwise-front-face
        // variant.
        bool mirrored{false};

        // @ref mesh_description::bounds in world space; meaningful only when
        // the description has bounds.
        core::math::aabb world_bounds{};

        // Whether the proxy is drawn at all; a hidden proxy keeps its place
        // in the draw order.
        bool visible{true};

        // Whether the geometry's vertex format suits the material (always
        // true when the description skips the check), and whether a mismatch
        // has been reported for the current geometry and material.
        bool format_ok{true};
        bool format_reported{false};

        // The instanced draw's per-instance data; empty otherwise.
        mesh_instances instances;

        // The joint palette a skinning material draws the geometry with, one
        // matrix per joint, and a stamp that advances with every write.
        std::vector<core::math::mat4> joints;
        uint64_t joints_revision{0};
    };
} // namespace rendering_engine
