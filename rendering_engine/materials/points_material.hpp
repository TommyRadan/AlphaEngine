// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <memory>

#include <rendering_engine/assets/color.hpp>
#include <rendering_engine/assets/image.hpp>
#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/gpu/types.hpp>
#include <rendering_engine/materials/material.hpp>
#include <rendering_engine/materials/material_template.hpp>

namespace rendering_engine
{
    // Built-in unlit point-cloud material. Its pipeline bakes
    // @c primitive_topology::points, so a @ref points renderable that
    // fronts it rasterizes one GL point sprite per vertex.
    //
    // Vertex stream: position (vec3) + per-point colour (vec3). The
    // fragment colour is the per-point colour modulated by the shared
    // tint and, optionally, a sprite texture sampled with
    // @c gl_PointCoord (round sprites, glyphs, particles).
    //
    // Slot layout mirrors @ref basic_material: the per-frame group at
    // slot 0 (camera, owned by the @ref scene_pass), the per-draw group
    // at slot 1 (model + normal matrix, built by the renderable), and
    // the tint / size / sprite in the per-material group at slot 2
    // owned by each instance.
    struct points_material : public material
    {
        // @p tmpl is the shared points template (see @ref create_template).
        explicit points_material(std::shared_ptr<material_template> tmpl);
        ~points_material() override;

        // The template every points_material shares. @p frame_layout is
        // the per-frame bind-group layout owned by the @ref scene_pass;
        // it must match the layout the pass binds at slot 0 every frame
        // so the pipelines and the runtime bind group agree on slot shape.
        static std::shared_ptr<material_template> create_template(gpu::device& device,
                                                                  gpu::bind_group_layout frame_layout);

        // Tint multiplied into every point's colour (white leaves the
        // per-point colour unchanged). Alpha participates when the
        // material is transparent.
        void set_color(const color& color);

        // Point size. When @ref set_size_attenuation is off this is the
        // sprite diameter in pixels; when on it is the size at one unit
        // of view-space depth and shrinks with distance.
        void set_size(float size);

        // Toggle perspective size attenuation. Off: constant pixel size
        // (size attenuation disabled). On: points shrink with view-space distance.
        void set_size_attenuation(bool enabled);

        // Bind a sprite texture; the fragment shader samples it at
        // @c gl_PointCoord and modulates the point colour by it.
        // Replaces any previous sprite and rebuilds the per-material
        // bind group. @p space is the colour space the sprite was
        // authored in: sRGB by default (uploaded as @c rgba8_srgb so it
        // modulates the linear point colour correctly); pass @c linear
        // for an already-linear mask.
        void set_sprite(const image& image, gpu::color_space space = gpu::color_space::srgb);

        // Drop the sprite texture; points fall back to flat square
        // sprites tinted by their colour. No-op when no sprite is set.
        void clear_sprite();

    private:
        // (Re)create the per-material bind group against the current
        // sprite texture handle, then push the latest params into the
        // UBO.
        void rebuild_bind_group();

        // Push {color, size, sizeAttenuation, useTexture} into the
        // per-material UBO.
        void upload_params();

        color m_color{255, 255, 255, 255};
        float m_size{4.0f};
        bool m_size_attenuation{false};
        gpu::buffer m_material_ubo{};
        gpu::texture m_sprite{};
    };
} // namespace rendering_engine
