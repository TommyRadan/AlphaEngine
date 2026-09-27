// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/gpu/backend/opengl/gl_command_encoder.hpp>

#include <algorithm>
#include <array>
#include <cstdint>

#include <core/log.hpp>
#include <rendering_engine/gpu/backend/opengl/gl_check.hpp>
#include <rendering_engine/gpu/backend/opengl/gl_device.hpp>
#include <rendering_engine/gpu/backend/opengl/gl_state_cache.hpp>
#include <rendering_engine/gpu/backend/opengl/gl_translate.hpp>

namespace rendering_engine::gpu::backend::opengl
{
    namespace
    {
        const bind_group_layout_entry* find_layout_entry(const bind_group_layout_descriptor& descriptor,
                                                         uint32_t binding)
        {
            for (const auto& entry : descriptor.entries)
            {
                if (entry.binding == binding)
                {
                    return &entry;
                }
            }
            return nullptr;
        }

        // Tell the driver the named planes of @p target hold nothing
        // worth keeping: at pass begin for load_op::dont_care (no
        // load, no clear) and at pass end for store_op::dont_care (the
        // results are never read back). @p color flags each of the
        // target's @p color_count colour attachments. A tile-based GPU
        // skips the load / store traffic; a desktop driver treats it as
        // a hint.
        void invalidate_attachments(const gl_render_target& target,
                                    const std::array<bool, max_color_attachments>& color,
                                    uint32_t color_count,
                                    bool depth)
        {
            std::array<GLenum, max_color_attachments + 2> attachments{};
            GLsizei count = 0;
            const bool default_framebuffer = target.framebuffer_id == 0;
            for (uint32_t i = 0; i < color_count && i < max_color_attachments; ++i)
            {
                if (color[i])
                {
                    attachments[count++] = default_framebuffer ? GL_COLOR : GL_COLOR_ATTACHMENT0 + i;
                }
            }
            if (depth && target.has_depth)
            {
                attachments[count++] = default_framebuffer ? GL_DEPTH : GL_DEPTH_ATTACHMENT;
                if (target.has_stencil)
                {
                    attachments[count++] = default_framebuffer ? GL_STENCIL : GL_STENCIL_ATTACHMENT;
                }
            }
            if (count > 0)
            {
                glInvalidateNamedFramebufferData(target.framebuffer_id, count, attachments.data());
            }
        }

        // Drop a pipeline's vertex-array binding shadows once the
        // device's cache epoch has moved on (a pass boundary or an
        // object deletion since they were recorded).
        void sync_binding_shadows(const gl_device& device, gl_pipeline& pipe)
        {
            if (pipe.shadow_epoch == device.state_epoch())
            {
                return;
            }
            for (auto& shadow : pipe.vertex_binding_shadows)
            {
                shadow.known = false;
            }
            pipe.element_buffer_known = false;
            pipe.shadow_epoch = device.state_epoch();
        }

        // The byte range @p value exposes of @p buf once @p dynamic_offset
        // is added: @p offset / @p size for glBindBufferRange, or a size
        // of 0 for a whole-buffer glBindBufferBase. False, with the
        // problem logged, when the range leaves the buffer.
        bool resolve_buffer_range(
            const binding_value& value, const gl_buffer& buf, size_t dynamic_offset, GLintptr& offset, GLsizeiptr& size)
        {
            const size_t start = value.offset + dynamic_offset;
            if (start == 0 && value.size == 0)
            {
                offset = 0;
                size = 0;
                return true;
            }
            const size_t length = value.size != 0 ? value.size : (start < buf.size ? buf.size - start : 0);
            if (start > buf.size || length == 0 || length > buf.size - start)
            {
                LOG_WRN("set_bind_group: binding %u range of %zu bytes at offset %zu leaves the %zu-byte buffer",
                        value.binding,
                        length,
                        start,
                        buf.size);
                return false;
            }
            offset = static_cast<GLintptr>(start);
            size = static_cast<GLsizeiptr>(length);
            return true;
        }

        // True when @p dynamic_offsets carries exactly one offset per
        // dynamic slot of @p bg. A mismatch is a call-site bug (Vulkan
        // rejects the bind outright), so the group is not bound and the
        // problem is logged once per group rather than once per draw.
        bool dynamic_offsets_match(gl_bind_group& bg, std::span<const uint32_t> dynamic_offsets, uint32_t group)
        {
            if (dynamic_offsets.size() == bg.dynamic_count)
            {
                return true;
            }
            if (!bg.offset_mismatch_reported)
            {
                bg.offset_mismatch_reported = true;
                LOG_ERR("set_bind_group: slot %u takes %u dynamic offsets, %zu given; the group is not bound",
                        group,
                        bg.dynamic_count,
                        dynamic_offsets.size());
            }
            return false;
        }

        // Bind every entry of @p bg. @p dynamic_offsets has been checked
        // against @c gl_bind_group::dynamic_count by the caller.
        void apply_bind_group(gl_device& device, const gl_bind_group& bg, std::span<const uint32_t> dynamic_offsets)
        {
            // Bindings come straight from the SPIR-V @c Binding
            // decoration. UBO / SSBO / texture / image binding
            // namespaces are disjoint in OpenGL, so the same numeric
            // binding can appear on entries of different kinds
            // without collision.
            auto& cache = device.state_cache();
            const auto* layout = device.lookup_bind_group_layout(bg.layout);

            // Units a sampler entry of this group claims keep that
            // sampler; every other texture unit this group touches
            // falls back to the state baked onto the texture (sampler
            // object 0), so a sampler left on the unit by an earlier
            // group or pass never leaks into this draw. A standalone
            // sampler therefore belongs in the same bind group as the
            // texture it samples.
            uint64_t sampler_units = 0;
            for (const auto& value : bg.entries)
            {
                if (value.kind == binding_kind::sampler && value.binding < 64)
                {
                    sampler_units |= uint64_t{1} << value.binding;
                }
            }

            for (size_t i = 0; i < bg.entries.size(); ++i)
            {
                const binding_value& value = bg.entries[i];
                switch (value.kind)
                {
                case binding_kind::uniform_buffer:
                {
                    auto* buf = device.lookup_buffer(value.buffer_value);
                    if (buf == nullptr || buf->object_id == 0)
                    {
                        break;
                    }
                    // A dynamic slot adds its bind-time offset to the
                    // range the group was written with.
                    const uint32_t dynamic =
                        i < bg.dynamic_index.size() ? bg.dynamic_index[i] : gl_bind_group::no_dynamic_offset;
                    const size_t dynamic_offset =
                        dynamic < dynamic_offsets.size() ? static_cast<size_t>(dynamic_offsets[dynamic]) : 0u;
                    GLintptr offset = 0;
                    GLsizeiptr size = 0;
                    if (resolve_buffer_range(value, *buf, dynamic_offset, offset, size))
                    {
                        cache.bind_uniform_buffer(value.binding, buf->object_id, offset, size);
                    }
                    break;
                }
                case binding_kind::storage_buffer:
                {
                    auto* buf = device.lookup_buffer(value.buffer_value);
                    if (buf == nullptr || buf->object_id == 0)
                    {
                        break;
                    }
                    GLintptr offset = 0;
                    GLsizeiptr size = 0;
                    if (resolve_buffer_range(value, *buf, 0, offset, size))
                    {
                        cache.bind_storage_buffer(value.binding, buf->object_id, offset, size);
                    }
                    break;
                }
                case binding_kind::texture:
                {
                    auto* tex = device.lookup_texture(value.texture_value);
                    if (tex != nullptr && tex->object_id != 0)
                    {
                        cache.bind_texture_unit(value.binding, tex->object_id);
                        const bool has_sampler =
                            value.binding < 64 && (sampler_units & (uint64_t{1} << value.binding)) != 0;
                        if (!has_sampler)
                        {
                            cache.bind_sampler(value.binding, 0);
                        }
                    }
                    break;
                }
                case binding_kind::storage_texture:
                {
                    auto* tex = device.lookup_texture(value.texture_value);
                    if (tex == nullptr || tex->object_id == 0 || layout == nullptr)
                    {
                        break;
                    }
                    const auto* entry = find_layout_entry(layout->descriptor, value.binding);
                    if (entry == nullptr)
                    {
                        LOG_WRN("set_bind_group: storage_texture has no matching layout entry");
                        break;
                    }
                    const auto fmt = to_gl_texture_format(entry->storage_format);
                    const GLboolean layered = (tex->target == GL_TEXTURE_3D || tex->layered) ? GL_TRUE : GL_FALSE;
                    // Bind the requested mip level; layered binds every
                    // cube face / array layer / volume slice so a compute
                    // shader writes the whole level through an imageCube
                    // / image2DArray / image3D.
                    glBindImageTexture(value.binding,
                                       tex->object_id,
                                       static_cast<GLint>(value.storage_level),
                                       layered,
                                       0,
                                       to_gl_storage_access(entry->storage_access_mode),
                                       fmt.internal_format);
                    break;
                }
                case binding_kind::sampler:
                {
                    auto* samp = device.lookup_sampler(value.sampler_value);
                    if (samp != nullptr && samp->object_id != 0)
                    {
                        // The sampler object overrides the texture-baked
                        // state on this unit for the draws that follow.
                        cache.bind_sampler(value.binding, samp->object_id);
                    }
                    break;
                }
                }
            }
        }

        // The blend state pipeline @p pipe applies to colour attachment
        // @p index: its override for that attachment, else its default.
        const blend_state& blend_for_attachment(const gl_pipeline& pipe, uint32_t index)
        {
            return index < pipe.attachment_blend.size() ? pipe.attachment_blend[index] : pipe.blend;
        }

        void apply_stencil_face(gl_state_cache& cache,
                                GLenum face,
                                const stencil_face_state& state,
                                uint32_t reference,
                                const stencil_state& stencil)
        {
            cache.set_stencil_func(
                face, to_gl_compare(state.compare), static_cast<GLint>(reference), stencil.read_mask);
            cache.set_stencil_op(face,
                                 to_gl_stencil_op(state.fail_op),
                                 to_gl_stencil_op(state.depth_fail_op),
                                 to_gl_stencil_op(state.pass_op));
            cache.set_stencil_mask(face, stencil.write_mask);
        }

        // Every piece of fixed-function state a pipeline bakes, applied
        // for a pass over a target with @p color_count colour
        // attachments. Depth and stencil follow the pass's use_depth
        // (a pass without depth neither tests nor writes it, whatever
        // the pipeline asked for — the target may carry a plane another
        // pass owns) and the target's stencil plane.
        void apply_pipeline_state(gl_state_cache& cache,
                                  const gl_pipeline& pipe,
                                  bool use_depth,
                                  bool has_stencil,
                                  uint32_t color_count,
                                  uint32_t stencil_reference)
        {
            for (uint32_t i = 0; i < color_count && i < max_color_attachments; ++i)
            {
                const blend_state& blend = blend_for_attachment(pipe, i);
                cache.set_blend(i,
                                blend.enabled,
                                to_gl_blend_factor(blend.src),
                                to_gl_blend_factor(blend.dst),
                                to_gl_blend_op(blend.op));
                cache.set_color_mask(i,
                                     (blend.write_mask & color_write_red) != 0u ? GL_TRUE : GL_FALSE,
                                     (blend.write_mask & color_write_green) != 0u ? GL_TRUE : GL_FALSE,
                                     (blend.write_mask & color_write_blue) != 0u ? GL_TRUE : GL_FALSE,
                                     (blend.write_mask & color_write_alpha) != 0u ? GL_TRUE : GL_FALSE);
            }

            if (use_depth)
            {
                cache.set_depth_test(pipe.depth.test_enabled);
                cache.set_depth_write(pipe.depth.write_enabled);
                cache.set_depth_func(to_gl_compare(pipe.depth.compare));
            }
            else
            {
                cache.set_depth_test(false);
                cache.set_depth_write(false);
            }

            if (use_depth && has_stencil && pipe.stencil.test_enabled)
            {
                cache.set_stencil_test(true);
                apply_stencil_face(cache, GL_FRONT, pipe.stencil.front, stencil_reference, pipe.stencil);
                apply_stencil_face(cache, GL_BACK, pipe.stencil.back, stencil_reference, pipe.stencil);
            }
            else
            {
                cache.set_stencil_test(false);
            }

            // glPolygonOffset takes (factor, units): the slope scale
            // first, then the constant in resolvable depth steps.
            cache.set_polygon_offset(
                pipe.depth_bias.enabled, pipe.depth_bias.slope, pipe.depth_bias.constant, pipe.depth_bias.clamp);

            cache.set_cull(pipe.rasterizer.cull != cull_mode::none, to_gl_cull_face(pipe.rasterizer.cull));
            cache.set_front_face(to_gl_front_face(pipe.rasterizer.front));
            cache.set_polygon_mode(to_gl_polygon_mode(pipe.rasterizer.polygon));
        }

        // True when @p region lies inside level / layer of @p record and
        // @p size bytes from @p offset stay inside @p buffer; logs the
        // first problem otherwise.
        bool copy_region_fits(const char* where,
                              const gl_texture& record,
                              const texture_copy_region& region,
                              const gl_buffer& buffer,
                              size_t offset)
        {
            if (record.samples > 1)
            {
                LOG_WRN("%s: a multisampled texture cannot be copied", where);
                return false;
            }
            if (region.mip_level >= record.mip_levels || region.layer >= record.array_layers)
            {
                LOG_WRN("%s: level %u / layer %u is outside the texture (%u levels, %u layers)",
                        where,
                        region.mip_level,
                        region.layer,
                        record.mip_levels,
                        record.array_layers);
                return false;
            }
            const uint32_t level_width = std::max(1u, record.width >> region.mip_level);
            const uint32_t level_height = std::max(1u, record.height >> region.mip_level);
            const uint32_t level_depth = std::max(1u, record.depth >> region.mip_level);
            if (region.width == 0 || region.height == 0 || region.depth == 0 ||
                static_cast<uint64_t>(region.x) + region.width > level_width ||
                static_cast<uint64_t>(region.y) + region.height > level_height ||
                static_cast<uint64_t>(region.z) + region.depth > level_depth)
            {
                LOG_WRN("%s: region does not fit level %u", where, region.mip_level);
                return false;
            }
            const size_t bytes = texture_region_bytes(record.format, region);
            if (offset > buffer.size || bytes > buffer.size - offset)
            {
                LOG_WRN("%s: %zu bytes at offset %zu exceed the %zu-byte buffer", where, bytes, offset, buffer.size);
                return false;
            }
            return true;
        }
    } // namespace

    // -- gl_render_pass_encoder ---------------------------------------

    gl_render_pass_encoder::gl_render_pass_encoder(gl_device& device, const render_pass_descriptor& descriptor)
        : m_device{device}, m_active{true}
    {
        auto* target = device.lookup_render_target(descriptor.target);
        if (target == nullptr)
        {
            LOG_ERR("begin_render_pass: invalid render target handle");
            m_active = false;
            return;
        }

        m_target = descriptor.target;
        // The window backbuffer has one colour plane with no texture
        // behind it; an off-screen target has as many as it attached.
        m_color_count = target->framebuffer_id == 0 ? 1u : static_cast<uint32_t>(target->color.size());
        for (uint32_t i = 0; i < m_color_count && i < max_color_attachments; ++i)
        {
            m_color_store[i] = descriptor.color[i].store;
        }
        m_depth_store = descriptor.depth.store;
        m_use_depth = descriptor.use_depth;

        // Nothing the cache remembers from before this pass is trusted:
        // another pass, or the debug overlay recording into the same
        // context, may have changed anything since.
        m_device.invalidate_state_cache();
        auto& cache = m_device.state_cache();

        cache.bind_framebuffer(target->framebuffer_id);
        cache.set_scissor_test(false);
        if (target->width > 0 && target->height > 0)
        {
            cache.set_viewport(0, 0, static_cast<GLsizei>(target->width), static_cast<GLsizei>(target->height));
        }

        const bool depth_active = descriptor.use_depth && target->has_depth;

        // load_op::dont_care: neither loaded nor cleared, so tell the
        // driver the previous contents are dead.
        std::array<bool, max_color_attachments> discard{};
        for (uint32_t i = 0; i < m_color_count && i < max_color_attachments; ++i)
        {
            discard[i] = descriptor.color[i].load == load_op::dont_care;
        }
        invalidate_attachments(
            *target, discard, m_color_count, depth_active && descriptor.depth.load == load_op::dont_care);

        // Clears go through the named per-attachment entry points, so
        // each colour attachment takes its own value and the depth /
        // stencil planes clear together. Like glClear they honour the
        // write masks (and the scissor, disabled above), so the masks
        // are opened first — a previous pass's pipeline may have closed
        // them.
        const GLuint fbo = target->framebuffer_id;
        for (uint32_t i = 0; i < m_color_count && i < max_color_attachments; ++i)
        {
            if (descriptor.color[i].load != load_op::clear)
            {
                continue;
            }
            cache.set_color_mask(i, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            GL_CHECK(glClearNamedFramebufferfv(
                fbo, GL_COLOR, static_cast<GLint>(i), descriptor.color[i].clear_color.data()));
        }
        if (depth_active && descriptor.depth.load == load_op::clear)
        {
            cache.set_depth_write(true);
            if (target->has_stencil)
            {
                cache.set_stencil_mask(GL_FRONT, 0xFFFFFFFFu);
                cache.set_stencil_mask(GL_BACK, 0xFFFFFFFFu);
                GL_CHECK(glClearNamedFramebufferfi(fbo,
                                                   GL_DEPTH_STENCIL,
                                                   0,
                                                   descriptor.depth.clear_depth,
                                                   static_cast<GLint>(descriptor.depth.clear_stencil)));
            }
            else
            {
                GL_CHECK(glClearNamedFramebufferfv(fbo, GL_DEPTH, 0, &descriptor.depth.clear_depth));
            }
        }
    }

    gl_render_pass_encoder::~gl_render_pass_encoder()
    {
        if (m_active)
        {
            end();
        }
    }

    void gl_render_pass_encoder::set_pipeline(pipeline pipeline_handle)
    {
        auto* pipe = m_device.lookup_pipeline(pipeline_handle);
        if (pipe == nullptr)
        {
            LOG_WRN("set_pipeline: invalid pipeline handle");
            return;
        }
        if (pipe->is_compute)
        {
            LOG_WRN("set_pipeline: compute pipeline bound on render pass encoder");
            return;
        }
        const auto* target = m_device.lookup_render_target(m_target);
        if (target != nullptr && pipe->sample_count != target->samples)
        {
            LOG_WRN("set_pipeline: pipeline rasterises at %u samples, the target has %u",
                    pipe->sample_count,
                    target->samples);
        }
        m_pipeline_handle = pipeline_handle;
        m_program_id = pipe->program_id;
        m_vao_id = pipe->vao_id;
        m_topology = to_gl_primitive(pipe->topology);

        auto& cache = m_device.state_cache();
        cache.use_program(m_program_id);
        cache.bind_vertex_array(m_vao_id);
        apply_pipeline_state(
            cache, *pipe, m_use_depth, target != nullptr && target->has_stencil, m_color_count, m_stencil_reference);
        if (pipe->topology == primitive_topology::patches && pipe->patch_control_points > 0)
        {
            glPatchParameteri(GL_PATCH_VERTICES, static_cast<GLint>(pipe->patch_control_points));
        }
        // The element buffer is vertex-array state: a draw may only
        // source indices once set_index_buffer has attached one to
        // this pipeline's VAO.
        m_index_buffer_bound = false;
    }

    void gl_render_pass_encoder::set_vertex_buffer(uint32_t slot,
                                                   buffer buffer_handle,
                                                   size_t offset,
                                                   uint32_t stride_override)
    {
        auto* pipe = m_device.lookup_pipeline(m_pipeline_handle);
        auto* buf = m_device.lookup_buffer(buffer_handle);
        if (pipe == nullptr || buf == nullptr || slot >= pipe->vertex_buffers.size())
        {
            LOG_WRN("set_vertex_buffer: invalid pipeline / buffer / slot");
            return;
        }

        // The attribute formats are baked into the VAO; only the buffer
        // behind the slot's binding point changes here. A binding stride
        // of 0 would make every vertex read the same record (unlike the
        // old attribute-pointer path, where 0 meant tightly packed), so
        // a layout that leaves the stride to the draw and a draw that
        // leaves it to the layout fall back to the record the layout's
        // attributes span.
        const auto& layout = pipe->vertex_buffers[slot];
        uint32_t stride = stride_override != 0 ? stride_override : layout.stride;
        if (stride == 0)
        {
            stride = pipe->min_strides[slot];
        }

        sync_binding_shadows(m_device, *pipe);
        auto& shadow = pipe->vertex_binding_shadows[slot];
        const auto gl_offset = static_cast<GLintptr>(offset);
        const auto gl_stride = static_cast<GLsizei>(stride);
        if (shadow.known && shadow.buffer == buf->object_id && shadow.offset == gl_offset && shadow.stride == gl_stride)
        {
            return;
        }
        glVertexArrayVertexBuffer(pipe->vao_id, slot, buf->object_id, gl_offset, gl_stride);
        shadow.buffer = buf->object_id;
        shadow.offset = gl_offset;
        shadow.stride = gl_stride;
        shadow.known = true;
    }

    void gl_render_pass_encoder::set_index_buffer(buffer buffer_handle, index_format format)
    {
        auto* pipe = m_device.lookup_pipeline(m_pipeline_handle);
        auto* buf = m_device.lookup_buffer(buffer_handle);
        if (pipe == nullptr || buf == nullptr)
        {
            LOG_WRN("set_index_buffer: invalid pipeline / buffer handle");
            return;
        }

        // Attached to the pipeline's VAO by name, never through the
        // GL_ELEMENT_ARRAY_BUFFER target.
        sync_binding_shadows(m_device, *pipe);
        if (!pipe->element_buffer_known || pipe->element_buffer_shadow != buf->object_id)
        {
            glVertexArrayElementBuffer(pipe->vao_id, buf->object_id);
            pipe->element_buffer_shadow = buf->object_id;
            pipe->element_buffer_known = true;
        }
        m_index_type = to_gl_index_type(format);
        m_index_size = format == index_format::uint16 ? 2 : 4;
        m_index_buffer_bound = true;
    }

    void gl_render_pass_encoder::set_bind_group(uint32_t group,
                                                bind_group bind_group_handle,
                                                std::span<const uint32_t> dynamic_offsets)
    {
        auto* pipe = m_device.lookup_pipeline(m_pipeline_handle);
        auto* bg = m_device.lookup_bind_group(bind_group_handle);
        if (pipe == nullptr || bg == nullptr)
        {
            LOG_WRN("set_bind_group: invalid pipeline / bind_group");
            return;
        }
        if (group >= pipe->bind_group_layouts.size())
        {
            LOG_WRN("set_bind_group: group index out of range");
            return;
        }
        if (!dynamic_offsets_match(*bg, dynamic_offsets, group))
        {
            return;
        }
        apply_bind_group(m_device, *bg, dynamic_offsets);
    }

    void gl_render_pass_encoder::push_constants(shader_stages stages, uint32_t offset, uint32_t size, const void*)
    {
        // The device does not advertise push constants, so the renderer
        // never pushes here: it binds per-draw data from the per-draw
        // ring instead. A caller that ignores the feature gets its bytes
        // dropped, told once per pipeline.
        auto* pipe = m_device.lookup_pipeline(m_pipeline_handle);
        if (pipe != nullptr && !pipe->push_constants_reported)
        {
            pipe->push_constants_reported = true;
            LOG_WRN("push_constants: OpenGL has no push constants for SPIR-V programs; %u bytes at offset %u for "
                    "stages 0x%x are dropped",
                    size,
                    offset,
                    stages);
        }
    }

    void gl_render_pass_encoder::set_viewport(int x, int y, int width, int height)
    {
        auto& cache = m_device.state_cache();
        cache.set_viewport(x, y, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
        // The scissor follows the viewport, as it does on Vulkan where
        // both are set together.
        cache.set_scissor_test(true);
        cache.set_scissor(x, y, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    }

    void gl_render_pass_encoder::set_scissor(int x, int y, int width, int height)
    {
        auto& cache = m_device.state_cache();
        cache.set_scissor_test(true);
        cache.set_scissor(x, y, static_cast<GLsizei>(std::max(width, 0)), static_cast<GLsizei>(std::max(height, 0)));
    }

    void gl_render_pass_encoder::set_stencil_reference(uint32_t reference)
    {
        m_stencil_reference = reference;
        // The reference is part of glStencilFuncSeparate, so a bound
        // stencil pipeline has its funcs re-issued with the new value.
        const auto* pipe = m_device.lookup_pipeline(m_pipeline_handle);
        const auto* target = m_device.lookup_render_target(m_target);
        if (pipe == nullptr || target == nullptr || !pipe->stencil.test_enabled || !m_use_depth || !target->has_stencil)
        {
            return;
        }
        auto& cache = m_device.state_cache();
        apply_stencil_face(cache, GL_FRONT, pipe->stencil.front, reference, pipe->stencil);
        apply_stencil_face(cache, GL_BACK, pipe->stencil.back, reference, pipe->stencil);
    }

    void gl_render_pass_encoder::draw(uint32_t vertex_count,
                                      uint32_t instance_count,
                                      uint32_t first_vertex,
                                      uint32_t first_instance)
    {
        glDrawArraysInstancedBaseInstance(m_topology,
                                          static_cast<GLint>(first_vertex),
                                          static_cast<GLsizei>(vertex_count),
                                          static_cast<GLsizei>(instance_count),
                                          first_instance);
    }

    void gl_render_pass_encoder::draw_indexed(uint32_t index_count,
                                              uint32_t instance_count,
                                              uint32_t first_index,
                                              int32_t base_vertex,
                                              uint32_t first_instance)
    {
        if (!m_index_buffer_bound)
        {
            LOG_WRN("draw_indexed: no index buffer bound");
            return;
        }
        const auto byte_offset = static_cast<intptr_t>(first_index) * m_index_size;
        glDrawElementsInstancedBaseVertexBaseInstance(m_topology,
                                                      static_cast<GLsizei>(index_count),
                                                      m_index_type,
                                                      reinterpret_cast<const GLvoid*>(byte_offset),
                                                      static_cast<GLsizei>(instance_count),
                                                      base_vertex,
                                                      first_instance);
    }

    void gl_render_pass_encoder::draw_indexed_indirect(buffer indirect_buffer, size_t offset)
    {
        if (!m_index_buffer_bound)
        {
            LOG_WRN("draw_indexed_indirect: no index buffer bound");
            return;
        }
        auto* indirect = m_device.lookup_buffer(indirect_buffer);
        if (indirect == nullptr || indirect->object_id == 0)
        {
            LOG_WRN("draw_indexed_indirect: invalid indirect buffer");
            return;
        }
        m_device.state_cache().bind_draw_indirect_buffer(indirect->object_id);
        glDrawElementsIndirect(
            m_topology, m_index_type, reinterpret_cast<const GLvoid*>(static_cast<intptr_t>(offset)));
    }

    void gl_render_pass_encoder::multi_draw_indexed_indirect(buffer indirect_buffer,
                                                             size_t offset,
                                                             uint32_t draw_count,
                                                             uint32_t stride)
    {
        if (!m_index_buffer_bound)
        {
            LOG_WRN("multi_draw_indexed_indirect: no index buffer bound");
            return;
        }
        auto* indirect = m_device.lookup_buffer(indirect_buffer);
        if (indirect == nullptr || indirect->object_id == 0)
        {
            LOG_WRN("multi_draw_indexed_indirect: invalid indirect buffer");
            return;
        }
        m_device.state_cache().bind_draw_indirect_buffer(indirect->object_id);
        glMultiDrawElementsIndirect(m_topology,
                                    m_index_type,
                                    reinterpret_cast<const GLvoid*>(static_cast<intptr_t>(offset)),
                                    static_cast<GLsizei>(draw_count),
                                    static_cast<GLsizei>(stride));
    }

    void gl_render_pass_encoder::end()
    {
        if (!m_active)
        {
            return;
        }

        // store_op::dont_care: nothing downstream reads these planes,
        // so the driver may drop them instead of resolving them.
        if (const auto* target = m_device.lookup_render_target(m_target))
        {
            std::array<bool, max_color_attachments> discard{};
            for (uint32_t i = 0; i < m_color_count && i < max_color_attachments; ++i)
            {
                discard[i] = m_color_store[i] == store_op::dont_care;
            }
            invalidate_attachments(
                *target, discard, m_color_count, m_use_depth && m_depth_store == store_op::dont_care);
        }

        // Leave the context with nothing of this pass bound: the next
        // pass (or the window's present) starts from framebuffer 0, and
        // an outside recorder never inherits a VAO or program.
        auto& cache = m_device.state_cache();
        cache.bind_vertex_array(0);
        cache.use_program(0);
        cache.bind_framebuffer(0);
        m_device.invalidate_state_cache();
        m_active = false;
    }

    // -- gl_compute_pass_encoder ----------------------------------

    gl_compute_pass_encoder::gl_compute_pass_encoder(gl_device& device) : m_device{device}, m_active{true}
    {
        m_device.invalidate_state_cache();
    }

    gl_compute_pass_encoder::~gl_compute_pass_encoder()
    {
        if (m_active)
        {
            end();
        }
    }

    void gl_compute_pass_encoder::set_pipeline(pipeline pipeline_handle)
    {
        auto* pipe = m_device.lookup_pipeline(pipeline_handle);
        if (pipe == nullptr)
        {
            LOG_WRN("compute set_pipeline: invalid pipeline handle");
            return;
        }
        if (!pipe->is_compute)
        {
            LOG_WRN("compute set_pipeline: graphics pipeline bound on compute pass encoder");
            return;
        }
        m_pipeline_handle = pipeline_handle;
        m_program_id = pipe->program_id;
        m_device.state_cache().use_program(m_program_id);
    }

    void gl_compute_pass_encoder::set_bind_group(uint32_t group,
                                                 bind_group bind_group_handle,
                                                 std::span<const uint32_t> dynamic_offsets)
    {
        auto* pipe = m_device.lookup_pipeline(m_pipeline_handle);
        auto* bg = m_device.lookup_bind_group(bind_group_handle);
        if (pipe == nullptr || bg == nullptr)
        {
            LOG_WRN("compute set_bind_group: invalid pipeline / bind_group");
            return;
        }
        if (group >= pipe->bind_group_layouts.size())
        {
            LOG_WRN("compute set_bind_group: group index out of range");
            return;
        }
        if (!dynamic_offsets_match(*bg, dynamic_offsets, group))
        {
            return;
        }
        apply_bind_group(m_device, *bg, dynamic_offsets);
    }

    void gl_compute_pass_encoder::dispatch(uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z)
    {
        if (m_program_id == 0)
        {
            LOG_WRN("dispatch: no compute pipeline bound");
            return;
        }
        glDispatchCompute(group_count_x, group_count_y, group_count_z);
    }

    void gl_compute_pass_encoder::end()
    {
        if (!m_active)
        {
            return;
        }
        m_device.state_cache().use_program(0);
        m_device.invalidate_state_cache();
        m_active = false;
    }

    // -- gl_command_encoder -----------------------------------------

    gl_command_encoder::gl_command_encoder(gl_device& device) : m_device{device} {}

    std::unique_ptr<render_pass_encoder> gl_command_encoder::begin_render_pass(const render_pass_descriptor& descriptor)
    {
        return std::make_unique<gl_render_pass_encoder>(m_device, descriptor);
    }

    std::unique_ptr<compute_pass_encoder> gl_command_encoder::begin_compute_pass()
    {
        return std::make_unique<gl_compute_pass_encoder>(m_device);
    }

    void
    gl_command_encoder::copy_buffer_to_buffer(buffer src, size_t src_offset, buffer dst, size_t dst_offset, size_t size)
    {
        auto* src_record = m_device.lookup_buffer(src);
        auto* dst_record = m_device.lookup_buffer(dst);
        if (src_record == nullptr || src_record->object_id == 0 || dst_record == nullptr || dst_record->object_id == 0)
        {
            LOG_WRN("copy_buffer_to_buffer: invalid src / dst buffer");
            return;
        }
        // Named copy: no binding to the copy targets, so a pass that
        // is open around this call keeps its state.
        glCopyNamedBufferSubData(src_record->object_id,
                                 dst_record->object_id,
                                 static_cast<GLintptr>(src_offset),
                                 static_cast<GLintptr>(dst_offset),
                                 static_cast<GLsizeiptr>(size));
    }

    void gl_command_encoder::clear_buffer(buffer buffer_handle, size_t offset, size_t size, uint32_t value)
    {
        auto* record = m_device.lookup_buffer(buffer_handle);
        if (record == nullptr || record->object_id == 0)
        {
            LOG_WRN("clear_buffer: invalid buffer handle");
            return;
        }
        glClearNamedBufferSubData(record->object_id,
                                  GL_R32UI,
                                  static_cast<GLintptr>(offset),
                                  static_cast<GLsizeiptr>(size),
                                  GL_RED_INTEGER,
                                  GL_UNSIGNED_INT,
                                  &value);
    }

    void gl_command_encoder::barrier(pipeline_stage /*src_stage*/,
                                     pipeline_stage /*dst_stage*/,
                                     access_flag /*src_access*/,
                                     access_flag dst_access)
    {
        // OpenGL collapses pipeline-stage information into the
        // memory-barrier bitmask; the @c src / @c dst stages are
        // present for Vulkan portability and ignored here.
        glMemoryBarrier(to_gl_memory_barrier_bits(dst_access));
    }

    void gl_command_encoder::copy_buffer_to_texture(buffer src,
                                                    size_t src_offset,
                                                    texture dst,
                                                    const texture_copy_region& region)
    {
        auto* src_record = m_device.lookup_buffer(src);
        auto* dst_record = m_device.lookup_texture(dst);
        if (src_record == nullptr || src_record->object_id == 0 || dst_record == nullptr || dst_record->object_id == 0)
        {
            LOG_WRN("copy_buffer_to_texture: invalid src buffer / dst texture");
            return;
        }
        if ((dst_record->usage & texture_usage_copy_dst) == 0u)
        {
            LOG_WRN("copy_buffer_to_texture: the texture was created without texture_usage_copy_dst");
            return;
        }
        if (!copy_region_fits("copy_buffer_to_texture", *dst_record, region, *src_record, src_offset))
        {
            return;
        }
        // With a pixel unpack buffer bound the data pointer is an offset
        // into it; the rows are tightly packed (GL_UNPACK_ALIGNMENT 1).
        const auto fmt = to_gl_copy_format(dst_record->format);
        const auto* offset_pointer = reinterpret_cast<const void*>(static_cast<uintptr_t>(src_offset));
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, src_record->object_id);
        if (dst_record->target == GL_TEXTURE_2D)
        {
            GL_CHECK(glTextureSubImage2D(dst_record->object_id,
                                         static_cast<GLint>(region.mip_level),
                                         static_cast<GLint>(region.x),
                                         static_cast<GLint>(region.y),
                                         static_cast<GLsizei>(region.width),
                                         static_cast<GLsizei>(region.height),
                                         fmt.upload_format,
                                         fmt.upload_type,
                                         offset_pointer));
        }
        else
        {
            // Layered textures and cubes address the layer as z; a 3D
            // texture uses the region's own z range.
            const bool volume = dst_record->target == GL_TEXTURE_3D;
            GL_CHECK(glTextureSubImage3D(dst_record->object_id,
                                         static_cast<GLint>(region.mip_level),
                                         static_cast<GLint>(region.x),
                                         static_cast<GLint>(region.y),
                                         volume ? static_cast<GLint>(region.z) : static_cast<GLint>(region.layer),
                                         static_cast<GLsizei>(region.width),
                                         static_cast<GLsizei>(region.height),
                                         volume ? static_cast<GLsizei>(region.depth) : 1,
                                         fmt.upload_format,
                                         fmt.upload_type,
                                         offset_pointer));
        }
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    }

    void gl_command_encoder::copy_texture_to_buffer(texture src,
                                                    const texture_copy_region& region,
                                                    buffer dst,
                                                    size_t dst_offset)
    {
        auto* src_record = m_device.lookup_texture(src);
        auto* dst_record = m_device.lookup_buffer(dst);
        if (src_record == nullptr || src_record->object_id == 0 || dst_record == nullptr || dst_record->object_id == 0)
        {
            LOG_WRN("copy_texture_to_buffer: invalid src texture / dst buffer");
            return;
        }
        if ((src_record->usage & texture_usage_copy_src) == 0u)
        {
            LOG_WRN("copy_texture_to_buffer: the texture was created without texture_usage_copy_src");
            return;
        }
        if (!copy_region_fits("copy_texture_to_buffer", *src_record, region, *dst_record, dst_offset))
        {
            return;
        }
        // With a pixel pack buffer bound the destination pointer is an
        // offset into it, and the read is asynchronous like every other
        // recorded command (GL_PACK_ALIGNMENT 1 keeps rows tight).
        const bool volume = src_record->target == GL_TEXTURE_3D;
        const auto fmt = to_gl_copy_format(src_record->format);
        const size_t bytes = texture_region_bytes(src_record->format, region);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, dst_record->object_id);
        GL_CHECK(glGetTextureSubImage(src_record->object_id,
                                      static_cast<GLint>(region.mip_level),
                                      static_cast<GLint>(region.x),
                                      static_cast<GLint>(region.y),
                                      volume ? static_cast<GLint>(region.z) : static_cast<GLint>(region.layer),
                                      static_cast<GLsizei>(region.width),
                                      static_cast<GLsizei>(region.height),
                                      volume ? static_cast<GLsizei>(region.depth) : 1,
                                      fmt.upload_format,
                                      fmt.upload_type,
                                      static_cast<GLsizei>(bytes),
                                      reinterpret_cast<void*>(static_cast<uintptr_t>(dst_offset))));
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    }

    void gl_command_encoder::push_debug_group(const char* name)
    {
        if (name == nullptr)
        {
            name = "";
        }
        // KHR_debug groups are core in 4.3; a graphics debugger shows
        // them as a tree around the draws recorded inside.
        glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, name);
        ++m_debug_group_depth;
    }

    void gl_command_encoder::pop_debug_group()
    {
        if (m_debug_group_depth == 0)
        {
            return;
        }
        glPopDebugGroup();
        --m_debug_group_depth;
    }

    void gl_command_encoder::reset_queries(query_set /*set*/, uint32_t /*first*/, uint32_t /*count*/)
    {
        // A query object is rewritten by the next glQueryCounter; there
        // is nothing to reset between frames.
    }

    void gl_command_encoder::write_timestamp(query_set set, uint32_t index)
    {
        auto* record = m_device.lookup_query_set(set);
        if (record == nullptr)
        {
            return;
        }
        if (index >= record->ids.size())
        {
            LOG_WRN("write_timestamp: query %u of a %zu-query set", index, record->ids.size());
            return;
        }
        glQueryCounter(record->ids[index], GL_TIMESTAMP);
    }
} // namespace rendering_engine::gpu::backend::opengl
