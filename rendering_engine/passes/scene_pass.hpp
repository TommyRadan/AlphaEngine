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

#pragma once

#include <vector>

#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/passes/pass.hpp>
#include <rendering_engine/renderables/draw_item.hpp>

namespace rendering_engine
{
    struct renderable;
    struct shadow_pass;

    /**
     * @brief 3D scene pass. Clears the swapchain colour and depth,
     *        collects draw items from the scene-renderable registry,
     *        sorts them by pipeline, and dispatches them; broadcasts
     *        @ref event_engine::render_scene as the documented escape
     *        hatch for debug / gizmo callers.
     *
     * Owns the per-frame bind-group layout (camera @c viewMatrix /
     * @c projectionMatrix at binding 0 and the packed lights block at
     * binding 2, both in slot 0; plus the directional shadow matrix at
     * binding 4 and the shadow map at binding 9). The matching lit
     * materials read the layout via @ref frame_bind_group_layout so the
     * pipeline and the runtime bind group agree on slot shape.
     *
     * Skipped when no camera is attached (matches the previous
     * @c if (camera != nullptr) gate).
     */
    struct scene_pass : pass
    {
        // @p shadow is the shadow pass that runs ahead of this one; the
        // scene pass bakes its depth map into the per-frame bind group
        // and uploads its light-space matrix each frame so the lit
        // materials can sample it. May be null to disable shadowing.
        scene_pass(std::vector<renderable*>* registry, shadow_pass* shadow);
        ~scene_pass() override;

        scene_pass(const scene_pass&) = delete;
        scene_pass& operator=(const scene_pass&) = delete;

        void record(gpu::command_encoder& encoder, const frame_context& ctx) override;

        // Layout for the per-frame bind group bound at slot 0 each
        // frame. The matching material's pipeline_descriptor must
        // reserve slot 0 for this layout.
        gpu::bind_group_layout frame_bind_group_layout() const;

        // The per-frame bind group itself (camera / lights / shadow at
        // slot 0). The handle is stable across frames — the pass refills
        // the backing UBOs in record() rather than recreating the group —
        // so the debug pass can capture it once and bind it to project
        // its line-based gizmos with the same camera the scene used.
        gpu::bind_group frame_bind_group() const;

    private:
        // Non-owning back-pointer to the engine context's
        // scene-renderable registry. The context outlives every
        // pass so the pointer stays valid for the pass's lifetime.
        std::vector<renderable*>* m_registry;

        // Per-frame state — owned by the pass; created once and
        // refilled every record(). Released in the destructor before
        // the device tears its pools down. The camera UBO carries the
        // {viewMatrix, projectionMatrix} pair packed std140 (two
        // mat4s = 128 bytes) at binding 0; the lights UBO carries the
        // packed @ref gpu_lights block at binding 2. Both live in the
        // single per-frame bind group bound at slot 0.
        gpu::bind_group_layout m_frame_layout{};
        gpu::buffer m_frame_ubo{};
        gpu::buffer m_lights_ubo{};
        gpu::buffer m_shadow_ubo{};
        gpu::bind_group m_frame_bind_group{};

        // Shadow pass feeding the per-frame group. Non-owning — the
        // engine context owns both passes and orders the shadow pass
        // before this one. Null disables shadowing.
        shadow_pass* m_shadow{nullptr};

        // Reused across frames so the underlying allocation persists.
        std::vector<draw_item> m_items;
    };
} // namespace rendering_engine
