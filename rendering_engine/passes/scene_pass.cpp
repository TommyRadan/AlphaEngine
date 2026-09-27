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

#include <rendering_engine/passes/scene_pass.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <functional>

#include <core/math/math.hpp>
#include <rendering_engine/camera/camera.hpp>
#include <rendering_engine/gpu/buffer.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/gpu/render_target.hpp>
#include <rendering_engine/gpu/shader_bindings.hpp>
#include <rendering_engine/lighting/light.hpp>
#include <rendering_engine/lighting/lights_ubo.hpp>
#include <rendering_engine/materials/material.hpp>
#include <rendering_engine/materials/material_template.hpp>
#include <rendering_engine/passes/point_shadow_pass.hpp>
#include <rendering_engine/passes/shadow_pass.hpp>
#include <rendering_engine/passes/spot_shadow_pass.hpp>
#include <rendering_engine/passes/view_globals.hpp>
#include <rendering_engine/renderables/per_draw_ubo.hpp>
#include <rendering_engine/renderables/renderable.hpp>
#include <runtime/engine.hpp>

namespace rendering_engine
{
    namespace
    {
        // Binding numbers within the per-frame bind group (slot 0), from
        // the global table in gpu/shader_bindings.hpp (which explains why
        // they stay unique across both descriptor sets of a lit pipeline).
        // The view_globals block (view_globals.hpp) takes the per-frame
        // binding.
        constexpr uint32_t view_globals_binding = gpu::shader_bindings::per_frame;
        constexpr uint32_t lights_binding = gpu::shader_bindings::lights;

        // Directional shadow data shares the per-frame group.
        constexpr uint32_t shadow_map_binding = gpu::shader_bindings::shadow_map;
        constexpr uint32_t shadow_binding = gpu::shader_bindings::shadow;

        // std140 layout of the per-frame Shadow block (the directional
        // cascades): mat4 lightViewProj[max_shadow_cascades] at offset 0
        // (256 bytes), vec4 splitDepths at 256 (each cascade's far view
        // depth), vec4 cascadeBias at 272 (each cascade's receiver bias),
        // vec4 params at 288 (x enabled, y cascade count, z caster index,
        // w PCF taps per side) and vec4 blend at 304 (x the cross-fade
        // band as a fraction of a cascade's depth range). mat4 and vec4
        // are 16-byte aligned, so there is no padding. 320 bytes total.
        constexpr size_t shadow_ubo_floats = static_cast<size_t>(max_shadow_cascades) * 16 + 4 * 4;
        constexpr size_t shadow_ubo_size = shadow_ubo_floats * sizeof(float);
        static_assert(max_shadow_cascades <= 4, "splitDepths / cascadeBias hold one vec4 lane per cascade");

        // Omni (point-light) shadow data also shares the per-frame group:
        // the UBO plus the depth cube map.
        constexpr uint32_t point_shadow_binding = gpu::shader_bindings::point_shadow;
        constexpr uint32_t point_shadow_map_binding = gpu::shader_bindings::point_shadow_map;

        // std140 layout of the PointShadow block: mat4 faceViewProj[6] at
        // offset 0 (384 bytes), vec4 lightPos at 384 (xyz position, w the
        // faces' near plane), vec4 params at 400 (x enabled, y bias,
        // z caster point index, w the faces' far plane). 416 bytes total.
        constexpr size_t point_shadow_ubo_size =
            point_shadow_face_count * sizeof(core::math::mat4) + 2 * 4 * sizeof(float);

        // Spot shadow data shares the per-frame group too, in a block
        // holding a single perspective matrix (rather than the
        // directional block's auto-fitted orthographic cascades).
        constexpr uint32_t spot_shadow_binding = gpu::shader_bindings::spot_shadow;
        constexpr uint32_t spot_shadow_map_binding = gpu::shader_bindings::spot_shadow_map;

        // std140 layout of the SpotShadow block: mat4 lightViewProj at
        // offset 0, vec4 params at offset 64 (x enabled, y bias, z caster
        // spot index). 80 bytes total.
        constexpr size_t spot_shadow_ubo_size = sizeof(core::math::mat4) + 4 * sizeof(float);
    } // namespace

    scene_pass::scene_pass(std::vector<renderable*>* registry,
                           shadow_pass* shadow,
                           point_shadow_pass* point_shadow,
                           spot_shadow_pass* spot_shadow,
                           render_stats* stats,
                           bool taa_jitter)
        : m_registry(registry), m_shadow(shadow), m_point_shadow(point_shadow), m_spot_shadow(spot_shadow),
          m_stats(stats), m_taa_jitter(taa_jitter)
    {
        auto& gpu = *runtime::current_engine().gpu;

        gpu::bind_group_layout_descriptor frame_layout_descriptor{};
        frame_layout_descriptor.entries.push_back({view_globals_binding, gpu::binding_kind::uniform_buffer});
        frame_layout_descriptor.entries.push_back({lights_binding, gpu::binding_kind::uniform_buffer});
        frame_layout_descriptor.entries.push_back({shadow_binding, gpu::binding_kind::uniform_buffer});
        // The directional cascades are one depth array read through a
        // comparison sampler (sampler2DArrayShadow in the shaders): the
        // texture entry says so, so a device that substitutes a
        // placeholder for an unset slot picks an array, and the sampler
        // shares its binding number (Vulkan folds it into that texture's
        // combined image sampler, OpenGL binds it to the same unit).
        gpu::bind_group_layout_entry shadow_map_entry{shadow_map_binding, gpu::binding_kind::texture};
        shadow_map_entry.dimension = gpu::texture_dimension::d2_array;
        frame_layout_descriptor.entries.push_back(shadow_map_entry);
        frame_layout_descriptor.entries.push_back({shadow_map_binding, gpu::binding_kind::sampler});
        frame_layout_descriptor.entries.push_back({point_shadow_binding, gpu::binding_kind::uniform_buffer});
        // The omni shadow is a depth cube (samplerCube in the shaders), so
        // the layout says so and a device that substitutes a placeholder
        // for an unset slot picks a cube.
        gpu::bind_group_layout_entry point_shadow_map_entry{point_shadow_map_binding, gpu::binding_kind::texture};
        point_shadow_map_entry.dimension = gpu::texture_dimension::cube;
        frame_layout_descriptor.entries.push_back(point_shadow_map_entry);
        frame_layout_descriptor.entries.push_back({spot_shadow_binding, gpu::binding_kind::uniform_buffer});
        frame_layout_descriptor.entries.push_back({spot_shadow_map_binding, gpu::binding_kind::texture});
        m_frame_layout = gpu.create_bind_group_layout(frame_layout_descriptor);

        gpu::buffer_descriptor ubo_descriptor{};
        ubo_descriptor.size = sizeof(view_globals);
        ubo_descriptor.usage = gpu::buffer_usage_uniform | gpu::buffer_usage_copy_dst;
        ubo_descriptor.hint = gpu::buffer_usage_hint::dynamic_data;
        m_frame_ubo = gpu.create_buffer(ubo_descriptor);

        // The unjittered view_globals twin only exists when jitter is
        // active; it backs the overlay bind group built below.
        if (m_taa_jitter)
        {
            m_overlay_frame_ubo = gpu.create_buffer(ubo_descriptor);
        }

        gpu::buffer_descriptor lights_descriptor{};
        lights_descriptor.size = sizeof(gpu_lights);
        lights_descriptor.usage = gpu::buffer_usage_uniform | gpu::buffer_usage_copy_dst;
        lights_descriptor.hint = gpu::buffer_usage_hint::dynamic_data;
        m_lights_ubo = gpu.create_buffer(lights_descriptor);

        gpu::buffer_descriptor shadow_descriptor{};
        shadow_descriptor.size = shadow_ubo_size;
        shadow_descriptor.usage = gpu::buffer_usage_uniform | gpu::buffer_usage_copy_dst;
        shadow_descriptor.hint = gpu::buffer_usage_hint::dynamic_data;
        m_shadow_ubo = gpu.create_buffer(shadow_descriptor);

        gpu::buffer_descriptor point_shadow_descriptor{};
        point_shadow_descriptor.size = point_shadow_ubo_size;
        point_shadow_descriptor.usage = gpu::buffer_usage_uniform | gpu::buffer_usage_copy_dst;
        point_shadow_descriptor.hint = gpu::buffer_usage_hint::dynamic_data;
        m_point_shadow_ubo = gpu.create_buffer(point_shadow_descriptor);

        gpu::buffer_descriptor spot_shadow_descriptor{};
        spot_shadow_descriptor.size = spot_shadow_ubo_size;
        spot_shadow_descriptor.usage = gpu::buffer_usage_uniform | gpu::buffer_usage_copy_dst;
        spot_shadow_descriptor.hint = gpu::buffer_usage_hint::dynamic_data;
        m_spot_shadow_ubo = gpu.create_buffer(spot_shadow_descriptor);

        gpu::bind_group_descriptor frame_bind_group_descriptor{};
        frame_bind_group_descriptor.layout = m_frame_layout;

        gpu::binding_value view_globals_slot{};
        view_globals_slot.binding = view_globals_binding;
        view_globals_slot.kind = gpu::binding_kind::uniform_buffer;
        view_globals_slot.buffer_value = m_frame_ubo;
        frame_bind_group_descriptor.entries.push_back(view_globals_slot);

        gpu::binding_value lights_slot{};
        lights_slot.binding = lights_binding;
        lights_slot.kind = gpu::binding_kind::uniform_buffer;
        lights_slot.buffer_value = m_lights_ubo;
        frame_bind_group_descriptor.entries.push_back(lights_slot);

        gpu::binding_value shadow_slot{};
        shadow_slot.binding = shadow_binding;
        shadow_slot.kind = gpu::binding_kind::uniform_buffer;
        shadow_slot.buffer_value = m_shadow_ubo;
        frame_bind_group_descriptor.entries.push_back(shadow_slot);

        // The cascade array and its comparison sampler are owned by the
        // shadow pass. Their handles are stable across frames, so capture
        // them once here; invalid handles (no shadow pass) simply bind
        // nothing and the enabled flag keeps the map unsampled.
        gpu::binding_value shadow_map_slot{};
        shadow_map_slot.binding = shadow_map_binding;
        shadow_map_slot.kind = gpu::binding_kind::texture;
        shadow_map_slot.texture_value = m_shadow != nullptr ? m_shadow->shadow_map() : gpu::texture{};
        frame_bind_group_descriptor.entries.push_back(shadow_map_slot);

        gpu::binding_value shadow_sampler_slot{};
        shadow_sampler_slot.binding = shadow_map_binding;
        shadow_sampler_slot.kind = gpu::binding_kind::sampler;
        shadow_sampler_slot.sampler_value = m_shadow != nullptr ? m_shadow->shadow_sampler() : gpu::sampler{};
        frame_bind_group_descriptor.entries.push_back(shadow_sampler_slot);

        gpu::binding_value point_shadow_slot{};
        point_shadow_slot.binding = point_shadow_binding;
        point_shadow_slot.kind = gpu::binding_kind::uniform_buffer;
        point_shadow_slot.buffer_value = m_point_shadow_ubo;
        frame_bind_group_descriptor.entries.push_back(point_shadow_slot);

        // The omni depth cube is owned by the point shadow pass; its handle
        // is stable across frames. An invalid handle (no pass) binds nothing
        // and the lit shader's enabled flag keeps it unsampled.
        gpu::binding_value point_map_slot{};
        point_map_slot.binding = point_shadow_map_binding;
        point_map_slot.kind = gpu::binding_kind::texture;
        point_map_slot.texture_value = m_point_shadow != nullptr ? m_point_shadow->shadow_map() : gpu::texture{};
        frame_bind_group_descriptor.entries.push_back(point_map_slot);

        gpu::binding_value spot_shadow_slot{};
        spot_shadow_slot.binding = spot_shadow_binding;
        spot_shadow_slot.kind = gpu::binding_kind::uniform_buffer;
        spot_shadow_slot.buffer_value = m_spot_shadow_ubo;
        frame_bind_group_descriptor.entries.push_back(spot_shadow_slot);

        // The spot shadow map is owned by the spot shadow pass. An invalid
        // handle (no pass) binds nothing and the lit shader's enabled flag
        // keeps it unsampled, just like the directional and omni maps.
        gpu::binding_value spot_map_slot{};
        spot_map_slot.binding = spot_shadow_map_binding;
        spot_map_slot.kind = gpu::binding_kind::texture;
        spot_map_slot.texture_value = m_spot_shadow != nullptr ? m_spot_shadow->shadow_map() : gpu::texture{};
        frame_bind_group_descriptor.entries.push_back(spot_map_slot);

        m_frame_bind_group = gpu.create_bind_group(frame_bind_group_descriptor);

        // The overlay twin shares every binding with the main group except
        // the view_globals block (entries[0], pushed first above), which it
        // points at the unjittered buffer so the debug pass projects without
        // the sub-pixel jitter.
        if (m_taa_jitter)
        {
            frame_bind_group_descriptor.entries[0].buffer_value = m_overlay_frame_ubo;
            m_overlay_frame_bind_group = gpu.create_bind_group(frame_bind_group_descriptor);
        }
    }

    scene_pass::~scene_pass()
    {
        auto& gpu = *runtime::current_engine().gpu;
        if (m_overlay_frame_bind_group.valid())
        {
            gpu.destroy(m_overlay_frame_bind_group);
            m_overlay_frame_bind_group = {};
        }
        if (m_overlay_frame_ubo.valid())
        {
            gpu.destroy(m_overlay_frame_ubo);
            m_overlay_frame_ubo = {};
        }
        if (m_frame_bind_group.valid())
        {
            gpu.destroy(m_frame_bind_group);
            m_frame_bind_group = {};
        }
        if (m_spot_shadow_ubo.valid())
        {
            gpu.destroy(m_spot_shadow_ubo);
            m_spot_shadow_ubo = {};
        }
        if (m_point_shadow_ubo.valid())
        {
            gpu.destroy(m_point_shadow_ubo);
            m_point_shadow_ubo = {};
        }
        if (m_shadow_ubo.valid())
        {
            gpu.destroy(m_shadow_ubo);
            m_shadow_ubo = {};
        }
        if (m_lights_ubo.valid())
        {
            gpu.destroy(m_lights_ubo);
            m_lights_ubo = {};
        }
        if (m_frame_ubo.valid())
        {
            gpu.destroy(m_frame_ubo);
            m_frame_ubo = {};
        }
        if (m_frame_layout.valid())
        {
            gpu.destroy(m_frame_layout);
            m_frame_layout = {};
        }
    }

    gpu::bind_group_layout scene_pass::frame_bind_group_layout() const
    {
        return m_frame_layout;
    }

    gpu::bind_group scene_pass::frame_bind_group() const
    {
        return m_frame_bind_group;
    }

    gpu::bind_group scene_pass::overlay_frame_bind_group() const
    {
        // Falls back to the (already unjittered) main group when jitter is
        // off, so the debug pass needs no special-casing.
        return m_overlay_frame_bind_group.valid() ? m_overlay_frame_bind_group : m_frame_bind_group;
    }

    void scene_pass::prepare(const frame_context& ctx)
    {
        // Once per frame: the depth pre-pass may already have prepared
        // this frame ahead of record(), and must see what record() draws.
        if (m_prepared_frame == ctx.frame_index)
        {
            return;
        }
        m_prepared_frame = ctx.frame_index;
        m_depth_prepassed = false;
        m_items.clear();

        auto& eng = runtime::current_engine();
        auto& gpu = *eng.gpu;

        // Reset this frame's stats up front. The renderable count is known
        // regardless of whether a camera is attached; the draw totals stay
        // zero on no-camera frames (nothing is collected below).
        if (m_stats != nullptr)
        {
            *m_stats = render_stats{};
            m_stats->scene_renderables = static_cast<uint32_t>(m_registry->size());
            // The shadow passes ran ahead of this one this frame; carry their
            // culling tallies over so the overlay reads one struct.
            m_stats->shadow_culled = m_shadow != nullptr ? m_shadow->culled_count() : 0u;
            m_stats->point_shadow_culled = m_point_shadow != nullptr ? m_point_shadow->culled_count() : 0u;
            m_stats->spot_shadow_culled = m_spot_shadow != nullptr ? m_spot_shadow->culled_count() : 0u;
        }

        // No camera, no scene: nothing to upload or collect.
        if (ctx.active_camera == nullptr)
        {
            return;
        }

        // Refill the view_globals block before any draw consults it: the
        // camera matrices and their inverses, the camera position, the
        // viewport, the clock, the jitter with the previous
        // view-projection, and the fog, all from the frame context.
        //
        // Temporal-AA sub-pixel jitter: the projection is offset by the
        // Halton step the context published for this frame (already scaled
        // to the live target size) so this frame samples the scene a
        // fraction of a pixel away from the last. The jitter feeds the
        // taa_pass accumulation and is otherwise invisible — culling still
        // uses the camera's unjittered frustum, the skybox applies the same
        // offset so its depth test agrees, and the velocity pass subtracts
        // it again. The overlay group carries the same view unjittered so
        // the debug pass — which paints after the TAA resolve and so cannot
        // average the jitter away — draws steady gizmos.
        if (m_overlay_frame_ubo.valid())
        {
            const view_globals overlay_globals = make_view_globals(ctx, false);
            gpu.write_buffer(m_overlay_frame_ubo, &overlay_globals, sizeof(view_globals), 0);
        }
        const view_globals globals = make_view_globals(ctx, m_taa_jitter);
        gpu.write_buffer(m_frame_ubo, &globals, sizeof(view_globals), 0);

        // Pack every live light into the std140 lights block and upload
        // it alongside the view_globals block. Lit materials read this
        // from the per-frame group; the scene pass owns it so light objects
        // never touch the GPU directly.
        gpu_lights lights_payload{};
        pack_lights(registered_lights(), lights_payload);
        gpu.write_buffer(m_lights_ubo, &lights_payload, sizeof(gpu_lights), 0);

        // Upload the directional shadow block: each cascade's light-space
        // matrix, split depth and receiver bias, then {enabled, cascade
        // count, caster index, PCF taps} and the blend band. When no
        // caster is active the enabled flag stays 0 and the lit shader
        // skips sampling, so the matrices and the (cleared) layers go
        // unused.
        std::array<float, shadow_ubo_floats> shadow_payload{};
        if (m_shadow != nullptr && m_shadow->has_shadow())
        {
            constexpr size_t splits_offset = static_cast<size_t>(max_shadow_cascades) * 16;
            constexpr size_t bias_offset = splits_offset + 4;
            constexpr size_t params_offset = bias_offset + 4;
            constexpr size_t blend_offset = params_offset + 4;
            const int cascades = m_shadow->cascade_count();
            for (int cascade = 0; cascade < cascades; ++cascade)
            {
                const auto lane = static_cast<size_t>(cascade);
                std::memcpy(shadow_payload.data() + lane * 16,
                            m_shadow->light_view_projection(cascade).data(),
                            sizeof(core::math::mat4));
                shadow_payload[splits_offset + lane] = m_shadow->split_depth(cascade);
                shadow_payload[bias_offset + lane] = m_shadow->depth_bias(cascade);
            }
            shadow_payload[params_offset] = 1.0f;
            shadow_payload[params_offset + 1] = static_cast<float>(cascades);
            shadow_payload[params_offset + 2] = static_cast<float>(m_shadow->shadow_light_index());
            shadow_payload[params_offset + 3] = static_cast<float>(m_shadow->pcf_kernel());
            shadow_payload[blend_offset] = m_shadow->cascade_blend();
        }
        gpu.write_buffer(m_shadow_ubo, shadow_payload.data(), shadow_ubo_size, 0);

        // Upload the omni shadow block: six face matrices, the light position
        // with the faces' near plane, and {enabled, bias, caster point index,
        // far plane}. enabled stays 0 with no caster so the lit shader skips
        // the (cleared) cube.
        std::array<float, 104> point_shadow_payload{};
        if (m_point_shadow != nullptr && m_point_shadow->has_shadow())
        {
            for (int face = 0; face < point_shadow_face_count; ++face)
            {
                std::memcpy(point_shadow_payload.data() + face * 16,
                            m_point_shadow->light_view_projection(face).data(),
                            sizeof(core::math::mat4));
            }
            const auto& pos = m_point_shadow->light_position();
            point_shadow_payload[96] = pos.x;
            point_shadow_payload[97] = pos.y;
            point_shadow_payload[98] = pos.z;
            point_shadow_payload[99] = m_point_shadow->shadow_near();
            point_shadow_payload[100] = 1.0f; // enabled
            point_shadow_payload[101] = m_point_shadow->depth_bias();
            point_shadow_payload[102] = static_cast<float>(m_point_shadow->shadow_point_index());
            point_shadow_payload[103] = m_point_shadow->shadow_far();
        }
        gpu.write_buffer(m_point_shadow_ubo, point_shadow_payload.data(), point_shadow_ubo_size, 0);

        // Upload the spot shadow block: the light-space matrix plus
        // {enabled, bias, caster spot index}. enabled stays 0 with no
        // caster so the lit shader skips the (cleared) map.
        std::array<float, 20> spot_shadow_payload{};
        if (m_spot_shadow != nullptr && m_spot_shadow->has_shadow())
        {
            std::memcpy(
                spot_shadow_payload.data(), m_spot_shadow->light_view_projection().data(), sizeof(core::math::mat4));
            spot_shadow_payload[16] = 1.0f;
            spot_shadow_payload[17] = m_spot_shadow->depth_bias();
            spot_shadow_payload[18] = static_cast<float>(m_spot_shadow->shadow_spot_index());
        }
        gpu.write_buffer(m_spot_shadow_ubo, spot_shadow_payload.data(), spot_shadow_ubo_size, 0);

        // Layer-filter, frustum-cull, then collect. A renderable whose
        // layer_mask shares no bit with the camera's culling mask is
        // skipped outright, the same as a frustum cull below it, so an
        // editor-only helper never reaches a gameplay camera that has
        // narrowed its mask. A renderable that reports world bounds is
        // then tested against the camera frustum and skipped when it lies
        // wholly outside, so it never builds a draw item or writes its
        // per-draw UBO; one with no bounds (fullscreen effects, gizmos) is
        // always collected. The frustum is the camera's unjittered one —
        // the TAA offset is a sub-pixel shift that no plane test could
        // tell apart.
        //
        // Every survivor's items get a sort key from @ref make_sort_key
        // right after they are collected: the queue from the item's
        // material (opaque or transparent), the view-space depth to the
        // renderable's world bounds centre (0 for a renderable with no
        // bounds), and the item's own pipeline id. The single per-frame
        // list is then sorted by that key ascending, which — by
        // construction of the key — sorts opaque items front-to-back for
        // early-Z rejection and transparent ones back-to-front so blending
        // composites correctly, with the pipeline id as a further
        // tie-break within equal depth; the material instance breaks any
        // remaining tie so a run of identical keys still shares one
        // bind-group rebind. The sort is stable, so within equal keys
        // submission order still applies.
        const core::math::frustum view_frustum = ctx.active_camera->get_frustum();
        const core::math::mat4 view_matrix = ctx.active_camera->get_view_matrix();
        const uint32_t camera_mask = ctx.active_camera->culling_mask();
        uint32_t submitted = 0;
        uint32_t culled = 0;
        for (auto* r : *m_registry)
        {
            if ((r->layer_mask & camera_mask) == 0)
            {
                ++culled;
                continue;
            }
            core::math::aabb bounds;
            const bool has_bounds = r->world_bounds(bounds);
            if (has_bounds && !view_frustum.intersects(bounds))
            {
                ++culled;
                continue;
            }
            ++submitted;
            const std::size_t first_item = m_items.size();
            r->collect_draw_items(m_items);

            float view_depth = 0.0f;
            if (has_bounds)
            {
                const core::math::vec3 centre = bounds.center();
                // View space looks down -z (the GL convention
                // core::math::look_at builds), so forward depth is
                // -(view * p).z — the same convention shadow_pass uses to
                // fit its cascades.
                view_depth = -(view_matrix * core::math::vec4{centre, 1.0f}).z;
            }
            for (std::size_t i = first_item; i < m_items.size(); ++i)
            {
                draw_item& item = m_items[i];
                const render_queue queue =
                    item.mat->params().transparent ? render_queue::transparent : render_queue::opaque;
                item.sort_key = make_sort_key(queue, view_depth, item.mat->pipeline(item.mirrored).id);
            }
        }
        std::stable_sort(m_items.begin(),
                         m_items.end(),
                         [](const draw_item& a, const draw_item& b)
                         {
                             if (a.sort_key != b.sort_key)
                             {
                                 return a.sort_key < b.sort_key;
                             }
                             return std::less<const material*>{}(a.mat, b.mat);
                         });

        // Tally this frame's draw statistics for the debug overlay. Each
        // item is one draw call; primitive / vertex counts scale by the
        // item's instance count and land under the topology its
        // material's template assembles (triangles, line segments or
        // points). Indexed draws count index_count vertices (vertices
        // fetched), non-indexed count vertex_count.
        if (m_stats != nullptr)
        {
            m_stats->submitted = submitted;
            m_stats->culled = culled;
            m_stats->draw_calls = static_cast<uint32_t>(m_items.size());
            for (const auto& item : m_items)
            {
                const uint32_t instances = item.instance_count == 0 ? 1u : item.instance_count;
                const uint32_t verts = item.index_buffer.valid() ? item.index_count : item.vertex_count;
                const uint64_t submitted_vertices = static_cast<uint64_t>(verts) * instances;
                m_stats->instances += instances;
                m_stats->vertices += submitted_vertices;
                tally_primitives(*m_stats, item.mat->get_template().descriptor().topology, submitted_vertices);
            }
        }
    }

    void scene_pass::record_depth_prepass(gpu::render_pass_encoder& pass_encoder)
    {
        m_depth_prepassed = true;
        dispatch(pass_encoder, draw_phase::depth_prepass);
    }

    void scene_pass::record(gpu::command_encoder& encoder, const frame_context& ctx)
    {
        // A no-op when the depth pre-pass already prepared this frame.
        prepare(ctx);

        // Render into the HDR scene-colour target so the post chain
        // can sample real luminance. The tonemap post pass maps the
        // result onto the swapchain before the UI composites. The depth
        // is cleared unless the depth pre-pass laid the opaque queue's
        // depth into it this frame, in which case it is loaded and the
        // pre-passed items below test against it without writing.
        gpu::render_pass_descriptor descriptor{};
        descriptor.target = ctx.scene_color_target;
        descriptor.color[0].load = gpu::load_op::clear;
        descriptor.color[0].clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
        descriptor.use_depth = true;
        descriptor.depth.load = m_depth_prepassed ? gpu::load_op::load : gpu::load_op::clear;
        descriptor.depth.clear_depth = 1.0f;

        auto pass_encoder = encoder.begin_render_pass(descriptor);

        // No camera, no scene — but we still opened the pass so the
        // HDR target gets cleared to black. Otherwise the tonemap
        // would map stale or driver-uninitialised contents into the
        // swapchain on no-camera frames. (The depth pre-pass never runs
        // without a camera, so the depth is cleared here too.)
        if (ctx.active_camera == nullptr)
        {
            pass_encoder->end();
            return;
        }

        dispatch(*pass_encoder, draw_phase::shading);

        pass_encoder->end();
    }

    void scene_pass::dispatch(gpu::render_pass_encoder& pass_encoder, draw_phase phase)
    {
        uint64_t last_pipeline_id = 0;
        const material* last_material = nullptr;
        bool first_iter = true;
        for (const auto& item : m_items)
        {
            // An item is pre-passed when the pre-pass ran this frame and
            // its material takes part: the pre-pass draws exactly those
            // (the list is sorted, so the opaque ones front-to-back) with
            // the depth-only twin of the item's pipeline, and this pass
            // shades them with the twin that tests less-or-equal against
            // that depth without writing it. Everything else — the
            // transparent queue, surfaces that skip the depth test or
            // write, templates that opt out — is drawn by this pass alone
            // with its ordinary pipeline.
            const bool prepassed = m_depth_prepassed && item.mat->draws_in_depth_prepass();
            gpu::pipeline pipeline{};
            if (phase == draw_phase::depth_prepass)
            {
                if (!prepassed)
                {
                    continue;
                }
                pipeline = item.mat->depth_prepass_pipeline(item.mirrored);
            }
            else
            {
                pipeline =
                    prepassed ? item.mat->depth_prepassed_pipeline(item.mirrored) : item.mat->pipeline(item.mirrored);
            }

            if (pipeline.id != last_pipeline_id)
            {
                pass_encoder.set_pipeline(pipeline);

                // Per-frame bind group bound once per frame after
                // the first pipeline change; the binding sticks
                // across subsequent set_pipeline calls within the
                // same pass, since every template's pipelines share
                // the per-frame layout and push-constant ranges.
                if (first_iter)
                {
                    pass_encoder.set_bind_group(0, m_frame_bind_group);
                    first_iter = false;
                }
                last_pipeline_id = pipeline.id;
                // A new pipeline invalidates the per-material binding
                // even when the instance is unchanged.
                last_material = nullptr;
            }

            // The per-material group follows the instance, not the
            // pipeline: several instances of one template share a
            // pipeline but each carries its own parameter block.
            if (item.mat != last_material)
            {
                if (item.mat->per_material_bind_group().valid())
                {
                    pass_encoder.set_bind_group(item.mat->per_material_slot(), item.mat->per_material_bind_group());
                }
                last_material = item.mat;
            }

            // Instanced renderables keep their per-instance data in a
            // vertex stream (slot 1), not a PerDraw block, so the per-draw
            // data is optional. A rigid renderable's block is pushed where
            // the device takes push constants, in the pre-pass as in the
            // shading pass; otherwise its group is the per-draw ring's
            // shared one and its dynamic offset picks the renderable's
            // block.
            bind_per_draw(pass_encoder, item, item.mat->per_draw_slot());
            pass_encoder.set_vertex_buffer(0, item.vertex_buffer, 0, item.vertex_stride);
            if (item.instance_buffer.valid())
            {
                pass_encoder.set_vertex_buffer(1, item.instance_buffer, 0, item.instance_stride);
            }
            if (item.index_buffer.valid())
            {
                pass_encoder.set_index_buffer(item.index_buffer, item.index_format);
                if (item.indirect_buffer.valid())
                {
                    // Instanced draw: index and instance counts come from
                    // the indirect command record (see @ref instanced_mesh).
                    pass_encoder.draw_indexed_indirect(item.indirect_buffer, 0);
                }
                else
                {
                    pass_encoder.draw_indexed(
                        item.index_count, item.instance_count, item.first_index, item.vertex_offset);
                }
            }
            else
            {
                pass_encoder.draw(item.vertex_count, item.instance_count, static_cast<uint32_t>(item.vertex_offset));
            }
        }
    }
} // namespace rendering_engine
