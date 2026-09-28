// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/passes/scene_pass.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <memory>

#include <core/job_pool.hpp>
#include <core/log.hpp>
#include <core/math/math.hpp>
#include <rendering_engine/gpu/buffer.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/gpu/render_target.hpp>
#include <rendering_engine/gpu/shader_bindings.hpp>
#include <rendering_engine/lighting/lights_ubo.hpp>
#include <rendering_engine/materials/material.hpp>
#include <rendering_engine/materials/material_template.hpp>
#include <rendering_engine/passes/view_globals.hpp>
#include <rendering_engine/renderables/per_draw_ubo.hpp>

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

    scene_pass::scene_pass(gpu::device& device,
                           core::job_pool* jobs,
                           render_stats* stats,
                           bool taa_jitter,
                           uint32_t parallel_draw_threshold)
        : m_device(&device), m_jobs(jobs), m_stats(stats), m_taa_jitter(taa_jitter),
          m_parallel_draw_threshold(parallel_draw_threshold)
    {
        auto& gpu = *m_device;

        if (m_parallel_draw_threshold != 0)
        {
            LOG_INF("scene_pass: draws record in parallel above %u draws (and per chunk)", m_parallel_draw_threshold);
        }

        gpu::bind_group_layout_descriptor frame_layout_descriptor{};
        frame_layout_descriptor.entries.push_back({view_globals_binding, gpu::binding_kind::uniform_buffer});
        frame_layout_descriptor.entries.push_back({lights_binding, gpu::binding_kind::uniform_buffer});
        frame_layout_descriptor.entries.push_back({shadow_binding, gpu::binding_kind::uniform_buffer});
        // The directional cascades are one depth array read through a
        // comparison sampler (sampler2DArrayShadow in the shaders): the
        // texture entry says so, so a device that substitutes a
        // placeholder for an unset slot picks an array, and the sampler
        // shares its binding number (Vulkan folds it into that texture's
        // combined image sampler).
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
    }

    scene_pass::~scene_pass()
    {
        auto& gpu = *m_device;
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
        if (m_frame_layout.valid())
        {
            gpu.destroy(m_frame_layout);
            m_frame_layout = {};
        }
    }

    scene_pass::view_data::view_data(gpu::device& device, bool taa_jitter) : device{&device}
    {
        gpu::buffer_descriptor ubo_descriptor{};
        ubo_descriptor.size = sizeof(view_globals);
        ubo_descriptor.usage = gpu::buffer_usage_uniform | gpu::buffer_usage_copy_dst;
        ubo_descriptor.hint = gpu::buffer_usage_hint::dynamic_data;
        frame_ubo = device.create_buffer(ubo_descriptor);

        // The unjittered view_globals twin only exists when jitter is
        // active; it backs the overlay bind group.
        if (taa_jitter)
        {
            overlay_frame_ubo = device.create_buffer(ubo_descriptor);
        }
    }

    scene_pass::view_data::~view_data()
    {
        auto& gpu = *device;
        if (overlay_frame_bind_group.valid())
        {
            gpu.destroy(overlay_frame_bind_group);
        }
        if (overlay_frame_ubo.valid())
        {
            gpu.destroy(overlay_frame_ubo);
        }
        if (frame_bind_group.valid())
        {
            gpu.destroy(frame_bind_group);
        }
        if (frame_ubo.valid())
        {
            gpu.destroy(frame_ubo);
        }
    }

    gpu::bind_group_layout scene_pass::frame_bind_group_layout() const
    {
        return m_frame_layout;
    }

    void scene_pass::update_frame_bind_groups(view_data& view,
                                              const directional_shadow_data* shadow,
                                              const point_shadow_data* point_shadow,
                                              const spot_shadow_data* spot_shadow)
    {
        // The shadow maps (and the directional comparison sampler) this
        // pass binds are owned by the shadow passes and reach it through
        // the frame-global store, like every other texture a pass samples
        // but does not own. A view's groups are built on its first frame
        // and rebuilt only if one of those handles changes; the uniform
        // buffers behind them are this pass's and the view's own and never
        // change.
        const gpu::texture shadow_map = shadow != nullptr ? shadow->map : gpu::texture{};
        const gpu::sampler shadow_sampler = shadow != nullptr ? shadow->sampler : gpu::sampler{};
        const gpu::texture point_shadow_map = point_shadow != nullptr ? point_shadow->map : gpu::texture{};
        const gpu::texture spot_shadow_map = spot_shadow != nullptr ? spot_shadow->map : gpu::texture{};
        if (view.frame_bind_group.valid() && shadow_map == view.bound_shadow_map &&
            shadow_sampler == view.bound_shadow_sampler && point_shadow_map == view.bound_point_shadow_map &&
            spot_shadow_map == view.bound_spot_shadow_map)
        {
            return;
        }

        auto& gpu = *m_device;
        if (view.overlay_frame_bind_group.valid())
        {
            gpu.destroy(view.overlay_frame_bind_group);
            view.overlay_frame_bind_group = {};
        }
        if (view.frame_bind_group.valid())
        {
            gpu.destroy(view.frame_bind_group);
            view.frame_bind_group = {};
        }

        gpu::bind_group_descriptor frame_bind_group_descriptor{};
        frame_bind_group_descriptor.layout = m_frame_layout;

        gpu::binding_value view_globals_slot{};
        view_globals_slot.binding = view_globals_binding;
        view_globals_slot.kind = gpu::binding_kind::uniform_buffer;
        view_globals_slot.buffer_value = view.frame_ubo;
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
        // shadow pass; invalid handles (nothing published) simply bind
        // nothing and the enabled flag keeps the map unsampled.
        gpu::binding_value shadow_map_slot{};
        shadow_map_slot.binding = shadow_map_binding;
        shadow_map_slot.kind = gpu::binding_kind::texture;
        shadow_map_slot.texture_value = shadow_map;
        frame_bind_group_descriptor.entries.push_back(shadow_map_slot);

        gpu::binding_value shadow_sampler_slot{};
        shadow_sampler_slot.binding = shadow_map_binding;
        shadow_sampler_slot.kind = gpu::binding_kind::sampler;
        shadow_sampler_slot.sampler_value = shadow_sampler;
        frame_bind_group_descriptor.entries.push_back(shadow_sampler_slot);

        gpu::binding_value point_shadow_slot{};
        point_shadow_slot.binding = point_shadow_binding;
        point_shadow_slot.kind = gpu::binding_kind::uniform_buffer;
        point_shadow_slot.buffer_value = m_point_shadow_ubo;
        frame_bind_group_descriptor.entries.push_back(point_shadow_slot);

        // The omni depth cube is owned by the point shadow pass. An invalid
        // handle (nothing published) binds nothing and the lit shader's
        // enabled flag keeps it unsampled.
        gpu::binding_value point_map_slot{};
        point_map_slot.binding = point_shadow_map_binding;
        point_map_slot.kind = gpu::binding_kind::texture;
        point_map_slot.texture_value = point_shadow_map;
        frame_bind_group_descriptor.entries.push_back(point_map_slot);

        gpu::binding_value spot_shadow_slot{};
        spot_shadow_slot.binding = spot_shadow_binding;
        spot_shadow_slot.kind = gpu::binding_kind::uniform_buffer;
        spot_shadow_slot.buffer_value = m_spot_shadow_ubo;
        frame_bind_group_descriptor.entries.push_back(spot_shadow_slot);

        // The spot shadow map is owned by the spot shadow pass. An invalid
        // handle (nothing published) binds nothing and the lit shader's
        // enabled flag keeps it unsampled, just like the directional and
        // omni maps.
        gpu::binding_value spot_map_slot{};
        spot_map_slot.binding = spot_shadow_map_binding;
        spot_map_slot.kind = gpu::binding_kind::texture;
        spot_map_slot.texture_value = spot_shadow_map;
        frame_bind_group_descriptor.entries.push_back(spot_map_slot);

        view.frame_bind_group = gpu.create_bind_group(frame_bind_group_descriptor);

        // The overlay twin shares every binding with the main group except
        // the view_globals block (entries[0], pushed first above), which it
        // points at the unjittered buffer so the debug pass projects without
        // the sub-pixel jitter.
        if (view.overlay_frame_ubo.valid())
        {
            frame_bind_group_descriptor.entries[0].buffer_value = view.overlay_frame_ubo;
            view.overlay_frame_bind_group = gpu.create_bind_group(frame_bind_group_descriptor);
        }

        view.bound_shadow_map = shadow_map;
        view.bound_shadow_sampler = shadow_sampler;
        view.bound_point_shadow_map = point_shadow_map;
        view.bound_spot_shadow_map = spot_shadow_map;
    }

    void scene_pass::prepare(const frame_context& ctx)
    {
        const resource_store& resources = *ctx.resources;

        // The depth pre-pass, which prepares ahead of this pass, publishes
        // its target on a frame it lays the depth down; a frame it skips
        // clears the depth here again.
        m_depth_prepassed = resources.find(frame_resources::depth_prepass) != nullptr;
        m_target = resources.get(frame_resources::scene_color).target;
        m_items.clear();
        m_pipelines.clear();
        m_depth_item_end = 0;

        // The shadow passes prepared ahead of this one this frame; their
        // maps, fits and culling tallies feed the groups, the uploads and
        // the stats below.
        const directional_shadow_data* shadow = resources.find(frame_resources::directional_shadow);
        const point_shadow_data* point_shadow = resources.find(frame_resources::point_shadow);
        const spot_shadow_data* spot_shadow = resources.find(frame_resources::spot_shadow);

        // A view's per-frame groups exist from its first prepare on, camera
        // or not: the depth pre-pass, this pass and the passes that bind
        // them later in the view all read them. The overlay twin falls back
        // to the (already unjittered) main group when jitter is off, so its
        // consumers need no special-casing.
        view_data& view = ctx.view->state<view_data>(*this, *m_device, m_taa_jitter);
        update_frame_bind_groups(view, shadow, point_shadow, spot_shadow);
        m_frame_bind_group = view.frame_bind_group;
        scene_view_data published{};
        published.frame_layout = m_frame_layout;
        published.frame_group = view.frame_bind_group;
        published.overlay_frame_group =
            view.overlay_frame_bind_group.valid() ? view.overlay_frame_bind_group : view.frame_bind_group;
        published.depth_prepass = this;
        ctx.resources->publish(frame_resources::scene_view, published);

        auto& gpu = *m_device;

        // Reset this frame's stats up front. The mesh proxy count is known
        // regardless of whether a camera is attached; the draw totals stay
        // zero on no-camera frames (nothing is collected below).
        if (m_stats != nullptr)
        {
            *m_stats = render_stats{};
            m_stats->scene_renderables = static_cast<uint32_t>(ctx.scene_draws.size());
            // The shadow passes prepared ahead of this one this frame; carry
            // their culling tallies over so the overlay reads one struct.
            m_stats->shadow_culled = shadow != nullptr ? shadow->culled : 0u;
            m_stats->point_shadow_culled = point_shadow != nullptr ? point_shadow->culled : 0u;
            m_stats->spot_shadow_culled = spot_shadow != nullptr ? spot_shadow->culled : 0u;
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
        // Halton step the renderer published for this frame (already scaled
        // to the live target size) so this frame samples the scene a
        // fraction of a pixel away from the last. The jitter feeds the
        // taa_pass accumulation and is otherwise invisible — culling still
        // uses the camera's unjittered frustum, the skybox applies the same
        // offset so its depth test agrees, and the velocity pass subtracts
        // it again. The overlay group carries the same view unjittered so
        // the debug pass — which paints after the TAA resolve and so cannot
        // average the jitter away — draws steady gizmos.
        if (view.overlay_frame_ubo.valid())
        {
            const view_globals overlay_globals = make_view_globals(ctx, false);
            gpu.write_buffer(view.overlay_frame_ubo, &overlay_globals, sizeof(view_globals), 0);
        }
        const view_globals globals = make_view_globals(ctx, m_taa_jitter);
        gpu.write_buffer(view.frame_ubo, &globals, sizeof(view_globals), 0);

        // Pack every live light into the std140 lights block and upload
        // it alongside the view_globals block. Lit materials read this
        // from the per-frame group; the scene pass owns it so light objects
        // never touch the GPU directly. This block and the shadow blocks
        // below hold the same bytes for every view of a frame (the lights
        // and the shared shadow maps), so every view's group binds the
        // same buffers.
        gpu_lights lights_payload{};
        pack_lights(ctx.lights, lights_payload);
        gpu.write_buffer(m_lights_ubo, &lights_payload, sizeof(gpu_lights), 0);

        // Upload the directional shadow block: each cascade's light-space
        // matrix, split depth and receiver bias, then {enabled, cascade
        // count, caster index, PCF taps} and the blend band. When no
        // caster is active the enabled flag stays 0 and the lit shader
        // skips sampling, so the matrices and the (cleared) layers go
        // unused.
        std::array<float, shadow_ubo_floats> shadow_payload{};
        if (shadow != nullptr && shadow->active)
        {
            constexpr size_t splits_offset = static_cast<size_t>(max_shadow_cascades) * 16;
            constexpr size_t bias_offset = splits_offset + 4;
            constexpr size_t params_offset = bias_offset + 4;
            constexpr size_t blend_offset = params_offset + 4;
            const int cascades = shadow->cascade_count;
            for (int cascade = 0; cascade < cascades; ++cascade)
            {
                const auto lane = static_cast<size_t>(cascade);
                std::memcpy(shadow_payload.data() + lane * 16,
                            shadow->light_view_projection[lane].data(),
                            sizeof(core::math::mat4));
                shadow_payload[splits_offset + lane] = shadow->split_depth[lane];
                shadow_payload[bias_offset + lane] = shadow->depth_bias[lane];
            }
            shadow_payload[params_offset] = 1.0f;
            shadow_payload[params_offset + 1] = static_cast<float>(cascades);
            shadow_payload[params_offset + 2] = static_cast<float>(shadow->light_index);
            shadow_payload[params_offset + 3] = static_cast<float>(shadow->pcf_kernel);
            shadow_payload[blend_offset] = shadow->cascade_blend;
        }
        gpu.write_buffer(m_shadow_ubo, shadow_payload.data(), shadow_ubo_size, 0);

        // Upload the omni shadow block: six face matrices, the light position
        // with the faces' near plane, and {enabled, bias, caster point index,
        // far plane}. enabled stays 0 with no caster so the lit shader skips
        // the (cleared) cube.
        std::array<float, 104> point_shadow_payload{};
        if (point_shadow != nullptr && point_shadow->active)
        {
            for (int face = 0; face < point_shadow_face_count; ++face)
            {
                std::memcpy(point_shadow_payload.data() + face * 16,
                            point_shadow->face_view_projection[static_cast<size_t>(face)].data(),
                            sizeof(core::math::mat4));
            }
            const auto& pos = point_shadow->light_position;
            point_shadow_payload[96] = pos.x;
            point_shadow_payload[97] = pos.y;
            point_shadow_payload[98] = pos.z;
            point_shadow_payload[99] = point_shadow->near_plane;
            point_shadow_payload[100] = 1.0f; // enabled
            point_shadow_payload[101] = point_shadow->depth_bias;
            point_shadow_payload[102] = static_cast<float>(point_shadow->light_index);
            point_shadow_payload[103] = point_shadow->far_plane;
        }
        gpu.write_buffer(m_point_shadow_ubo, point_shadow_payload.data(), point_shadow_ubo_size, 0);

        // Upload the spot shadow block: the light-space matrix plus
        // {enabled, bias, caster spot index}. enabled stays 0 with no
        // caster so the lit shader skips the (cleared) map.
        std::array<float, 20> spot_shadow_payload{};
        if (spot_shadow != nullptr && spot_shadow->active)
        {
            std::memcpy(
                spot_shadow_payload.data(), spot_shadow->light_view_projection.data(), sizeof(core::math::mat4));
            spot_shadow_payload[16] = 1.0f;
            spot_shadow_payload[17] = spot_shadow->depth_bias;
            spot_shadow_payload[18] = static_cast<float>(spot_shadow->light_index);
        }
        gpu.write_buffer(m_spot_shadow_ubo, spot_shadow_payload.data(), spot_shadow_ubo_size, 0);

        // Layer-filter, frustum-cull, then collect. A mesh draw whose
        // layer_mask shares no bit with the camera's culling mask is
        // skipped outright, the same as a frustum cull below it, so an
        // editor-only helper never reaches a gameplay camera that has
        // narrowed its mask. One with world bounds is then tested against
        // the camera frustum and skipped when it lies wholly outside; one
        // with no bounds (fullscreen effects, gizmos, skinned meshes) is
        // always collected. The frustum is the camera's unjittered one —
        // the TAA offset is a sub-pixel shift that no plane test could
        // tell apart. A survivor that has nothing to draw this frame (a
        // hidden helper, missing geometry) still counts as submitted.
        //
        // Every survivor's item gets a sort key from @ref make_sort_key
        // right after it is collected: the queue from the item's material
        // (opaque or transparent), the view-space depth to the draw's world
        // bounds centre (0 for a draw with no bounds), and the item's own
        // pipeline id. The single per-frame list is then sorted by that
        // key ascending, which — by construction of the key — sorts
        // opaque items front-to-back for early-Z rejection and transparent
        // ones back-to-front so blending composites correctly, with the
        // pipeline id as a further tie-break within equal depth; the
        // material instance breaks any remaining tie so a run of identical
        // keys still shares one bind-group rebind. The sort is stable, so
        // within equal keys submission order still applies.
        const core::math::frustum& view_frustum = ctx.active_camera->frustum;
        const core::math::mat4& view_matrix = ctx.active_camera->view;
        const uint32_t camera_mask = ctx.active_camera->culling_mask;
        uint32_t submitted = 0;
        uint32_t culled = 0;
        for (const mesh_draw& draw : ctx.scene_draws)
        {
            if ((draw.layer_mask & camera_mask) == 0)
            {
                ++culled;
                continue;
            }
            const bool has_bounds = draw.bounded;
            const core::math::aabb& bounds = draw.bounds;
            if (has_bounds && !view_frustum.intersects(bounds))
            {
                ++culled;
                continue;
            }
            ++submitted;
            const std::size_t first_item = m_items.size();
            if (draw.drawable)
            {
                m_items.push_back(draw.item);
            }

            float view_depth = 0.0f;
            if (has_bounds)
            {
                const core::math::vec3 centre = bounds.center();
                // View space looks down -z (the right-handed convention
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

        // Resolve, in list order, the pipeline each item binds in each
        // dispatch, so the dispatches — which may run on worker threads
        // — only read handles: a material's variant twins are looked up
        // or built on first use, which is main-thread work that belongs
        // here. An item is pre-passed when the pre-pass runs this frame
        // and its material takes part: the pre-pass draws exactly those
        // (the list is sorted, so the opaque ones front-to-back) with the
        // depth-only twin of the item's pipeline, and this pass shades
        // them with the twin that tests less-or-equal against that depth
        // without writing it. Everything else — the transparent queue,
        // surfaces that skip the depth test or write, templates that opt
        // out — is drawn by this pass alone with its ordinary pipeline.
        m_pipelines.reserve(m_items.size());
        for (std::size_t i = 0; i < m_items.size(); ++i)
        {
            const draw_item& item = m_items[i];
            item_pipelines pipelines{};
            if (m_depth_prepassed && item.mat->draws_in_depth_prepass())
            {
                pipelines.depth = item.mat->depth_prepass_pipeline(item.mirrored);
                pipelines.shading = item.mat->depth_prepassed_pipeline(item.mirrored);
                m_depth_item_end = i + 1;
            }
            else
            {
                pipelines.shading = item.mat->pipeline(item.mirrored);
            }
            m_pipelines.push_back(pipelines);
        }

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

    void scene_pass::record_depth_prepass(gpu::command_encoder& encoder,
                                          const gpu::render_pass_descriptor& descriptor) const
    {
        record_phase(encoder, descriptor, draw_phase::depth_prepass);
    }

    void scene_pass::record(gpu::command_encoder& encoder, const frame_context& /*ctx*/)
    {
        // Render into the HDR scene-colour target so the post chain
        // can sample real luminance. The tonemap post pass maps the
        // result onto the swapchain before the UI composites. The depth
        // is cleared unless the depth pre-pass laid the opaque queue's
        // depth into it this frame, in which case it is loaded and the
        // pre-passed items test against it without writing.
        gpu::render_pass_descriptor descriptor{};
        descriptor.target = m_target;
        descriptor.color[0].load = gpu::load_op::clear;
        descriptor.color[0].clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
        descriptor.use_depth = true;
        descriptor.depth.load = m_depth_prepassed ? gpu::load_op::load : gpu::load_op::clear;
        descriptor.depth.clear_depth = 1.0f;

        // No camera, no scene — but the pass is still opened so the HDR
        // target gets cleared to black. Otherwise the tonemap would map
        // stale or driver-uninitialised contents into the swapchain on
        // no-camera frames. (The depth pre-pass never runs without a
        // camera, so the depth is cleared here too.) prepare() left the
        // list empty, so the walk draws nothing.
        record_phase(encoder, descriptor, draw_phase::shading);
    }

    uint32_t scene_pass::plan_chunks(size_t draw_count) const
    {
        if (m_parallel_draw_threshold == 0 || draw_count <= m_parallel_draw_threshold)
        {
            return 1;
        }
        // One recording thread per chunk at most: the pool's workers plus
        // this thread, which helps while it waits on the fork. Without
        // workers there is nobody to hand a chunk to.
        const size_t lanes = m_jobs != nullptr ? static_cast<size_t>(m_jobs->worker_count()) + 1 : 1;
        if (lanes < 2)
        {
            return 1;
        }
        // Above the threshold at least two chunks, each of at least a
        // threshold's worth of draws, so the fork is always amortised.
        const size_t wanted = std::max<size_t>(draw_count / m_parallel_draw_threshold, 2);
        return static_cast<uint32_t>(std::min(wanted, lanes));
    }

    void scene_pass::record_phase(gpu::command_encoder& encoder,
                                  gpu::render_pass_descriptor descriptor,
                                  draw_phase phase) const
    {
        // The pre-pass walks only the prefix that holds pre-passed items;
        // the shading pass the whole list.
        const size_t count = phase == draw_phase::depth_prepass ? m_depth_item_end : m_items.size();
        const uint32_t chunks = plan_chunks(count);
        if (chunks <= 1)
        {
            auto pass_encoder = encoder.begin_render_pass(descriptor);
            dispatch(*pass_encoder, phase, 0, count);
            pass_encoder->end();
            return;
        }

        // Parallel: the pass takes its draws from one secondary encoder
        // per chunk. The secondaries are opened here, on the main thread,
        // before the fork (each takes its command buffer from the lane's
        // own pool), recorded and ended on whichever thread runs the
        // chunk, and executed in list order once every chunk has joined,
        // so the draws land as the serial walk would issue them. A
        // secondary the backend could not open is logged there and its
        // chunk goes undrawn this frame: a pass begun for secondaries
        // cannot take the draws inline.
        LOG_TRC("scene_pass: %s dispatch of %zu draws in %u chunks",
                phase == draw_phase::depth_prepass ? "depth pre-pass" : "shading",
                count,
                chunks);
        descriptor.parallel = true;
        auto pass_encoder = encoder.begin_render_pass(descriptor);
        std::vector<std::unique_ptr<gpu::render_pass_encoder>> secondaries;
        secondaries.reserve(chunks);
        for (uint32_t chunk = 0; chunk < chunks; ++chunk)
        {
            secondaries.push_back(pass_encoder->begin_secondary(chunk));
        }

        // More than one chunk means plan_chunks found the pool's workers.
        m_jobs->parallel_for(
            chunks,
            [&](size_t chunk)
            {
                gpu::render_pass_encoder* secondary = secondaries[chunk].get();
                if (secondary == nullptr)
                {
                    return;
                }
                // Contiguous, evenly cut ranges, so the chunk order is the
                // list order and every chunk gets the same share.
                const size_t first = count * chunk / chunks;
                const size_t last = count * (chunk + 1) / chunks;
                dispatch(*secondary, phase, first, last);
                secondary->end();
            },
            1);

        for (auto& secondary : secondaries)
        {
            if (secondary != nullptr)
            {
                pass_encoder->execute_secondary(*secondary);
            }
        }
        pass_encoder->end();
    }

    void scene_pass::dispatch(gpu::render_pass_encoder& pass_encoder, draw_phase phase, size_t first, size_t last) const
    {
        uint64_t last_pipeline_id = 0;
        const material* last_material = nullptr;
        bool first_iter = true;
        for (size_t i = first; i < last && i < m_items.size(); ++i)
        {
            const draw_item& item = m_items[i];
            // The pipelines prepare() resolved for the item: the pre-pass
            // skips an item without a depth-only twin (its material opts
            // out or it is not in the opaque queue).
            const gpu::pipeline pipeline =
                phase == draw_phase::depth_prepass ? m_pipelines[i].depth : m_pipelines[i].shading;
            if (!pipeline.valid())
            {
                continue;
            }

            if (pipeline.id != last_pipeline_id)
            {
                pass_encoder.set_pipeline(pipeline);

                // Per-frame bind group bound once per walk after the
                // first pipeline change; the binding sticks across
                // subsequent set_pipeline calls within the same encoder,
                // since every template's pipelines share the per-frame
                // layout and push-constant ranges. Every chunk of a
                // parallel walk binds it again: nothing carries into a
                // secondary.
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

            // Instanced draws keep their per-instance data in a vertex
            // stream (slot 1), not a PerDraw block, so the per-draw data is
            // optional. A mesh proxy's block is pushed, in the pre-pass as in
            // the shading pass, and a skinned one also binds its
            // joint-palette group.
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
                    // the indirect command record (see @ref mesh_draw_builder).
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
