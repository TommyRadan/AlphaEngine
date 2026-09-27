// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/materials/material.hpp>

#include <utility>

#include <rendering_engine/assets/color.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/gpu/texture.hpp>
#include <rendering_engine/materials/material_template.hpp>

namespace rendering_engine
{
    material::material(std::shared_ptr<material_template> tmpl, const material_params& params, uint32_t keywords)
        : m_template(std::move(tmpl)), m_params(params), m_keywords(keywords)
    {
        rebind_variant();
        m_template->register_instance(this);
    }

    material::~material()
    {
        // Instances are released while the device is live (before
        // rendering_engine::renderer::quit tears it down); the template
        // they share outlives them through the shared handle.
        m_template->unregister_instance(this);
        release_per_material_bind_group();
    }

    material_template& material::get_template() const
    {
        return *m_template;
    }

    gpu::pipeline material::pipeline() const
    {
        return m_pipeline;
    }

    gpu::pipeline material::pipeline(bool mirrored)
    {
        if (!mirrored)
        {
            return m_pipeline;
        }
        if (!m_mirrored_pipeline.valid())
        {
            m_mirrored_pipeline = m_template->pipeline(facing_key(true));
        }
        return m_mirrored_pipeline;
    }

    bool material::draws_in_depth_prepass() const
    {
        // The queue test is the scene pass's own (transparent picks the
        // transparent queue), and a transparent key never writes depth
        // anyway. A variant that skips the test or the write must not be
        // pre-passed either: depth it never writes in the scene pass
        // would otherwise start occluding other surfaces.
        return m_template->descriptor().depth_prepass && !m_params.transparent && m_key.depth_test && m_key.depth_write;
    }

    gpu::pipeline material::depth_prepass_pipeline(bool mirrored)
    {
        gpu::pipeline& twin = m_depth_prepass_pipelines[mirrored ? 1 : 0];
        if (!twin.valid())
        {
            twin = m_template->pipeline(depth_prepass_variant(facing_key(mirrored)));
        }
        return twin;
    }

    gpu::pipeline material::depth_prepassed_pipeline(bool mirrored)
    {
        gpu::pipeline& twin = m_depth_prepassed_pipelines[mirrored ? 1 : 0];
        if (!twin.valid())
        {
            twin = m_template->pipeline(depth_prepassed_variant(facing_key(mirrored)));
        }
        return twin;
    }

    const pipeline_variant_key& material::variant_key() const
    {
        return m_key;
    }

    const material_params& material::params() const
    {
        return m_params;
    }

    uint32_t material::keywords() const
    {
        return m_keywords;
    }

    void material::set_params(const material_params& params)
    {
        m_params = params;
        rebind_variant();
        on_params_changed();
    }

    void material::set_transparent(bool transparent)
    {
        material_params params = m_params;
        params.transparent = transparent;
        set_params(params);
    }

    void material::set_opacity(float opacity)
    {
        material_params params = m_params;
        params.opacity = opacity;
        set_params(params);
    }

    void material::set_double_sided(bool double_sided)
    {
        material_params params = m_params;
        params.double_sided = double_sided;
        set_params(params);
    }

    void material::set_blending(blend_mode blending)
    {
        material_params params = m_params;
        params.blending = blending;
        set_params(params);
    }

    void material::set_wireframe(bool wireframe)
    {
        material_params params = m_params;
        params.wireframe = wireframe;
        set_params(params);
    }

    void material::set_depth_test(bool depth_test)
    {
        material_params params = m_params;
        params.depth_test = depth_test;
        set_params(params);
    }

    void material::set_depth_write(bool depth_write)
    {
        material_params params = m_params;
        params.depth_write = depth_write;
        set_params(params);
    }

    void material::set_fog(bool fog)
    {
        material_params params = m_params;
        params.fog = fog;
        set_params(params);
    }

    gpu::bind_group_layout material::per_draw_layout() const
    {
        return m_template->per_draw_layout(m_key.keywords);
    }

    bool material::is_skinned() const
    {
        return m_template->skins(m_key.keywords);
    }

    uint32_t material::per_draw_slot() const
    {
        return m_template->per_draw_slot();
    }

    uint32_t material::per_material_slot() const
    {
        return m_template->per_material_slot();
    }

    gpu::bind_group material::per_material_bind_group() const
    {
        return m_per_material_bind_group;
    }

    vertex_format material::required_vertex_format() const
    {
        return m_template->required_vertex_format(m_key.keywords);
    }

    uint32_t material::min_vertex_stride() const
    {
        return m_template->min_vertex_stride(m_key.keywords);
    }

    gpu::device& material::device() const
    {
        return m_template->device();
    }

    void material::set_keyword(material_keyword keyword, bool enabled)
    {
        const uint32_t bit = keyword_bit(keyword);
        set_keywords(enabled ? (m_keywords | bit) : (m_keywords & ~bit));
    }

    void material::set_keywords(uint32_t keywords)
    {
        if (keywords == m_keywords)
        {
            return;
        }
        m_keywords = keywords;
        rebind_variant();
    }

    void material::release_per_material_bind_group()
    {
        if (m_per_material_bind_group.valid())
        {
            device().destroy(m_per_material_bind_group);
            m_per_material_bind_group = {};
        }
    }

    gpu::texture material::upload_map(const image& image, gpu::color_space space, gpu::address_mode address) const
    {
        auto& gpu = device();

        gpu::texture_descriptor descriptor{};
        descriptor.dimension = gpu::texture_dimension::d2;
        descriptor.format = gpu::rgba8_format(space);
        descriptor.width = image.get_width();
        descriptor.height = image.get_height();
        descriptor.mipmaps = true;
        descriptor.min_filter = gpu::filter_mode::linear;
        descriptor.mag_filter = gpu::filter_mode::linear;
        descriptor.address_u = address;
        descriptor.address_v = address;
        descriptor.address_w = address;
        gpu::texture map = gpu.create_texture(descriptor);

        const size_t pixel_bytes =
            static_cast<size_t>(image.get_width()) * static_cast<size_t>(image.get_height()) * sizeof(color);
        gpu.write_texture(map, image.get_pixels(), pixel_bytes);
        gpu.generate_mipmaps(map);
        return map;
    }

    void material::release_map(gpu::texture& map) const
    {
        if (!map.valid())
        {
            return;
        }
        device().destroy(map);
        map = {};
    }

    void material::rebind_variant()
    {
        const pipeline_variant_key key = make_pipeline_variant_key(m_params, m_keywords);
        if (key == m_key && m_pipeline.valid())
        {
            return;
        }
        m_key = key;
        m_pipeline = m_template->pipeline(key);
        // The clockwise and depth pre-pass twins belong to the old key;
        // the next draw that wants one resolves it again through the
        // cache.
        m_mirrored_pipeline = {};
        m_depth_prepass_pipelines = {};
        m_depth_prepassed_pipelines = {};
    }

    pipeline_variant_key material::facing_key(bool mirrored) const
    {
        pipeline_variant_key key = m_key;
        if (mirrored)
        {
            key.front = gpu::front_face::clockwise;
        }
        return key;
    }
} // namespace rendering_engine
