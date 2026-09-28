// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <memory>

#include <assets/color.hpp>
#include <assets/image.hpp>
#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/gpu/types.hpp>
#include <rendering_engine/materials/material.hpp>
#include <rendering_engine/materials/material_template.hpp>

namespace rendering_engine
{
    // Built-in lit 3D scene material. Position+UV+normal vertex stream,
    // MVP transform in the vertex shader, and Blinn-Phong shading in
    // the fragment shader driven by the per-frame lights UBO the
    // @ref scene_pass uploads at slot 0, binding 2. The first actually
    // shaded built-in surface; cheaper than full PBR.
    //
    // Slot layout matches the other 3D materials: the per-frame group
    // at slot 0 (camera + lights, owned by the @ref scene_pass) and the
    // per-draw group at slot 1 (model + normal matrix, built by each
    // renderable), so renderables need no changes. The diffuse /
    // specular params and the optional diffuse map live in the
    // per-material group at slot 2 owned by each instance.
    struct phong_material : public material
    {
        // @p tmpl is the shared phong template (see @ref describe).
        explicit phong_material(std::shared_ptr<material_template> tmpl);
        ~phong_material() override;

        // The descriptor of the template every phong_material shares: the
        // material library builds it (material_library::create_template)
        // over the scene pass's per-frame set and registers it as
        // "phong"; material_library::create_material makes a phong_material
        // of it. Touches no gpu::device.
        static material_template_descriptor describe();

        // Base diffuse (Lambertian) colour. When a diffuse map is set
        // the sampled texel modulates this tint (white leaves it
        // unchanged). The alpha channel carries through to the output.
        void set_diffuse(const assets::color& color);

        // Specular highlight colour. White gives a neutral highlight;
        // tint it to colour the reflection.
        void set_specular(const assets::color& color);

        // Blinn-Phong specular exponent. Larger values yield a tighter,
        // sharper highlight.
        void set_shininess(float shininess);

        // Bind a diffuse texture; the fragment shader multiplies it into
        // the diffuse term. Replaces any previous map and rebuilds the
        // per-material bind group. @p space is the colour space the image
        // was authored in: diffuse art is sRGB (the default), uploaded as
        // @c rgba8_srgb so the sampler decodes it to linear before the
        // lighting math; pass @c linear only for already-linear data.
        void set_diffuse_map(const assets::image& image, assets::color_space space = assets::color_space::srgb);

        // Drop the diffuse texture; the material falls back to the flat
        // diffuse tint. No-op when no map is set.
        void clear_diffuse_map();

    private:
        // (Re)create the per-material bind group against the current
        // diffuse-map handle, then push the latest params into the UBO.
        void rebuild_bind_group();

        // Push {diffuse, specular, shininess, useTexture} into the
        // per-material UBO.
        void upload_params();

        assets::color m_diffuse{255, 255, 255, 255};
        assets::color m_specular{255, 255, 255, 255};
        float m_shininess{32.0f};
        gpu::buffer m_material_ubo{};
        gpu::texture m_diffuse_map{};
    };
} // namespace rendering_engine
