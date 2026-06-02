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

#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/materials/material.hpp>
#include <rendering_engine/util/color.hpp>
#include <rendering_engine/util/image.hpp>

namespace rendering_engine
{
    struct environment;

    // Built-in physically-based lit 3D scene material
    // (metallic-roughness workflow). Cook-
    // Torrance specular (GGX distribution, Smith geometry, Schlick-
    // Fresnel) plus a Lambertian diffuse, driven by the per-frame lights
    // UBO the @ref scene_pass uploads at slot 0, binding 2. The modern
    // default surface: more expensive than @ref phong_material but the
    // single biggest step toward physically based visual fidelity.
    //
    // Consumes the position+uv+normal+tangent vertex stream (the tangent
    // feeds the normal-map TBN basis). Slot layout matches the other 3D
    // materials: the per-frame group at slot 0 (camera + lights, owned by
    // the @ref scene_pass) and the per-draw group at slot 1 (@c
    // modelMatrix, built by each renderable). The PBR scalars and the
    // optional albedo / normal / metalness / roughness / emissive maps
    // live in the per-material group at slot 2 owned by this material.
    //
    // Ambient comes from image-based lighting when an @ref environment is
    // attached (diffuse irradiance + prefiltered specular weighted by the
    // split-sum BRDF LUT); otherwise it falls back to the flat
    // @c ambient * albedo term driven by the per-frame ambient light.
    struct standard_material : public material
    {
        // @p frame_layout is the per-frame bind-group layout owned by
        // the @ref scene_pass; it must match the layout the pass binds
        // at slot 0 every frame so the pipeline and the runtime bind
        // group agree on slot shape.
        explicit standard_material(gpu::bind_group_layout frame_layout);
        ~standard_material() override;

        // Base (albedo) colour. When an albedo map is set the sampled
        // texel modulates this tint (white leaves it unchanged). The
        // alpha channel carries through to the output.
        void set_base_color(const util::color& color);

        // Metalness in [0, 1]. 0 is a dielectric (plastic, wood), 1 is a
        // raw metal whose diffuse term vanishes and whose specular tints
        // toward the base colour.
        void set_metalness(float metalness);

        // Perceptual roughness in [0, 1]. 0 is a mirror-smooth surface,
        // 1 is fully diffuse.
        void set_roughness(float roughness);

        // Emissive colour added after shading (unaffected by lights).
        // Black (the default) emits nothing.
        void set_emissive(const util::color& color);

        // Scalar multiplier on the emissive colour.
        void set_emissive_intensity(float intensity);

        // Bind an albedo (base-colour) texture; multiplied into the base
        // colour. Replaces any previous map and rebuilds the per-material
        // bind group.
        void set_albedo_map(const util::image& image);
        void clear_albedo_map();

        // Bind a tangent-space normal map; perturbs the shading normal
        // through the per-vertex TBN basis. Requires tangents on the
        // vertex stream (this material's layout supplies them).
        void set_normal_map(const util::image& image);
        void clear_normal_map();

        // Bind a metalness map; its red channel multiplies the metalness
        // scalar.
        void set_metalness_map(const util::image& image);
        void clear_metalness_map();

        // Bind a roughness map; its red channel multiplies the roughness
        // scalar.
        void set_roughness_map(const util::image& image);
        void clear_roughness_map();

        // Bind an emissive map; multiplied into the emissive term.
        void set_emissive_map(const util::image& image);
        void clear_emissive_map();

        // Attach an image-based-lighting environment. Its irradiance,
        // prefiltered specular (the skybox mip chain) and BRDF LUT replace
        // the flat ambient term. The @ref environment must outlive the
        // material (or be cleared first); only the texture handles are
        // referenced, not owned.
        void set_environment(const environment& env);

        // Detach the environment and revert to the flat ambient term.
        void clear_environment();

        // The per-material group trails the per-frame and per-draw
        // groups, so it occupies slot 2 (per-frame at 0, per-draw at 1).
        uint32_t per_material_slot() const override;

    private:
        // (Re)create the per-material bind group against the current map
        // handles, then push the latest params into the UBO.
        void rebuild_bind_group();

        // Push the PBR scalars + per-map enable flags into the per-
        // material UBO.
        void upload_params();

        // Upload an RGBA8 image to a fresh mipmapped, repeat-addressed
        // 2D texture. Shared by every set_*_map.
        gpu::texture upload_map(const util::image& image);

        // Destroy @p map if valid and null it. Shared by every clear_*_map
        // and set_*_map (which replaces the previous map).
        void release_map(gpu::texture& map);

        util::color m_base_color{255, 255, 255, 255};
        util::color m_emissive{0, 0, 0, 255};
        float m_metalness{0.0f};
        float m_roughness{1.0f};
        float m_emissive_intensity{1.0f};

        gpu::buffer m_material_ubo{};
        gpu::texture m_albedo_map{};
        gpu::texture m_normal_map{};
        gpu::texture m_metalness_map{};
        gpu::texture m_roughness_map{};
        gpu::texture m_emissive_map{};

        // Image-based-lighting source, or null for the flat ambient
        // fallback. Non-owning: the caller keeps the @ref environment
        // alive. Only its texture handles are bound into the per-material
        // group; the @c iblParams flag in the UBO gates the shader path.
        const environment* m_environment{nullptr};
    };
} // namespace rendering_engine
