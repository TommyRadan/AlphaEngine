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

/**
 * @file environment.hpp
 * @brief Image-based-lighting source set built from a cube-map skybox.
 */

#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/util/image.hpp>

namespace rendering_engine
{
    // The pre-integrated image-based-lighting set derived from a single
    // cube-map background — the analogue of @c THREE.PMREMGenerator's
    // output that @c scene.environment feeds into a standard material.
    //
    // Constructing an @ref environment uploads the source cube map (HDR,
    // with a mip chain) and derives:
    //  - the @ref skybox cube, which the @ref skybox_pass samples as the
    //    background;
    //  - the @ref prefiltered specular cube, GGX-convolved per roughness
    //    (one roughness per mip level);
    //  - the @ref irradiance cube, a cosine-convolved diffuse table;
    //  - the @ref brdf_lut, the split-sum environment BRDF integration
    //    (a 2D scale/bias table independent of the environment).
    //
    // When the backend reports @c supports_compute_prefilter the three
    // derived tables are convolved on the GPU through compute shaders;
    // otherwise they are built on the CPU and the prefiltered specular
    // falls back to the source cube's box-filtered mip chain.
    //
    // All three are owned by the object and released in the destructor, so
    // an @ref environment must outlive every pass and material that binds
    // its textures. Build it once (the convolutions are not cheap) and keep
    // it alive for the scene's lifetime.
    struct environment
    {
        // Build from six HDR cube faces in @c gpu::cube_face order
        // (+X, -X, +Y, -Y, +Z, -Z). Each face is @p face_size *
        // @p face_size RGBA texels laid out row-major, four floats per
        // texel; the alpha channel is ignored. Linear (scene-referred)
        // radiance is expected — no sRGB decode is applied.
        environment(uint32_t face_size, const std::array<std::vector<float>, 6>& faces);

        // Build from six LDR face images in the same order. Each image's
        // sRGB texels are decoded to linear radiance before convolution,
        // so ordinary 8-bit skybox PNGs drop straight in.
        explicit environment(const std::array<util::image, 6>& faces);

        ~environment();

        environment(const environment&) = delete;
        environment& operator=(const environment&) = delete;

        // The source skybox cube map: HDR rgba16f with a full mip chain,
        // sampled by the skybox pass for the background.
        gpu::texture skybox() const;

        // The prefiltered specular cube map sampled by lit materials at
        // @c roughness * maxMip. On the GPU path this is a dedicated cube
        // whose every mip is GGX-convolved for its roughness; on the CPU
        // fallback it is the source cube's box-filtered mip chain.
        gpu::texture prefiltered() const;

        // The diffuse irradiance cube map: the source convolved against a
        // cosine-weighted hemisphere, sampled by the surface normal.
        gpu::texture irradiance() const;

        // The environment BRDF look-up table: a 2D rg table indexed by
        // (N·V, roughness) carrying the Fresnel scale and bias of the
        // split-sum approximation.
        gpu::texture brdf_lut() const;

    private:
        // Upload the source cube + mip chain, then derive the tables on
        // the GPU or the CPU depending on backend support.
        void build(uint32_t face_size, const std::array<std::vector<float>, 6>& faces);

        // Allocate @ref m_skybox and upload the six source faces + mips.
        void upload_source(uint32_t face_size, const std::array<std::vector<float>, 6>& faces);

        // GPU compute path: dispatch the irradiance, prefilter and BRDF
        // convolutions into freshly allocated storage textures.
        void build_derived_gpu(uint32_t face_size);

        // CPU fallback path: convolve the irradiance cube and integrate
        // the BRDF LUT on the host; prefiltered specular reuses the source
        // mip chain (see @ref prefiltered).
        void build_derived_cpu(uint32_t face_size, const std::array<std::vector<float>, 6>& faces);

        gpu::texture m_skybox{};
        gpu::texture m_prefiltered{};
        gpu::texture m_irradiance{};
        gpu::texture m_brdf_lut{};
    };
} // namespace rendering_engine
