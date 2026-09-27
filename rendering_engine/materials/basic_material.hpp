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
    // Built-in unlit 3D scene material. Position+UV vertex stream, MVP
    // transform in the vertex shader, fragment colour is a flat base
    // tint optionally modulated by an albedo texture.
    //
    // Slot layout: the per-frame group at slot 0 (camera + lights,
    // owned by the @ref scene_pass) and the per-draw group at slot 1
    // (model + normal matrix, built by each renderable) match the other
    // built-in 3D materials, so renderables need no changes. The tint
    // and texture live in the per-material group at slot 2 owned by
    // each instance — every renderable that fronts it shares them.
    struct basic_material : public material
    {
        // @p tmpl is the shared basic template (see @ref create_template).
        explicit basic_material(std::shared_ptr<material_template> tmpl);
        ~basic_material() override;

        // The template every basic_material shares. @p frame_layout is
        // the per-frame bind-group layout owned by the @ref scene_pass;
        // it must match the layout the pass binds at slot 0 every frame
        // so the pipelines and the runtime bind group agree on slot shape.
        static std::shared_ptr<material_template> create_template(gpu::device& device,
                                                                  gpu::bind_group_layout frame_layout);

        // Base colour tint. When an albedo texture is set the sampled
        // texel is multiplied by this tint (white leaves it unchanged).
        void set_color(const color& color);

        // Bind an albedo texture; the fragment shader switches to
        // sampling it (modulated by the tint). Replaces any previous
        // texture and rebuilds the per-material bind group. @p space is
        // the colour space the image was authored in: albedo art is sRGB
        // (the default), uploaded as @c rgba8_srgb so the sampler hands
        // the shader linear values; pass @c linear only for an image
        // whose bytes are already linear.
        void set_albedo(const image& image, gpu::color_space space = gpu::color_space::srgb);

        // Drop the albedo texture; the material falls back to the flat
        // tint. No-op when no texture is set.
        void clear_albedo();

    private:
        // (Re)create the per-material bind group against the current
        // albedo texture handle, then push the latest tint / texture
        // flag into the UBO.
        void rebuild_bind_group();

        // Push {color, useTexture} into the per-material UBO.
        void upload_params();

        color m_color{255, 255, 255, 255};
        gpu::buffer m_material_ubo{};
        gpu::texture m_albedo{};
    };
} // namespace rendering_engine
