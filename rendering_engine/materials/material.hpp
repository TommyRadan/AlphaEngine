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

#include <array>
#include <cstdint>
#include <memory>

#include <rendering_engine/assets/image.hpp>
#include <rendering_engine/assets/vertex.hpp>
#include <rendering_engine/gpu/bind_group.hpp>
#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/gpu/types.hpp>
#include <rendering_engine/materials/pipeline_variant.hpp>

namespace rendering_engine
{
    namespace gpu
    {
        struct device;
    }

    struct material_template;

    // A material *instance*: the thing a @ref draw_item points at. It
    // owns the per-instance state — the shared @ref material_params
    // surface, the keyword set its maps imply, its parameter UBO and
    // the per-material bind group — and shares everything else
    // (shaders, layouts, the pipeline variants) with every other
    // instance of its @ref material_template.
    //
    // The instance is bound to one variant of the template at a time:
    // the pipeline for the key @ref make_pipeline_variant_key derives
    // from its params and keywords. Every setter that changes the key
    // rebinds through the template's cache, so a parameter change is a
    // hash lookup (or, the first time a key is seen anywhere, one
    // pipeline build) rather than a new material.
    //
    // A draw whose model matrix mirrors (negative determinant) reverses
    // every triangle's winding; the passes ask for @ref pipeline(true)
    // for such items, which is the same variant with the front face
    // flipped to clockwise, resolved lazily on first use. The depth
    // pre-pass twins (@ref depth_prepass_pipeline and
    // @ref depth_prepassed_pipeline) are resolved the same way.
    struct material
    {
        virtual ~material();

        material(const material&) = delete;
        material& operator=(const material&) = delete;

        material_template& get_template() const;

        // The pipeline bound for the current params and keywords, with
        // the engine's counter-clockwise front face.
        gpu::pipeline pipeline() const;

        // The pipeline for a draw whose model matrix does (@p mirrored)
        // or does not flip handedness. The mirrored variant is looked
        // up or built on first request and cached until the key changes.
        gpu::pipeline pipeline(bool mirrored);

        // Whether the depth pre-pass lays this surface's depth down ahead
        // of the scene pass: the bound variant is in the opaque queue
        // (not @c transparent, so unblended), depth-tested and
        // depth-writing, and the template allows it
        // (@ref material_template_descriptor::depth_prepass). Anything
        // else keeps drawing with @ref pipeline in the scene pass alone.
        bool draws_in_depth_prepass() const;

        // The depth-only twin of @ref pipeline(bool) the depth pre-pass
        // draws this instance with (@ref depth_prepass_variant): the same
        // vertex module and rasterizer state, no fragment stage. Looked
        // up or built on first request and cached until the key changes,
        // like the mirrored variant.
        gpu::pipeline depth_prepass_pipeline(bool mirrored);

        // The twin of @ref pipeline(bool) the scene pass draws this
        // instance with once the depth pre-pass has laid it down
        // (@ref depth_prepassed_variant): depth writes off, compared with
        // @ref depth_prepassed_compare. Cached like the other twins.
        gpu::pipeline depth_prepassed_pipeline(bool mirrored);

        // The variant key the instance is currently bound to.
        const pipeline_variant_key& variant_key() const;

        // The shared base parameters, and the keyword set the instance
        // itself contributes (its bound maps, the tangent flag). The
        // keywords the shaders were actually compiled with are
        // @c variant_key().keywords, which also carries the bits derived
        // from the params (@c WIREFRAME, @c NO_FOG).
        const material_params& params() const;
        uint32_t keywords() const;

        // Replace the whole parameter surface. Rebinds the pipeline when
        // the resulting key differs from the current one; always lets the
        // subclass push the parameters its shader reads (opacity).
        void set_params(const material_params& params);

        // Field-wise setters over @ref set_params.
        void set_transparent(bool transparent);
        void set_opacity(float opacity);
        void set_double_sided(bool double_sided);
        void set_blending(blend_mode blending);
        void set_wireframe(bool wireframe);
        void set_depth_test(bool depth_test);
        void set_depth_write(bool depth_write);

        // Whether the scene fog blends over this surface (on by default;
        // three.js Material.fog). Off, a lit material rebinds to the
        // @c NO_FOG variant of its template, which carries no fog code.
        void set_fog(bool fog);

        // Layout renderables build their per-draw bind group
        // against (model matrix, per-draw textures, options). Follows the
        // bound variant: a skinning variant's layout also carries the
        // joint-matrix storage buffer.
        gpu::bind_group_layout per_draw_layout() const;

        // Whether the bound variant skins its vertices (the SKINNED keyword
        // on a template that supports it): a renderable drawing with it
        // must bind a joint palette in its per-draw group and feed the
        // skinned vertex record.
        bool is_skinned() const;

        // Slot index used for the per-draw bind group: 1 when the
        // template reserves slot 0 for a per-frame bind group, else 0.
        uint32_t per_draw_slot() const;

        // Slot index for the per-material bind group: the one after the
        // per-draw slot. Meaningful only when @ref per_material_bind_group
        // is valid.
        uint32_t per_material_slot() const;

        // Optional per-material bind group; invalid when the template
        // declares no per-material resources. The dispatch loop binds it
        // whenever the material changes between draws.
        gpu::bind_group per_material_bind_group() const;

        // The vertex record layout the bound variant reads from vertex
        // slot 0. Renderables check the mesh they draw against it (see
        // @ref vertex_format_compatible) before emitting a @c draw_item;
        // @c custom means the template declared none and only the
        // stride can be checked.
        vertex_format required_vertex_format() const;

        // Smallest vertex stride slot 0 can be bound with: the byte
        // extent of the bound variant's furthest-reaching slot-0
        // attribute.
        uint32_t min_vertex_stride() const;

    protected:
        // Binds the instance to @p tmpl's variant for @p params and
        // @p keywords (built on demand). The template must outlive the
        // instance's use; the shared handle keeps it alive.
        material(std::shared_ptr<material_template> tmpl, const material_params& params, uint32_t keywords = 0);

        gpu::device& device() const;

        // Set or clear one keyword (or replace the whole set) and rebind
        // the variant if that changed the key.
        void set_keyword(material_keyword keyword, bool enabled);
        void set_keywords(uint32_t keywords);

        // Hook for subclasses whose parameter block mirrors part of the
        // base params (opacity): called after every params change, once
        // the variant has been rebound.
        virtual void on_params_changed() {}

        // Destroy @ref m_per_material_bind_group if valid and null it.
        void release_per_material_bind_group();

        // Upload an RGBA8 image to a fresh mipmapped, linearly filtered
        // 2D texture in the RGBA8 format for @p space, addressed with
        // @p address on every axis. Shared by every set_*_map.
        gpu::texture upload_map(const image& image,
                                gpu::color_space space,
                                gpu::address_mode address = gpu::address_mode::repeat) const;

        // Destroy @p map if valid and null it. Shared by every
        // clear_*_map and set_*_map (which replaces the previous map).
        void release_map(gpu::texture& map) const;

        std::shared_ptr<material_template> m_template;
        material_params m_params{};
        uint32_t m_keywords{0};

        // Built by subclasses against the template's per-material layout.
        gpu::bind_group m_per_material_bind_group{};

    private:
        // Recompute the key from params + keywords and refresh the bound
        // pipeline; the mirrored and depth pre-pass twins are dropped and
        // resolved lazily.
        void rebind_variant();

        // The bound key, with the front face flipped to clockwise for a
        // @p mirrored draw.
        pipeline_variant_key facing_key(bool mirrored) const;

        pipeline_variant_key m_key{};
        gpu::pipeline m_pipeline{};
        gpu::pipeline m_mirrored_pipeline{};

        // The depth pre-pass twins of the bound variant, indexed by
        // mirroring (0 counter-clockwise, 1 clockwise front face);
        // invalid until first asked for.
        std::array<gpu::pipeline, 2> m_depth_prepass_pipelines{};
        std::array<gpu::pipeline, 2> m_depth_prepassed_pipelines{};
    };
} // namespace rendering_engine
